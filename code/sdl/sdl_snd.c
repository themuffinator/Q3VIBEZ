/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifdef USE_LOCAL_HEADERS
#	include "SDL3/SDL.h"
#else
#	include <SDL3/SDL.h>
#endif

#include "../qcommon/q_shared.h"
#include "../client/snd_local.h"
#include "../client/client.h"

qboolean snd_inited = qfalse;

extern cvar_t *s_khz;
cvar_t *s_sdlBits;
cvar_t *s_sdlChannels;
cvar_t *s_sdlDevSamps;
cvar_t *s_sdlMixSamps;

static int dmapos = 0;
static int dmasize = 0;
static SDL_AudioFormat sdlPlaybackFormat = SDL_AUDIO_UNKNOWN;
static SDL_AudioStream *sdlPlaybackStream;

#if defined USE_VOIP && SDL_VERSION_ATLEAST( 2, 0, 5 )
#define USE_SDL_AUDIO_CAPTURE

static SDL_AudioStream *sdlCaptureStream;
static cvar_t *s_sdlCapture;
static float sdlMasterGain = 1.0f;
#endif

static void SNDDMA_QueueSilence( SDL_AudioStream *stream, int len )
{
	Uint8 silenceBuffer[4096];
	const Uint8 silence = (Uint8)SDL_GetSilenceValueForFormat( sdlPlaybackFormat != SDL_AUDIO_UNKNOWN ? sdlPlaybackFormat : SDL_AUDIO_S16 );

	while ( len > 0 )
	{
		const int chunk = len > (int)sizeof( silenceBuffer ) ? (int)sizeof( silenceBuffer ) : len;

		memset( silenceBuffer, silence, chunk );
		SDL_PutAudioStreamData( stream, silenceBuffer, chunk );
		len -= chunk;
	}
}

/*
===============
SNDDMA_AudioCallback
===============
*/
static void SDLCALL SNDDMA_AudioCallback( void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount )
{
	const int sampleBytes = dma.samplebits / 8;
	int remaining = additional_amount;

	(void) userdata;
	(void) total_amount;

	if ( !snd_inited || dma.buffer == NULL || dmasize <= 0 || sampleBytes <= 0 )
	{
		SNDDMA_QueueSilence( stream, remaining );
		return;
	}

	while ( remaining > 0 )
	{
		int pos = dmapos * sampleBytes;
		int chunk = remaining;
		int tobufend;

		if ( pos >= dmasize )
		{
			dmapos = 0;
			pos = 0;
		}

		tobufend = dmasize - pos;
		if ( chunk > tobufend )
			chunk = tobufend;

		if ( chunk <= 0 )
		{
			dmapos = 0;
			continue;
		}

		if ( !SDL_PutAudioStreamData( stream, dma.buffer + pos, chunk ) )
		{
			break;
		}

		dmapos += chunk / sampleBytes;
		if ( dmapos >= dma.samples )
			dmapos = 0;

		remaining -= chunk;
	}

	if ( remaining > 0 )
		SNDDMA_QueueSilence( stream, remaining );
}

/*
===============
SNDDMA_PrintAudiospec
===============
*/
static void SNDDMA_PrintAudiospec( const char *str, const SDL_AudioSpec *spec )
{
	Com_Printf( "%s:\n", str );
	Com_Printf( "  Format:   %s\n", SDL_GetAudioFormatName( spec->format ) );
	Com_Printf( "  Freq:     %d\n", spec->freq );
	Com_Printf( "  Channels: %d\n", spec->channels );
}


static int SNDDMA_KHzToHz( int khz )
{
	switch ( khz )
	{
		default:
		case 22: return 22050;
		case 48: return 48000;
		case 44: return 44100;
		case 11: return 11025;
		case  8: return  8000;
	}
}


static int SNDDMA_DefaultDeviceFrames( int freq )
{
	if ( freq <= 11025 )
		return 256;
	if ( freq <= 22050 )
		return 512;
	if ( freq <= 44100 )
		return 1024;
	return 2048;
}


/*
===============
SNDDMA_Init
===============
*/
qboolean SNDDMA_Init( void )
{
	SDL_AudioSpec desired;
	SDL_AudioSpec streamIn;
	SDL_AudioSpec streamOut;
	int tmp;
	int deviceFrames;

	if ( snd_inited )
		return qtrue;

	{
		s_sdlBits = Cvar_Get( "s_sdlBits", "16", CVAR_ARCHIVE_ND | CVAR_LATCH );
		Cvar_CheckRange( s_sdlBits, "8", "16", CV_INTEGER );
		Cvar_SetDescription( s_sdlBits, "Bits per-sample to request for SDL audio output (possible options: 8 or 16). When set to 0 it uses 16." );

		s_sdlChannels = Cvar_Get( "s_sdlChannels", "2", CVAR_ARCHIVE_ND | CVAR_LATCH );
		Cvar_CheckRange( s_sdlChannels, "1", "2", CV_INTEGER );
		Cvar_SetDescription( s_sdlChannels, "Number of audio channels to request for SDL audio output. The Quake 3 audio mixer only supports mono and stereo. Additional channels are silent." );

		s_sdlDevSamps = Cvar_Get( "s_sdlDevSamps", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
		Cvar_SetDescription( s_sdlDevSamps, "Target device sample-frame chunk size for the SDL3 playback stream. When set to 0 it picks a value based on s_sdlSpeed." );
		s_sdlMixSamps = Cvar_Get( "s_sdlMixSamps", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
		Cvar_SetDescription( s_sdlMixSamps, "Number of audio samples for Quake 3's audio mixer when using SDL audio output." );
	}

	Com_Printf( "SDL_InitSubSystem( SDL_INIT_AUDIO )... " );

	if ( !SDL_InitSubSystem( SDL_INIT_AUDIO ) )
	{
		Com_Printf( "FAILED (%s)\n", SDL_GetError() );
		return qfalse;
	}

	Com_Printf( "OK\n" );
	Com_Printf( "SDL audio driver is \"%s\".\n", SDL_GetCurrentAudioDriver() );

	SDL_zero( desired );
	SDL_zero( streamIn );
	SDL_zero( streamOut );

	desired.freq = SNDDMA_KHzToHz( s_khz->integer );
	if ( desired.freq == 0 )
		desired.freq = 22050;

	tmp = s_sdlBits->integer;
	if ( tmp < 16 )
		tmp = 8;

	desired.format = ( tmp == 16 ) ? SDL_AUDIO_S16 : SDL_AUDIO_U8;
	desired.channels = s_sdlChannels->integer;

	sdlPlaybackStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, SNDDMA_AudioCallback, NULL );
	if ( sdlPlaybackStream == NULL )
	{
		Com_Printf( "SDL_OpenAudioDeviceStream() failed: %s\n", SDL_GetError() );
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		return qfalse;
	}

	if ( !SDL_GetAudioStreamFormat( sdlPlaybackStream, &streamIn, &streamOut ) )
	{
		Com_DPrintf( "SDL_GetAudioStreamFormat() failed: %s\n", SDL_GetError() );
		streamIn = desired;
		streamOut = desired;
	}

	SNDDMA_PrintAudiospec( "SDL playback stream input", &streamIn );
	SNDDMA_PrintAudiospec( "SDL playback device output", &streamOut );

	deviceFrames = s_sdlDevSamps->integer;
	if ( !deviceFrames )
		deviceFrames = SNDDMA_DefaultDeviceFrames( streamOut.freq ? streamOut.freq : streamIn.freq );

	tmp = s_sdlMixSamps->integer;
	if ( !tmp )
		tmp = ( deviceFrames * streamIn.channels ) * 10;

	tmp -= tmp % streamIn.channels;
	tmp = log2pad( tmp, 1 );

	dmapos = 0;
	dma.samplebits = SDL_AUDIO_BITSIZE( streamIn.format );
	dma.isfloat = SDL_AUDIO_ISFLOAT( streamIn.format ) ? qtrue : qfalse;
	dma.channels = streamIn.channels;
	dma.samples = tmp;
	dma.fullsamples = dma.samples / dma.channels;
	dma.submission_chunk = 1;
	dma.speed = streamIn.freq;
	dma.driver = "SDL3";
	dmasize = dma.samples * SDL_AUDIO_BYTESIZE( streamIn.format );
	dma.buffer = calloc( 1, dmasize );
	sdlPlaybackFormat = streamIn.format;

	if ( dma.buffer == NULL )
	{
		Com_Printf( "Failed to allocate SDL audio DMA buffer.\n" );
		SDL_DestroyAudioStream( sdlPlaybackStream );
		sdlPlaybackStream = NULL;
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		return qfalse;
	}

#ifdef USE_SDL_AUDIO_CAPTURE
	s_sdlCapture = Cvar_Get( "s_sdlCapture", "1", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( s_sdlCapture, "Set to 1 to enable SDL audio capture." );
	if ( !s_sdlCapture->integer )
	{
		Com_Printf( "SDL audio capture support disabled by user ('+set s_sdlCapture 1' to enable)\n" );
	}
#if USE_MUMBLE
	else if ( cl_useMumble->integer )
	{
		Com_Printf( "SDL audio capture support disabled for Mumble support\n" );
	}
#endif
	else
	{
		SDL_AudioSpec spec;
		SDL_zero( spec );
		spec.freq = 48000;
		spec.format = SDL_AUDIO_S16;
		spec.channels = 1;
		sdlCaptureStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL );
		Com_Printf( "SDL capture stream %s.\n", sdlCaptureStream == NULL ? "failed to open" : "opened" );
		if ( sdlCaptureStream == NULL )
			Com_DPrintf( "SDL_OpenAudioDeviceStream(recording) failed: %s\n", SDL_GetError() );
	}

	sdlMasterGain = 1.0f;
	SDL_SetAudioStreamGain( sdlPlaybackStream, sdlMasterGain );
#endif

	Com_Printf( "Starting SDL audio stream...\n" );
	if ( !SDL_ResumeAudioStreamDevice( sdlPlaybackStream ) )
	{
		Com_Printf( "SDL_ResumeAudioStreamDevice() failed: %s\n", SDL_GetError() );
#ifdef USE_SDL_AUDIO_CAPTURE
		if ( sdlCaptureStream != NULL )
		{
			SDL_DestroyAudioStream( sdlCaptureStream );
			sdlCaptureStream = NULL;
		}
#endif
		SDL_DestroyAudioStream( sdlPlaybackStream );
		sdlPlaybackStream = NULL;
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		free( dma.buffer );
		dma.buffer = NULL;
		return qfalse;
	}

	Com_Printf( "SDL audio initialized.\n" );
	snd_inited = qtrue;
	return qtrue;
}


/*
===============
SNDDMA_GetDMAPos
===============
*/
int SNDDMA_GetDMAPos( void )
{
	return dmapos;
}


/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown( void )
{
	if ( sdlPlaybackStream != NULL )
	{
		Com_Printf( "Closing SDL audio playback stream...\n" );
		SDL_DestroyAudioStream( sdlPlaybackStream );
		Com_Printf( "SDL audio playback stream closed.\n" );
		sdlPlaybackStream = NULL;
	}

#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream != NULL )
	{
		Com_Printf( "Closing SDL audio capture stream...\n" );
		SDL_DestroyAudioStream( sdlCaptureStream );
		Com_Printf( "SDL audio capture stream closed.\n" );
		sdlCaptureStream = NULL;
	}
#endif

	SDL_QuitSubSystem( SDL_INIT_AUDIO );
	free( dma.buffer );
	dma.buffer = NULL;
	dmapos = 0;
	dmasize = 0;
	sdlPlaybackFormat = SDL_AUDIO_UNKNOWN;
	dma.driver = NULL;
	snd_inited = qfalse;
	Com_Printf( "SDL audio shut down.\n" );
}


/*
===============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit( void )
{
	if ( sdlPlaybackStream != NULL )
		SDL_UnlockAudioStream( sdlPlaybackStream );
}


/*
===============
SNDDMA_BeginPainting
===============
*/
void SNDDMA_BeginPainting( void )
{
	if ( sdlPlaybackStream != NULL )
		SDL_LockAudioStream( sdlPlaybackStream );
}


#ifdef USE_VOIP
void SNDDMA_StartCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream != NULL )
	{
		SDL_ClearAudioStream( sdlCaptureStream );
		SDL_ResumeAudioStreamDevice( sdlCaptureStream );
	}
#endif
}


int SNDDMA_AvailableCaptureSamples(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream != NULL )
	{
		const int available = SDL_GetAudioStreamAvailable( sdlCaptureStream );
		return available > 0 ? ( available / (int)sizeof( Sint16 ) ) : 0;
	}
#endif
	return 0;
}


void SNDDMA_Capture(int samples, byte *data)
{
	const int bytes = samples * (int)sizeof( Sint16 );

#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream != NULL )
	{
		int got = SDL_GetAudioStreamData( sdlCaptureStream, data, bytes );
		if ( got < 0 )
			got = 0;
		if ( got < bytes )
			SDL_memset( data + got, '\0', bytes - got );
		return;
	}
#endif

	SDL_memset( data, '\0', bytes );
}

void SNDDMA_StopCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream != NULL )
	{
		SDL_PauseAudioStreamDevice( sdlCaptureStream );
	}
#endif
}

void SNDDMA_MasterGain( float val )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	sdlMasterGain = val;
	if ( sdlPlaybackStream != NULL )
		SDL_SetAudioStreamGain( sdlPlaybackStream, sdlMasterGain );
#else
	(void) val;
#endif
}
#endif
