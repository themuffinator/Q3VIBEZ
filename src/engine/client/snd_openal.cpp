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

#include "client.h"
#include "snd_codec.h"
#include "snd_local.h"

#ifdef USE_OPENAL

#include <ctype.h>
#include <float.h>
#include <math.h>

#define AL_ALEXT_PROTOTYPES

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <AL/efx-presets.h>

namespace {

constexpr int MAX_OPENAL_SFX = 4096;
constexpr int OPENAL_SFX_HASH = 128;
constexpr int MAX_REVERB_ZONES = 256;
constexpr int MAX_STREAM_BUFFERS = 4;
constexpr int STREAM_BUFFER_BYTES = 16384;
constexpr float REVERB_PROBE_DISTANCE = 8192.0f;
constexpr float SOUND_FULLVOLUME = 80.0f;
constexpr float SOUND_MAXDISTANCE = 1330.0f;
constexpr float LEGACY_MASTER_GAIN = 127.0f / 255.0f;
constexpr float LEGACY_SPHERE_GAIN = 90.0f / 255.0f;
constexpr float REVERB_LERP_PER_MSEC = 0.004f;

enum alReverbPresetId_t {
	AL_REVERB_DEFAULT = 0,
	AL_REVERB_UNDERWATER,
	AL_REVERB_ABANDONED,
	AL_REVERB_ALLEY,
	AL_REVERB_ARENA,
	AL_REVERB_AUDITORIUM,
	AL_REVERB_BATHROOM,
	AL_REVERB_CARPETED_HALLWAY,
	AL_REVERB_CAVE,
	AL_REVERB_CHAPEL,
	AL_REVERB_CITY,
	AL_REVERB_CITY_STREETS,
	AL_REVERB_CONCERT_HALL,
	AL_REVERB_DIZZY,
	AL_REVERB_DRUGGED,
	AL_REVERB_DUSTY_ROOM,
	AL_REVERB_FOREST,
	AL_REVERB_HALLWAY,
	AL_REVERB_HANGAR,
	AL_REVERB_LIBRARY,
	AL_REVERB_LIVINGROOM,
	AL_REVERB_MOUNTAINS,
	AL_REVERB_MUSEUM,
	AL_REVERB_PADDED_CELL,
	AL_REVERB_PARKING_LOT,
	AL_REVERB_PLAIN,
	AL_REVERB_PSYCHOTIC,
	AL_REVERB_QUARRY,
	AL_REVERB_ROOM,
	AL_REVERB_SEWER_PIPE,
	AL_REVERB_SMALL_WATER_ROOM,
	AL_REVERB_STONE_CORRIDOR,
	AL_REVERB_STONE_ROOM,
	AL_REVERB_SUBWAY,
	AL_REVERB_UNDERPASS,
	AL_REVERB_COUNT
};

struct alReverbPreset_t {
	const char *name;
	EFXEAXREVERBPROPERTIES properties;
};

struct alReverbZone_t {
	vec3_t origin;
	float radius;
	int presetId;
};

struct alPresetReverbState_t {
	ALuint effect;
	ALuint slot;
	qboolean initialized;
};

struct alSfx_t {
	char name[MAX_QPATH];
	ALuint buffer;
	int channels;
	int sampleRate;
	int frameCount;
	int bufferBytes;
	int lastUsed;
	qboolean failed;
	alSfx_t *next;
};

struct alLoopSound_t {
	vec3_t origin;
	vec3_t velocity;
	int sfxHandle;
	qboolean active;
	qboolean kill;
	qboolean realLoop;
};

struct alChannel_t {
	qboolean active;
	qboolean looping;
	qboolean loopSeen;
	qboolean fixedOrigin;
	qboolean localSound;
	int entnum;
	int entchannel;
	int sfxHandle;
	int allocTime;
	vec3_t origin;
	vec3_t velocity;
	float masterGain;
	ALuint source;
};

struct alMusicState_t {
	snd_stream_t *stream;
	char loopName[MAX_QPATH];
	ALuint source;
	ALuint buffers[MAX_STREAM_BUFFERS];
	int queuedBuffers;
	qboolean bufferQueued[MAX_STREAM_BUFFERS];
};

static const EFXEAXREVERBPROPERTIES kDefaultReverb = EFX_REVERB_PRESET_GENERIC;

static const alReverbPreset_t kReverbPresets[AL_REVERB_COUNT] = {
	{ "generic", kDefaultReverb },
	{ "underwater", EFX_REVERB_PRESET_UNDERWATER },
	{ "abandoned", EFX_REVERB_PRESET_CITY_ABANDONED },
	{ "alley", EFX_REVERB_PRESET_ALLEY },
	{ "arena", EFX_REVERB_PRESET_ARENA },
	{ "auditorium", EFX_REVERB_PRESET_AUDITORIUM },
	{ "bathroom", EFX_REVERB_PRESET_BATHROOM },
	{ "carpetedhallway", EFX_REVERB_PRESET_CARPETEDHALLWAY },
	{ "cave", EFX_REVERB_PRESET_CAVE },
	{ "chapel", EFX_REVERB_PRESET_CHAPEL },
	{ "city", EFX_REVERB_PRESET_CITY },
	{ "citystreets", EFX_REVERB_PRESET_CITY_STREETS },
	{ "concerthall", EFX_REVERB_PRESET_CONCERTHALL },
	{ "dizzy", EFX_REVERB_PRESET_DIZZY },
	{ "drugged", EFX_REVERB_PRESET_DRUGGED },
	{ "dustyroom", EFX_REVERB_PRESET_DUSTYROOM },
	{ "forest", EFX_REVERB_PRESET_FOREST },
	{ "hallway", EFX_REVERB_PRESET_HALLWAY },
	{ "hangar", EFX_REVERB_PRESET_HANGAR },
	{ "library", EFX_REVERB_PRESET_CITY_LIBRARY },
	{ "livingroom", EFX_REVERB_PRESET_LIVINGROOM },
	{ "mountains", EFX_REVERB_PRESET_MOUNTAINS },
	{ "museum", EFX_REVERB_PRESET_CITY_MUSEUM },
	{ "paddedcell", EFX_REVERB_PRESET_PADDEDCELL },
	{ "parkinglot", EFX_REVERB_PRESET_PARKINGLOT },
	{ "plain", EFX_REVERB_PRESET_PLAIN },
	{ "psychotic", EFX_REVERB_PRESET_PSYCHOTIC },
	{ "quarry", EFX_REVERB_PRESET_QUARRY },
	{ "room", EFX_REVERB_PRESET_ROOM },
	{ "sewerpipe", EFX_REVERB_PRESET_SEWERPIPE },
	{ "smallwaterroom", EFX_REVERB_PRESET_SMALLWATERROOM },
	{ "stonecorridor", EFX_REVERB_PRESET_STONECORRIDOR },
	{ "stoneroom", EFX_REVERB_PRESET_STONEROOM },
	{ "subway", EFX_REVERB_PRESET_CITY_SUBWAY },
	{ "underpass", EFX_REVERB_PRESET_CITY_UNDERPASS },
};

static const vec3_t kReverbProbeDirections[] = {
	{ 0.0f, 0.0f, -1.0f },
	{ 0.0f, 0.0f, 1.0f },
	{ 0.707106769f, 0.0f, 0.707106769f },
	{ 0.353553385f, 0.612372458f, 0.707106769f },
	{ -0.353553444f, 0.612372458f, 0.707106769f },
	{ -0.707106769f, -0.0000000618172393f, 0.707106769f },
	{ -0.353553325f, -0.612372518f, 0.707106769f },
	{ 0.353553355f, -0.612372458f, 0.707106769f },
	{ 1.0f, 0.0f, -0.0000000437113883f },
	{ 0.499999970f, 0.866025448f, -0.0000000437113883f },
	{ -0.500000060f, 0.866025388f, -0.0000000437113883f },
	{ -1.0f, -0.0000000874227766f, -0.0000000437113883f },
	{ -0.499999911f, -0.866025448f, -0.0000000437113883f },
	{ 0.499999911f, -0.866025448f, -0.0000000437113883f },
};

static ALCdevice *s_alDevice = nullptr;
static ALCcontext *s_alContext = nullptr;
static qboolean s_alEfxAvailable = qfalse;
static qboolean s_alEaxReverbAvailable = qfalse;
static qboolean s_alHrtfAvailable = qfalse;
static qboolean s_alSourceSpatializeAvailable = qfalse;
static int s_alNumChannels = 0;

static ALuint s_reverbEffect = 0;
static ALuint s_reverbSlot = 0;
static ALuint s_occlusionDirectFilter = 0;
static ALuint s_occlusionSendFilter = 0;
static ALuint s_rawSource = 0;
static int s_rawQueuedBuffers = 0;
static alPresetReverbState_t s_presetReverbs[AL_REVERB_COUNT];
static float s_presetReverbLevel = -1.0f;

static alMusicState_t s_music = {};
static alChannel_t s_alChannels[MAX_CHANNELS];
static alLoopSound_t s_loopSounds[MAX_GENTITIES];
static vec3_t s_entityOrigins[MAX_GENTITIES];
static alSfx_t s_knownSfx[MAX_OPENAL_SFX];
static alSfx_t *s_sfxHash[OPENAL_SFX_HASH];
static int s_numSfx = 0;

static alReverbZone_t s_reverbZones[MAX_REVERB_ZONES];
static int s_numReverbZones = 0;
static char s_reverbMapName[MAX_QPATH] = {};
static char s_bspReverbMapName[MAX_QPATH] = {};
static int s_targetReverbPreset = AL_REVERB_DEFAULT;
static EFXEAXREVERBPROPERTIES s_currentReverb = kDefaultReverb;
static EFXEAXREVERBPROPERTIES s_targetReverb = kDefaultReverb;
static int s_bspReverbProbeTime = 0;
static float s_bspReverbAverage = REVERB_PROBE_DISTANCE;
static int s_bspReverbPreset = AL_REVERB_DEFAULT;
static qboolean s_bspReverbOpenSky = qfalse;

static qboolean s_soundStarted = qfalse;
static qboolean s_soundMuted = qfalse;
static int s_listenerEntity = ENTITYNUM_NONE;
static int s_listenerInWater = 0;
static vec3_t s_listenerOrigin = {};
static vec3_t s_listenerAxis[3] = {};

static cvar_t *s_openalHrtf = nullptr;
static cvar_t *s_openalHrtfAvailable = nullptr;
static cvar_t *s_openalReverb = nullptr;
static cvar_t *s_openalEfxAvailable = nullptr;
static cvar_t *s_openalEaxAvailable = nullptr;
static cvar_t *s_openalReverbPreset = nullptr;
static cvar_t *s_openalReverbLevel = nullptr;
static cvar_t *s_openalOcclusion = nullptr;
static cvar_t *s_openalOcclusionStrength = nullptr;
static cvar_t *s_openalZoneDebug = nullptr;

static unsigned int S_AL_HashSfxName( const char *name );
static alSfx_t *S_AL_FindSfx( const char *name );
static qboolean S_AL_LoadSfx( alSfx_t *sfx, int handle );
static void S_AL_StopChannel( alChannel_t *channel );
static void S_AL_UpdateChannel( alChannel_t *channel );
static void S_AL_UpdateLoopingSounds( void );
static void S_AL_StopAllSources( void );
static void S_AL_ClearRawStream( void );
static void S_AL_StopBackgroundTrack( void );
static void S_AL_UpdateBackgroundTrack( void );
static void S_AL_RefreshReverbState( int msec );
static void S_AL_ParseReverbZones( void );
static int S_AL_SelectBspReverbPreset( void );
static int S_AL_SelectZonePresetForPoint( const vec3_t point );
static ALuint S_AL_GetSourceReverbSlot( const vec3_t origin );
static sfxHandle_t S_AL_RegisterSound( const char *sample, qboolean compressed );

} // namespace

namespace {

static void S_AL_CopyVectorToOpenAL( const vec3_t in, ALfloat out[3] )
{
	out[0] = -in[1];
	out[1] = in[2];
	out[2] = -in[0];
}

static void S_AL_ApplyPosition( ALuint source, const vec3_t origin )
{
	ALfloat position[3];
	S_AL_CopyVectorToOpenAL( origin, position );
	alSource3f( source, AL_POSITION, position[0], position[1], position[2] );
}

static void S_AL_ApplyVelocity( ALuint source, const vec3_t velocity )
{
	ALfloat alVelocity[3];
	S_AL_CopyVectorToOpenAL( velocity, alVelocity );
	alSource3f( source, AL_VELOCITY, alVelocity[0], alVelocity[1], alVelocity[2] );
}

static int S_AL_ClampPresetId( const int presetId )
{
	return Com_Clamp( 0, AL_REVERB_COUNT - 1, presetId );
}

static const alReverbPreset_t *S_AL_GetPreset( const int presetId )
{
	return &kReverbPresets[S_AL_ClampPresetId( presetId )];
}

static EFXEAXREVERBPROPERTIES S_AL_ScaleReverbPreset( const int presetId )
{
	EFXEAXREVERBPROPERTIES scaled = S_AL_GetPreset( presetId )->properties;
	const float level = Com_Clamp( 0.0f, 2.0f, s_openalReverbLevel->value );

	scaled.flGain *= level;
	scaled.flReflectionsGain *= level;
	scaled.flLateReverbGain *= level;
	return scaled;
}

static ALenum S_AL_GetBufferFormat( const int channels, const int width )
{
	if ( width == 1 ) {
		switch ( channels ) {
			case 1:
				return AL_FORMAT_MONO8;
			case 2:
				return AL_FORMAT_STEREO8;
			default:
				return AL_NONE;
		}
	}

	if ( width == 2 ) {
		switch ( channels ) {
			case 1:
				return AL_FORMAT_MONO16;
			case 2:
				return AL_FORMAT_STEREO16;
			default:
				return AL_NONE;
		}
	}

	return AL_NONE;
}

static qboolean S_AL_IsMuted( void )
{
	return ( !gw_active && !gw_minimized && s_muteWhenUnfocused->integer ) ||
		( gw_minimized && s_muteWhenMinimized->integer );
}

static void S_AL_PublishCapabilities( void )
{
	Cvar_Set2( s_openalHrtfAvailable ? s_openalHrtfAvailable->name : "s_openalHrtfAvailable", s_alHrtfAvailable ? "1" : "0", qtrue );
	Cvar_Set2( s_openalEfxAvailable ? s_openalEfxAvailable->name : "s_openalEfxAvailable", s_alEfxAvailable ? "1" : "0", qtrue );
	Cvar_Set2( s_openalEaxAvailable ? s_openalEaxAvailable->name : "s_openalEaxAvailable", s_alEaxReverbAvailable ? "1" : "0", qtrue );
}

static void S_AL_UpdateOcclusionFilters( void )
{
	const float strength = Com_Clamp( 0.0f, 1.0f, s_openalOcclusionStrength ? s_openalOcclusionStrength->value : 0.0f );

	if ( !s_alEfxAvailable || !s_occlusionDirectFilter || !s_occlusionSendFilter ) {
		return;
	}

	alFilteri( s_occlusionDirectFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
	alFilterf( s_occlusionDirectFilter, AL_LOWPASS_GAIN, 1.0f - 0.6f * strength );
	alFilterf( s_occlusionDirectFilter, AL_LOWPASS_GAINHF, 1.0f - 0.9f * strength );
	alFilteri( s_occlusionSendFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
	alFilterf( s_occlusionSendFilter, AL_LOWPASS_GAIN, 1.0f );
	alFilterf( s_occlusionSendFilter, AL_LOWPASS_GAINHF, 1.0f - 0.5f * strength );
}

static int S_AL_FindMusicBufferIndex( const ALuint buffer )
{
	for ( int i = 0; i < MAX_STREAM_BUFFERS; ++i ) {
		if ( s_music.buffers[i] == buffer ) {
			return i;
		}
	}

	return -1;
}

static void S_AL_MarkMusicBufferQueued( const ALuint buffer, const qboolean queued )
{
	const int index = S_AL_FindMusicBufferIndex( buffer );
	if ( index >= 0 ) {
		s_music.bufferQueued[index] = queued;
	}
}

static qboolean S_AL_HasWorldCollisionModel( void )
{
	return CM_NumInlineModels() > 0 && cls.state >= CA_LOADING && cls.state != CA_CINEMATIC;
}

static qboolean S_AL_ShouldKeepLocalSoundDry( const alChannel_t *channel )
{
	return channel->localSound &&
		( cls.state != CA_ACTIVE || ( Key_GetCatcher() & KEYCATCH_UI ) != 0 ||
			channel->entchannel == CHAN_LOCAL || channel->entchannel == CHAN_LOCAL_SOUND );
}

static void S_AL_SetChannelFilters( const alChannel_t *channel, const qboolean useOcclusion, const ALuint reverbSlot )
{
	const qboolean dryLocalSound = S_AL_ShouldKeepLocalSoundDry( channel );
	const qboolean listenerOwned = dryLocalSound || channel->entnum == s_listenerEntity;

	if ( !s_alEfxAvailable ) {
		alSourcei( channel->source, AL_DIRECT_FILTER, 0 );
		alSource3i( channel->source, AL_AUXILIARY_SEND_FILTER, 0, 0, 0 );
		return;
	}

	if ( s_openalReverb->integer && reverbSlot && !dryLocalSound ) {
		alSource3i(
			channel->source,
			AL_AUXILIARY_SEND_FILTER,
			reverbSlot,
			0,
			( useOcclusion && !listenerOwned ) ? static_cast<ALint>( s_occlusionSendFilter ) : 0
		);
	} else {
		alSource3i( channel->source, AL_AUXILIARY_SEND_FILTER, 0, 0, 0 );
	}

	alSourcei(
		channel->source,
		AL_DIRECT_FILTER,
		( useOcclusion && !listenerOwned ) ? static_cast<ALint>( s_occlusionDirectFilter ) : 0
	);
}

static qboolean S_AL_IsOccluded( const vec3_t origin )
{
	trace_t trace;

	if ( !s_alEfxAvailable || !s_openalOcclusion->integer || !S_AL_HasWorldCollisionModel() ) {
		return qfalse;
	}

	CM_BoxTrace( &trace, s_listenerOrigin, origin, vec3_origin, vec3_origin, 0, MASK_SOLID, qfalse );
	return trace.fraction < 1.0f;
}

static unsigned int S_AL_HashSfxName( const char *name )
{
	unsigned int hash = 0;

	for ( int i = 0; name[i] != '\0'; ++i ) {
		char letter = static_cast<char>( tolower( name[i] ) );
		if ( letter == '.' ) {
			break;
		}
		if ( letter == '\\' ) {
			letter = '/';
		}
		hash += static_cast<unsigned int>( letter ) * static_cast<unsigned int>( i + 119 );
	}

	return hash & ( OPENAL_SFX_HASH - 1 );
}

static alSfx_t *S_AL_FindSfx( const char *name )
{
	const unsigned int hash = S_AL_HashSfxName( name );
	alSfx_t *sfx = s_sfxHash[hash];

	while ( sfx ) {
		if ( !Q_stricmp( sfx->name, name ) ) {
			return sfx;
		}
		sfx = sfx->next;
	}

	if ( s_numSfx >= MAX_OPENAL_SFX ) {
		Com_Printf( S_COLOR_RED "ERROR: OpenAL sound registry exhausted.\n" );
		return nullptr;
	}

	sfx = &s_knownSfx[s_numSfx++];
	Com_Memset( sfx, 0, sizeof( *sfx ) );
	Q_strncpyz( sfx->name, name, sizeof( sfx->name ) );
	sfx->next = s_sfxHash[hash];
	s_sfxHash[hash] = sfx;
	return sfx;
}

static qboolean S_AL_LoadSfx( alSfx_t *sfx, const int handle )
{
	snd_info_t info;
	byte *data;
	const byte *sampleData;
	ALenum format;

	if ( sfx->buffer || sfx->failed ) {
		return sfx->buffer != 0;
	}

	data = static_cast<byte *>( S_CodecLoad( sfx->name, &info ) );
	if ( !data ) {
		sfx->failed = qtrue;
		return qfalse;
	}

	format = S_AL_GetBufferFormat( info.channels, info.width );
	sampleData = data + info.dataofs;

	if ( format == AL_NONE || info.rate <= 0 || info.size <= 0 ) {
		Com_Printf(
			S_COLOR_YELLOW "WARNING: Unsupported format for %s (%d ch, %d-bit, %d Hz)\n",
			sfx->name,
			info.channels,
			info.width * 8,
			info.rate
		);
		Hunk_FreeTempMemory( data );
		sfx->failed = qtrue;
		return qfalse;
	}

	alGenBuffers( 1, &sfx->buffer );
	alBufferData( sfx->buffer, format, sampleData, info.size, info.rate );
	Hunk_FreeTempMemory( data );

	if ( alGetError() != AL_NO_ERROR ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Failed to upload %s to OpenAL\n", sfx->name );
		if ( sfx->buffer ) {
			alDeleteBuffers( 1, &sfx->buffer );
			sfx->buffer = 0;
		}
		sfx->failed = qtrue;
		return qfalse;
	}

	sfx->channels = info.channels;
	sfx->sampleRate = info.rate;
	sfx->frameCount = info.samples;
	sfx->bufferBytes = info.size;
	sfx->lastUsed = cls.realtime;

	if ( s_openalZoneDebug->integer > 1 ) {
		Com_Printf( "OpenAL loaded sound #%d: %s\n", handle, sfx->name );
	}

	return qtrue;
}

static void S_AL_LerpReverb( EFXEAXREVERBPROPERTIES *dest, const EFXEAXREVERBPROPERTIES *target, const float fraction )
{
	const float t = Com_Clamp( 0.0f, 1.0f, fraction );
	const auto lerpFloat = [t]( const float a, const float b ) {
		return a + ( b - a ) * t;
	};

	dest->flDensity = lerpFloat( dest->flDensity, target->flDensity );
	dest->flDiffusion = lerpFloat( dest->flDiffusion, target->flDiffusion );
	dest->flGain = lerpFloat( dest->flGain, target->flGain );
	dest->flGainHF = lerpFloat( dest->flGainHF, target->flGainHF );
	dest->flGainLF = lerpFloat( dest->flGainLF, target->flGainLF );
	dest->flDecayTime = lerpFloat( dest->flDecayTime, target->flDecayTime );
	dest->flDecayHFRatio = lerpFloat( dest->flDecayHFRatio, target->flDecayHFRatio );
	dest->flDecayLFRatio = lerpFloat( dest->flDecayLFRatio, target->flDecayLFRatio );
	dest->flReflectionsGain = lerpFloat( dest->flReflectionsGain, target->flReflectionsGain );
	dest->flReflectionsDelay = lerpFloat( dest->flReflectionsDelay, target->flReflectionsDelay );
	dest->flLateReverbGain = lerpFloat( dest->flLateReverbGain, target->flLateReverbGain );
	dest->flLateReverbDelay = lerpFloat( dest->flLateReverbDelay, target->flLateReverbDelay );
	dest->flEchoTime = lerpFloat( dest->flEchoTime, target->flEchoTime );
	dest->flEchoDepth = lerpFloat( dest->flEchoDepth, target->flEchoDepth );
	dest->flModulationTime = lerpFloat( dest->flModulationTime, target->flModulationTime );
	dest->flModulationDepth = lerpFloat( dest->flModulationDepth, target->flModulationDepth );
	dest->flAirAbsorptionGainHF = lerpFloat( dest->flAirAbsorptionGainHF, target->flAirAbsorptionGainHF );
	dest->flHFReference = lerpFloat( dest->flHFReference, target->flHFReference );
	dest->flLFReference = lerpFloat( dest->flLFReference, target->flLFReference );
	dest->flRoomRolloffFactor = lerpFloat( dest->flRoomRolloffFactor, target->flRoomRolloffFactor );

	for ( int i = 0; i < 3; ++i ) {
		dest->flReflectionsPan[i] = lerpFloat( dest->flReflectionsPan[i], target->flReflectionsPan[i] );
		dest->flLateReverbPan[i] = lerpFloat( dest->flLateReverbPan[i], target->flLateReverbPan[i] );
	}

	dest->iDecayHFLimit = target->iDecayHFLimit;
}

static qboolean S_AL_ApplyReverbToSlot( const ALuint effect, const ALuint slot, const EFXEAXREVERBPROPERTIES &properties )
{
	const auto clampf = []( const float value, const float minValue, const float maxValue ) {
		return Com_Clamp( minValue, maxValue, value );
	};
	const auto clampi = []( const int value, const int minValue, const int maxValue ) {
		return static_cast<ALint>( Com_Clamp( static_cast<float>( minValue ), static_cast<float>( maxValue ), static_cast<float>( value ) ) );
	};

	if ( !s_alEfxAvailable || !effect || !slot ) {
		return qfalse;
	}

	alGetError();

	if ( s_alEaxReverbAvailable ) {
		alEffecti( effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
		alEffectf( effect, AL_EAXREVERB_DENSITY, clampf( properties.flDensity, AL_EAXREVERB_MIN_DENSITY, AL_EAXREVERB_MAX_DENSITY ) );
		alEffectf( effect, AL_EAXREVERB_DIFFUSION, clampf( properties.flDiffusion, AL_EAXREVERB_MIN_DIFFUSION, AL_EAXREVERB_MAX_DIFFUSION ) );
		alEffectf( effect, AL_EAXREVERB_GAIN, clampf( properties.flGain, AL_EAXREVERB_MIN_GAIN, AL_EAXREVERB_MAX_GAIN ) );
		alEffectf( effect, AL_EAXREVERB_GAINHF, clampf( properties.flGainHF, AL_EAXREVERB_MIN_GAINHF, AL_EAXREVERB_MAX_GAINHF ) );
		alEffectf( effect, AL_EAXREVERB_GAINLF, clampf( properties.flGainLF, AL_EAXREVERB_MIN_GAINLF, AL_EAXREVERB_MAX_GAINLF ) );
		alEffectf( effect, AL_EAXREVERB_DECAY_TIME, clampf( properties.flDecayTime, AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME ) );
		alEffectf( effect, AL_EAXREVERB_DECAY_HFRATIO, clampf( properties.flDecayHFRatio, AL_EAXREVERB_MIN_DECAY_HFRATIO, AL_EAXREVERB_MAX_DECAY_HFRATIO ) );
		alEffectf( effect, AL_EAXREVERB_DECAY_LFRATIO, clampf( properties.flDecayLFRatio, AL_EAXREVERB_MIN_DECAY_LFRATIO, AL_EAXREVERB_MAX_DECAY_LFRATIO ) );
		alEffectf( effect, AL_EAXREVERB_REFLECTIONS_GAIN, clampf( properties.flReflectionsGain, AL_EAXREVERB_MIN_REFLECTIONS_GAIN, AL_EAXREVERB_MAX_REFLECTIONS_GAIN ) );
		alEffectf( effect, AL_EAXREVERB_REFLECTIONS_DELAY, clampf( properties.flReflectionsDelay, AL_EAXREVERB_MIN_REFLECTIONS_DELAY, AL_EAXREVERB_MAX_REFLECTIONS_DELAY ) );
		alEffectfv( effect, AL_EAXREVERB_REFLECTIONS_PAN, properties.flReflectionsPan );
		alEffectf( effect, AL_EAXREVERB_LATE_REVERB_GAIN, clampf( properties.flLateReverbGain, AL_EAXREVERB_MIN_LATE_REVERB_GAIN, AL_EAXREVERB_MAX_LATE_REVERB_GAIN ) );
		alEffectf( effect, AL_EAXREVERB_LATE_REVERB_DELAY, clampf( properties.flLateReverbDelay, AL_EAXREVERB_MIN_LATE_REVERB_DELAY, AL_EAXREVERB_MAX_LATE_REVERB_DELAY ) );
		alEffectfv( effect, AL_EAXREVERB_LATE_REVERB_PAN, properties.flLateReverbPan );
		alEffectf( effect, AL_EAXREVERB_ECHO_TIME, clampf( properties.flEchoTime, AL_EAXREVERB_MIN_ECHO_TIME, AL_EAXREVERB_MAX_ECHO_TIME ) );
		alEffectf( effect, AL_EAXREVERB_ECHO_DEPTH, clampf( properties.flEchoDepth, AL_EAXREVERB_MIN_ECHO_DEPTH, AL_EAXREVERB_MAX_ECHO_DEPTH ) );
		alEffectf( effect, AL_EAXREVERB_MODULATION_TIME, clampf( properties.flModulationTime, AL_EAXREVERB_MIN_MODULATION_TIME, AL_EAXREVERB_MAX_MODULATION_TIME ) );
		alEffectf( effect, AL_EAXREVERB_MODULATION_DEPTH, clampf( properties.flModulationDepth, AL_EAXREVERB_MIN_MODULATION_DEPTH, AL_EAXREVERB_MAX_MODULATION_DEPTH ) );
		alEffectf( effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, clampf( properties.flAirAbsorptionGainHF, AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF, AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF ) );
		alEffectf( effect, AL_EAXREVERB_HFREFERENCE, clampf( properties.flHFReference, AL_EAXREVERB_MIN_HFREFERENCE, AL_EAXREVERB_MAX_HFREFERENCE ) );
		alEffectf( effect, AL_EAXREVERB_LFREFERENCE, clampf( properties.flLFReference, AL_EAXREVERB_MIN_LFREFERENCE, AL_EAXREVERB_MAX_LFREFERENCE ) );
		alEffectf( effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, clampf( properties.flRoomRolloffFactor, AL_EAXREVERB_MIN_ROOM_ROLLOFF_FACTOR, AL_EAXREVERB_MAX_ROOM_ROLLOFF_FACTOR ) );
		alEffecti( effect, AL_EAXREVERB_DECAY_HFLIMIT, clampi( properties.iDecayHFLimit, AL_EAXREVERB_MIN_DECAY_HFLIMIT, AL_EAXREVERB_MAX_DECAY_HFLIMIT ) );
	} else {
		alEffecti( effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
		alEffectf( effect, AL_REVERB_DENSITY, clampf( properties.flDensity, AL_REVERB_MIN_DENSITY, AL_REVERB_MAX_DENSITY ) );
		alEffectf( effect, AL_REVERB_DIFFUSION, clampf( properties.flDiffusion, AL_REVERB_MIN_DIFFUSION, AL_REVERB_MAX_DIFFUSION ) );
		alEffectf( effect, AL_REVERB_GAIN, clampf( properties.flGain, AL_REVERB_MIN_GAIN, AL_REVERB_MAX_GAIN ) );
		alEffectf( effect, AL_REVERB_GAINHF, clampf( properties.flGainHF, AL_REVERB_MIN_GAINHF, AL_REVERB_MAX_GAINHF ) );
		alEffectf( effect, AL_REVERB_DECAY_TIME, clampf( properties.flDecayTime, AL_REVERB_MIN_DECAY_TIME, AL_REVERB_MAX_DECAY_TIME ) );
		alEffectf( effect, AL_REVERB_DECAY_HFRATIO, clampf( properties.flDecayHFRatio, AL_REVERB_MIN_DECAY_HFRATIO, AL_REVERB_MAX_DECAY_HFRATIO ) );
		alEffectf( effect, AL_REVERB_REFLECTIONS_GAIN, clampf( properties.flReflectionsGain, AL_REVERB_MIN_REFLECTIONS_GAIN, AL_REVERB_MAX_REFLECTIONS_GAIN ) );
		alEffectf( effect, AL_REVERB_REFLECTIONS_DELAY, clampf( properties.flReflectionsDelay, AL_REVERB_MIN_REFLECTIONS_DELAY, AL_REVERB_MAX_REFLECTIONS_DELAY ) );
		alEffectf( effect, AL_REVERB_LATE_REVERB_GAIN, clampf( properties.flLateReverbGain, AL_REVERB_MIN_LATE_REVERB_GAIN, AL_REVERB_MAX_LATE_REVERB_GAIN ) );
		alEffectf( effect, AL_REVERB_LATE_REVERB_DELAY, clampf( properties.flLateReverbDelay, AL_REVERB_MIN_LATE_REVERB_DELAY, AL_REVERB_MAX_LATE_REVERB_DELAY ) );
		alEffectf( effect, AL_REVERB_AIR_ABSORPTION_GAINHF, clampf( properties.flAirAbsorptionGainHF, AL_REVERB_MIN_AIR_ABSORPTION_GAINHF, AL_REVERB_MAX_AIR_ABSORPTION_GAINHF ) );
		alEffectf( effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, clampf( properties.flRoomRolloffFactor, AL_REVERB_MIN_ROOM_ROLLOFF_FACTOR, AL_REVERB_MAX_ROOM_ROLLOFF_FACTOR ) );
		alEffecti( effect, AL_REVERB_DECAY_HFLIMIT, clampi( properties.iDecayHFLimit, AL_REVERB_MIN_DECAY_HFLIMIT, AL_REVERB_MAX_DECAY_HFLIMIT ) );
	}

	alAuxiliaryEffectSloti( slot, AL_EFFECTSLOT_EFFECT, static_cast<ALint>( effect ) );
	return alGetError() == AL_NO_ERROR;
}

static void S_AL_ApplyReverb( const EFXEAXREVERBPROPERTIES &properties )
{
	(void)S_AL_ApplyReverbToSlot( s_reverbEffect, s_reverbSlot, properties );
}

static void S_AL_DestroyPresetReverbState( alPresetReverbState_t *state )
{
	if ( !state ) {
		return;
	}

	if ( state->slot ) {
		alDeleteAuxiliaryEffectSlots( 1, &state->slot );
		state->slot = 0;
	}
	if ( state->effect ) {
		alDeleteEffects( 1, &state->effect );
		state->effect = 0;
	}
	state->initialized = qfalse;
}

static qboolean S_AL_EnsurePresetReverbState( const int presetId )
{
	alPresetReverbState_t *state = &s_presetReverbs[S_AL_ClampPresetId( presetId )];

	if ( !s_alEfxAvailable ) {
		return qfalse;
	}

	if ( !state->initialized ) {
		alGenEffects( 1, &state->effect );
		alGenAuxiliaryEffectSlots( 1, &state->slot );
		if ( !state->effect || !state->slot ) {
			S_AL_DestroyPresetReverbState( state );
			return qfalse;
		}
		state->initialized = qtrue;
	}

	if ( !S_AL_ApplyReverbToSlot( state->effect, state->slot, S_AL_ScaleReverbPreset( presetId ) ) ) {
		S_AL_DestroyPresetReverbState( state );
		return qfalse;
	}

	return qtrue;
}

static void S_AL_RefreshPresetReverbStates( void )
{
	const float level = Com_Clamp( 0.0f, 2.0f, s_openalReverbLevel->value );

	if ( !s_alEfxAvailable || fabsf( level - s_presetReverbLevel ) < 0.001f ) {
		return;
	}

	for ( int i = 0; i < AL_REVERB_COUNT; ++i ) {
		if ( s_presetReverbs[i].initialized && !S_AL_EnsurePresetReverbState( i ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: Failed to refresh OpenAL reverb preset %s\n", S_AL_GetPreset( i )->name );
		}
	}

	s_presetReverbLevel = level;
}

static void S_AL_ResetChannelState( alChannel_t *channel )
{
	const ALuint source = channel->source;
	Com_Memset( channel, 0, sizeof( *channel ) );
	channel->source = source;
}

static void S_AL_StopChannel( alChannel_t *channel )
{
	if ( !channel->source ) {
		return;
	}

	alSourceStop( channel->source );
	alSourcei( channel->source, AL_BUFFER, AL_NONE );

	if ( s_alEfxAvailable ) {
		alSourcei( channel->source, AL_DIRECT_FILTER, 0 );
		alSource3i( channel->source, AL_AUXILIARY_SEND_FILTER, 0, 0, 0 );
	}

	S_AL_ResetChannelState( channel );
}

static alChannel_t *S_AL_FindLoopChannel( const int entnum )
{
	for ( int i = 0; i < s_alNumChannels; ++i ) {
		alChannel_t *channel = &s_alChannels[i];
		if ( channel->active && channel->looping && channel->entnum == entnum ) {
			return channel;
		}
	}

	return nullptr;
}

static alChannel_t *S_AL_AllocChannel( const int entnum, const int entchannel, const qboolean localSound )
{
	alChannel_t *oldest = nullptr;

	if ( entchannel != 0 ) {
		for ( int i = 0; i < s_alNumChannels; ++i ) {
			alChannel_t *channel = &s_alChannels[i];
			if ( !channel->active || channel->looping ) {
				continue;
			}

			if ( channel->entnum == entnum && channel->entchannel == entchannel ) {
				S_AL_StopChannel( channel );
				return channel;
			}
		}
	}

	for ( int i = 0; i < s_alNumChannels; ++i ) {
		alChannel_t *channel = &s_alChannels[i];
		if ( !channel->active ) {
			return channel;
		}

		if ( channel->localSound && !localSound ) {
			continue;
		}

		if ( !oldest || channel->allocTime < oldest->allocTime ) {
			oldest = channel;
		}
	}

	if ( oldest ) {
		S_AL_StopChannel( oldest );
	}

	return oldest;
}

static void S_AL_UpdateChannel( alChannel_t *channel )
{
	vec3_t origin;
	const qboolean localSound = channel->localSound || channel->entnum == s_listenerEntity;

	if ( !channel->active ) {
		return;
	}

	alSourcef( channel->source, AL_GAIN, channel->masterGain );

	if ( localSound ) {
		alSourcei( channel->source, AL_SOURCE_RELATIVE, AL_TRUE );
		alSourcef( channel->source, AL_ROLLOFF_FACTOR, 0.0f );
		alSource3f( channel->source, AL_POSITION, 0.0f, 0.0f, 0.0f );
		alSource3f( channel->source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
		if ( s_alSourceSpatializeAvailable ) {
			alSourcei( channel->source, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE );
		}
		S_AL_SetChannelFilters( channel, qfalse, s_openalReverb->integer ? s_reverbSlot : 0 );
		return;
	}

	if ( channel->fixedOrigin ) {
		VectorCopy( channel->origin, origin );
	} else {
		VectorCopy( s_entityOrigins[channel->entnum], origin );
	}

	alSourcei( channel->source, AL_SOURCE_RELATIVE, AL_FALSE );
	alSourcef( channel->source, AL_REFERENCE_DISTANCE, SOUND_FULLVOLUME );
	alSourcef( channel->source, AL_MAX_DISTANCE, SOUND_MAXDISTANCE );
	alSourcef( channel->source, AL_ROLLOFF_FACTOR, 1.0f );
	S_AL_ApplyPosition( channel->source, origin );
	S_AL_ApplyVelocity( channel->source, channel->velocity );

	if ( s_alSourceSpatializeAvailable ) {
		alSourcei( channel->source, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE );
	}

	S_AL_SetChannelFilters( channel, S_AL_IsOccluded( origin ), S_AL_GetSourceReverbSlot( origin ) );
}

static qboolean S_AL_StartChannelPlayback( alChannel_t *channel )
{
	alSfx_t *sfx = &s_knownSfx[channel->sfxHandle];

	if ( !sfx->buffer && !S_AL_LoadSfx( sfx, channel->sfxHandle ) ) {
		return qfalse;
	}

	alSourceStop( channel->source );
	alSourcei( channel->source, AL_BUFFER, static_cast<ALint>( sfx->buffer ) );
	alSourcei( channel->source, AL_LOOPING, channel->looping ? AL_TRUE : AL_FALSE );
	S_AL_UpdateChannel( channel );
	alSourcePlay( channel->source );

	if ( alGetError() != AL_NO_ERROR ) {
		S_AL_StopChannel( channel );
		return qfalse;
	}

	sfx->lastUsed = cls.realtime;
	return qtrue;
}

static void S_AL_StopAllSources( void )
{
	for ( int i = 0; i < s_alNumChannels; ++i ) {
		S_AL_StopChannel( &s_alChannels[i] );
	}
}

static void S_AL_ClearRawStream( void )
{
	if ( !s_rawSource ) {
		return;
	}

	alSourceStop( s_rawSource );

	while ( s_rawQueuedBuffers > 0 ) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers( s_rawSource, 1, &buffer );
		if ( buffer ) {
			alDeleteBuffers( 1, &buffer );
		}
		--s_rawQueuedBuffers;
	}
}

static void S_AL_ClearMusicQueue( void )
{
	if ( !s_music.source ) {
		return;
	}

	alSourceStop( s_music.source );

	while ( s_music.queuedBuffers > 0 ) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers( s_music.source, 1, &buffer );
		S_AL_MarkMusicBufferQueued( buffer, qfalse );
		--s_music.queuedBuffers;
	}

	Com_Memset( s_music.bufferQueued, 0, sizeof( s_music.bufferQueued ) );
}

static qboolean S_AL_StreamBackgroundBuffer( ALuint buffer )
{
	byte raw[STREAM_BUFFER_BYTES];
	int bytesRead = 0;
	ALenum format;
	int rate;

	while ( bytesRead < STREAM_BUFFER_BYTES ) {
		int read;

		if ( !s_music.stream ) {
			break;
		}

		read = S_CodecReadStream( s_music.stream, STREAM_BUFFER_BYTES - bytesRead, raw + bytesRead );
		if ( read > 0 ) {
			bytesRead += read;
			continue;
		}

		if ( !s_music.loopName[0] ) {
			break;
		}

		S_CodecCloseStream( s_music.stream );
		s_music.stream = S_CodecOpenStream( s_music.loopName );
		if ( !s_music.stream ) {
			s_music.loopName[0] = '\0';
			break;
		}
	}

	if ( bytesRead <= 0 || !s_music.stream ) {
		return qfalse;
	}

	format = S_AL_GetBufferFormat( s_music.stream->info.channels, s_music.stream->info.width );
	if ( format == AL_NONE ) {
		return qfalse;
	}
	rate = s_music.stream->info.rate;

	alBufferData( buffer, format, raw, bytesRead, rate );
	return alGetError() == AL_NO_ERROR;
}

static void S_AL_StopBackgroundTrack( void )
{
	if ( s_music.stream ) {
		S_CodecCloseStream( s_music.stream );
		s_music.stream = nullptr;
	}

	s_music.loopName[0] = '\0';
	S_AL_ClearMusicQueue();
}

static void S_AL_UpdateBackgroundTrack( void )
{
	ALint processed = 0;

	if ( !s_music.source ) {
		return;
	}

	alSourcef( s_music.source, AL_GAIN, s_musicVolume->value );
	alGetSourcei( s_music.source, AL_BUFFERS_PROCESSED, &processed );

	while ( processed-- > 0 ) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers( s_music.source, 1, &buffer );
		S_AL_MarkMusicBufferQueued( buffer, qfalse );
		if ( s_music.queuedBuffers > 0 ) {
			--s_music.queuedBuffers;
		}
	}

	for ( int i = 0; i < MAX_STREAM_BUFFERS && s_music.queuedBuffers < MAX_STREAM_BUFFERS; ++i ) {
		ALuint buffer = s_music.buffers[i];
		if ( s_music.bufferQueued[i] ) {
			continue;
		}
		if ( !S_AL_StreamBackgroundBuffer( buffer ) ) {
			break;
		}

		alSourceQueueBuffers( s_music.source, 1, &buffer );
		s_music.bufferQueued[i] = qtrue;
		++s_music.queuedBuffers;
	}

	if ( s_music.queuedBuffers > 0 ) {
		ALint state = AL_STOPPED;
		alGetSourcei( s_music.source, AL_SOURCE_STATE, &state );
		if ( state != AL_PLAYING ) {
			alSourcePlay( s_music.source );
		}
	}
}

static qboolean S_AL_IsZoneEntityClass( const char *classname )
{
	return !Q_stricmp( classname, "client_env_sound" ) ||
		!Q_stricmp( classname, "target_env_sound" ) ||
		!Q_stricmp( classname, "q3vibe_env_sound" );
}

static int S_AL_FindPresetIdByName( const char *name )
{
	if ( !Q_stricmp( name, "default" ) ) {
		return AL_REVERB_DEFAULT;
	}

	for ( int i = 0; i < AL_REVERB_COUNT; ++i ) {
		if ( !Q_stricmp( kReverbPresets[i].name, name ) ) {
			return i;
		}
	}

	return AL_REVERB_DEFAULT;
}

static void S_AL_ParseReverbZones( void )
{
	const char *data = CM_EntityString();
	int parsedCount = 0;

	s_numReverbZones = 0;
	Q_strncpyz( s_reverbMapName, cl.mapname, sizeof( s_reverbMapName ) );

	if ( !data || !data[0] ) {
		return;
	}

	COM_BeginParseSession( "OpenAL reverb zones" );

	while ( 1 ) {
		char classname[MAX_QPATH] = {};
		vec3_t origin = {};
		float radius = 250.0f;
		int presetId = AL_REVERB_DEFAULT;
		const char *token = COM_ParseExt( &data, qtrue );

		if ( !token[0] ) {
			break;
		}
		if ( Q_stricmp( token, "{" ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL zone parser found unexpected token '%s'\n", token );
			break;
		}

		while ( 1 ) {
			char key[MAX_QPATH];
			const char *value;

			token = COM_ParseExt( &data, qtrue );
			if ( !token[0] ) {
				data = "";
				break;
			}
			if ( !Q_stricmp( token, "}" ) ) {
				break;
			}

			Q_strncpyz( key, token, sizeof( key ) );
			value = COM_ParseExt( &data, qfalse );
			if ( !value[0] ) {
				break;
			}

			if ( !Q_stricmp( key, "classname" ) ) {
				Q_strncpyz( classname, value, sizeof( classname ) );
			} else if ( !Q_stricmp( key, "origin" ) ) {
				sscanf( value, "%f %f %f", &origin[0], &origin[1], &origin[2] );
			} else if ( !Q_stricmp( key, "radius" ) ) {
				radius = static_cast<float>( atof( value ) );
			} else if ( !Q_stricmp( key, "reverb_effect_id" ) ) {
				presetId = S_AL_ClampPresetId( atoi( value ) );
			} else if ( !Q_stricmp( key, "reverb_preset" ) ) {
				if ( isdigit( static_cast<unsigned char>( value[0] ) ) || value[0] == '-' ) {
					presetId = S_AL_ClampPresetId( atoi( value ) );
				} else {
					presetId = S_AL_FindPresetIdByName( value );
				}
			}
		}

		if ( !S_AL_IsZoneEntityClass( classname ) ) {
			continue;
		}

		if ( s_numReverbZones >= MAX_REVERB_ZONES ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL zone limit reached on %s\n", cl.mapname );
			break;
		}

		VectorCopy( origin, s_reverbZones[s_numReverbZones].origin );
		s_reverbZones[s_numReverbZones].radius = radius;
		s_reverbZones[s_numReverbZones].presetId = presetId;
		++s_numReverbZones;
		++parsedCount;
	}

	if ( s_openalZoneDebug->integer ) {
		Com_Printf( "OpenAL parsed %d reverb zones on %s\n", parsedCount, cl.mapname[0] ? cl.mapname : "<unknown>" );
	}
}

static int S_AL_ClassifyBspReverbPreset( const vec3_t extents, const qboolean openSky )
{
	const float horizontalMin = MIN( extents[0], extents[1] );
	const float horizontalMax = MAX( extents[0], extents[1] );
	const float average = ( extents[0] + extents[1] + extents[2] ) / 3.0f;

	if ( openSky && average > 2400.0f ) {
		return AL_REVERB_PLAIN;
	}

	if ( horizontalMin > 0.0f && horizontalMax / horizontalMin > 2.5f && average < 1400.0f ) {
		return AL_REVERB_HALLWAY;
	}

	if ( average < 240.0f ) {
		return AL_REVERB_BATHROOM;
	}
	if ( average < 480.0f ) {
		return AL_REVERB_ROOM;
	}
	if ( average < 900.0f ) {
		return AL_REVERB_STONE_ROOM;
	}
	if ( average < 1800.0f ) {
		return openSky ? AL_REVERB_CITY : AL_REVERB_AUDITORIUM;
	}

	return openSky ? AL_REVERB_PLAIN : AL_REVERB_HANGAR;
}

static void S_AL_UpdateBspReverbEstimate( void )
{
	vec3_t mins;
	vec3_t maxs;

	if ( Q_stricmp( s_bspReverbMapName, cl.mapname ) ) {
		Q_strncpyz( s_bspReverbMapName, cl.mapname, sizeof( s_bspReverbMapName ) );
		s_bspReverbProbeTime = 0;
		s_bspReverbAverage = REVERB_PROBE_DISTANCE;
		s_bspReverbPreset = AL_REVERB_DEFAULT;
		s_bspReverbOpenSky = qfalse;
	}

	if ( s_bspReverbProbeTime > cls.realtime ) {
		return;
	}

	if ( !S_AL_HasWorldCollisionModel() ) {
		s_bspReverbAverage = REVERB_PROBE_DISTANCE;
		s_bspReverbPreset = AL_REVERB_DEFAULT;
		s_bspReverbOpenSky = qfalse;
		return;
	}

	s_bspReverbProbeTime = cls.realtime + 50;
	ClearBounds( mins, maxs );
	s_bspReverbOpenSky = qfalse;

	for ( int i = 0; i < static_cast<int>( ARRAY_LEN( kReverbProbeDirections ) ); ++i ) {
		trace_t trace;
		vec3_t end;
		vec3_t hitDelta;

		VectorMA( s_listenerOrigin, REVERB_PROBE_DISTANCE, kReverbProbeDirections[i], end );
		CM_BoxTrace( &trace, s_listenerOrigin, end, vec3_origin, vec3_origin, 0, MASK_SOLID, qfalse );
		VectorSubtract( trace.endpos, s_listenerOrigin, hitDelta );

		if ( i == 1 && ( trace.surfaceFlags & SURF_SKY ) ) {
			hitDelta[2] += 4096.0f;
			s_bspReverbOpenSky = qtrue;
		}

		AddPointToBounds( hitDelta, mins, maxs );
	}

	vec3_t extents;
	VectorSubtract( maxs, mins, extents );
	s_bspReverbAverage = ( extents[0] + extents[1] + extents[2] ) / 3.0f;
	s_bspReverbPreset = S_AL_ClassifyBspReverbPreset( extents, s_bspReverbOpenSky );

	if ( s_openalZoneDebug->integer > 1 ) {
		Com_Printf(
			"OpenAL BSP reverb fallback: avg=%0.1f preset=%s sky=%s\n",
			s_bspReverbAverage,
			S_AL_GetPreset( s_bspReverbPreset )->name,
			s_bspReverbOpenSky ? "yes" : "no"
		);
	}
}

static int S_AL_SelectBspReverbPreset( void )
{
	S_AL_UpdateBspReverbEstimate();
	return s_bspReverbPreset;
}

static int S_AL_SelectZonePresetForPoint( const vec3_t point )
{
	int bestPreset = AL_REVERB_DEFAULT;
	float bestDistance = FLT_MAX;

	if ( !S_AL_HasWorldCollisionModel() ) {
		return bestPreset;
	}

	if ( Q_stricmp( s_reverbMapName, cl.mapname ) ) {
		S_AL_ParseReverbZones();
	}

	for ( int i = 0; i < s_numReverbZones; ++i ) {
		trace_t trace;
		const alReverbZone_t *zone = &s_reverbZones[i];
		const float distance = Distance( point, zone->origin );

		if ( distance > zone->radius || distance > bestDistance ) {
			continue;
		}

		CM_BoxTrace( &trace, zone->origin, point, vec3_origin, vec3_origin, 0, MASK_SOLID, qfalse );
		if ( trace.fraction < 1.0f ) {
			continue;
		}

		bestDistance = distance;
		bestPreset = zone->presetId;
	}

	return bestPreset;
}

static int S_AL_SelectReverbPreset( void )
{
	if ( s_openalReverbPreset->integer >= 0 ) {
		return S_AL_ClampPresetId( s_openalReverbPreset->integer );
	}

	if ( s_listenerInWater ) {
		return AL_REVERB_UNDERWATER;
	}

	int bestPreset = S_AL_SelectZonePresetForPoint( s_listenerOrigin );
	if ( bestPreset == AL_REVERB_DEFAULT ) {
		bestPreset = S_AL_SelectBspReverbPreset();
	}

	if ( s_openalZoneDebug->integer > 1 ) {
		Com_Printf( "OpenAL selected reverb preset %s\n", S_AL_GetPreset( bestPreset )->name );
	}

	return bestPreset;
}

static int S_AL_SelectSourceReverbPreset( const vec3_t origin )
{
	if ( s_openalReverbPreset->integer >= 0 ) {
		return S_AL_ClampPresetId( s_openalReverbPreset->integer );
	}

	const int sourcePreset = S_AL_SelectZonePresetForPoint( origin );
	return sourcePreset != AL_REVERB_DEFAULT ? sourcePreset : s_targetReverbPreset;
}

static ALuint S_AL_GetSourceReverbSlot( const vec3_t origin )
{
	const int presetId = S_AL_SelectSourceReverbPreset( origin );

	if ( !s_alEfxAvailable || !s_openalReverb->integer ) {
		return 0;
	}

	if ( presetId == s_targetReverbPreset ) {
		return s_reverbSlot;
	}

	if ( S_AL_EnsurePresetReverbState( presetId ) ) {
		return s_presetReverbs[presetId].slot;
	}

	return s_reverbSlot;
}

static void S_AL_RefreshReverbState( const int msec )
{
	const int selectedPreset = s_openalReverb->integer ? S_AL_SelectReverbPreset() : AL_REVERB_DEFAULT;

	if ( !s_alEfxAvailable ) {
		return;
	}

	if ( selectedPreset != s_targetReverbPreset ) {
		s_targetReverbPreset = selectedPreset;
	}
	s_targetReverb = S_AL_ScaleReverbPreset( selectedPreset );

	S_AL_LerpReverb( &s_currentReverb, &s_targetReverb, msec * REVERB_LERP_PER_MSEC );
	S_AL_ApplyReverb( s_currentReverb );
	S_AL_RefreshPresetReverbStates();
}

static void S_AL_UpdateLoopingSounds( void )
{
	for ( int i = 0; i < s_alNumChannels; ++i ) {
		if ( s_alChannels[i].active && s_alChannels[i].looping ) {
			s_alChannels[i].loopSeen = qfalse;
		}
	}

	for ( int i = 0; i < MAX_GENTITIES; ++i ) {
		alChannel_t *channel;
		alLoopSound_t *loopSound = &s_loopSounds[i];

		if ( !loopSound->active || loopSound->sfxHandle <= 0 || loopSound->sfxHandle >= s_numSfx ) {
			continue;
		}

		channel = S_AL_FindLoopChannel( i );
		if ( !channel ) {
			channel = S_AL_AllocChannel( i, CHAN_AUTO, qfalse );
			if ( !channel ) {
				continue;
			}

			S_AL_ResetChannelState( channel );
			channel->active = qtrue;
			channel->looping = qtrue;
			channel->entnum = i;
			channel->entchannel = CHAN_AUTO;
			channel->sfxHandle = loopSound->sfxHandle;
			channel->fixedOrigin = qtrue;
			channel->localSound = qfalse;
			channel->allocTime = cls.realtime;
		} else if ( channel->sfxHandle != loopSound->sfxHandle ) {
			S_AL_StopChannel( channel );
			channel->active = qtrue;
			channel->looping = qtrue;
			channel->entnum = i;
			channel->entchannel = CHAN_AUTO;
			channel->sfxHandle = loopSound->sfxHandle;
			channel->fixedOrigin = qtrue;
			channel->localSound = qfalse;
			channel->allocTime = cls.realtime;
		}

		VectorCopy( loopSound->origin, channel->origin );
		VectorCopy( loopSound->velocity, channel->velocity );
		channel->masterGain = loopSound->realLoop ? LEGACY_SPHERE_GAIN : LEGACY_MASTER_GAIN;
		channel->loopSeen = qtrue;

		ALint state = AL_STOPPED;
		alGetSourcei( channel->source, AL_SOURCE_STATE, &state );
		if ( state != AL_PLAYING ) {
			S_AL_StartChannelPlayback( channel );
		} else {
			S_AL_UpdateChannel( channel );
		}
	}

	for ( int i = 0; i < s_alNumChannels; ++i ) {
		alChannel_t *channel = &s_alChannels[i];
		if ( channel->active && channel->looping && !channel->loopSeen ) {
			S_AL_StopChannel( channel );
		}
	}
}

} // namespace

namespace {

static void S_AL_UpdateRawStream( void )
{
	ALint processed = 0;

	if ( !s_rawSource ) {
		return;
	}

	alGetSourcei( s_rawSource, AL_BUFFERS_PROCESSED, &processed );
	while ( processed-- > 0 ) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers( s_rawSource, 1, &buffer );
		if ( buffer ) {
			alDeleteBuffers( 1, &buffer );
		}
		if ( s_rawQueuedBuffers > 0 ) {
			--s_rawQueuedBuffers;
		}
	}
}

static void S_AL_Shutdown( void )
{
	S_AL_StopAllSources();
	S_AL_StopBackgroundTrack();
	S_AL_ClearRawStream();

	for ( int i = 1; i < s_numSfx; ++i ) {
		if ( s_knownSfx[i].buffer ) {
			alDeleteBuffers( 1, &s_knownSfx[i].buffer );
			s_knownSfx[i].buffer = 0;
		}
	}

	if ( s_rawSource ) {
		alDeleteSources( 1, &s_rawSource );
		s_rawSource = 0;
	}

	if ( s_music.source ) {
		alDeleteSources( 1, &s_music.source );
		s_music.source = 0;
	}

	if ( s_occlusionDirectFilter ) {
		alDeleteFilters( 1, &s_occlusionDirectFilter );
		s_occlusionDirectFilter = 0;
	}
	if ( s_occlusionSendFilter ) {
		alDeleteFilters( 1, &s_occlusionSendFilter );
		s_occlusionSendFilter = 0;
	}
	if ( s_reverbSlot ) {
		alDeleteAuxiliaryEffectSlots( 1, &s_reverbSlot );
		s_reverbSlot = 0;
	}
	if ( s_reverbEffect ) {
		alDeleteEffects( 1, &s_reverbEffect );
		s_reverbEffect = 0;
	}
	for ( int i = 0; i < AL_REVERB_COUNT; ++i ) {
		S_AL_DestroyPresetReverbState( &s_presetReverbs[i] );
	}

	for ( int i = 0; i < s_alNumChannels; ++i ) {
		if ( s_alChannels[i].source ) {
			alDeleteSources( 1, &s_alChannels[i].source );
			s_alChannels[i].source = 0;
		}
	}

	if ( s_alContext ) {
		alcMakeContextCurrent( nullptr );
		alcDestroyContext( s_alContext );
		s_alContext = nullptr;
	}
	if ( s_alDevice ) {
		alcCloseDevice( s_alDevice );
		s_alDevice = nullptr;
	}

	Com_Memset( &s_music, 0, sizeof( s_music ) );
	Com_Memset( s_alChannels, 0, sizeof( s_alChannels ) );
	Com_Memset( s_loopSounds, 0, sizeof( s_loopSounds ) );
	Com_Memset( s_entityOrigins, 0, sizeof( s_entityOrigins ) );
	Com_Memset( s_knownSfx, 0, sizeof( s_knownSfx ) );
	Com_Memset( s_sfxHash, 0, sizeof( s_sfxHash ) );

	s_numSfx = 0;
	s_alNumChannels = 0;
	s_rawQueuedBuffers = 0;
	s_numReverbZones = 0;
	s_bspReverbMapName[0] = '\0';
	s_bspReverbProbeTime = 0;
	s_bspReverbAverage = REVERB_PROBE_DISTANCE;
	s_bspReverbPreset = AL_REVERB_DEFAULT;
	s_bspReverbOpenSky = qfalse;
	s_presetReverbLevel = -1.0f;
	s_alEfxAvailable = qfalse;
	s_alEaxReverbAvailable = qfalse;
	s_alHrtfAvailable = qfalse;
	s_alSourceSpatializeAvailable = qfalse;
	s_soundStarted = qfalse;
	s_soundMuted = qfalse;
	cls.soundRegistered = qfalse;

	S_AL_PublishCapabilities();
}

static void S_AL_StartSound( const vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfxHandle )
{
	alChannel_t *channel;

	if ( !s_soundStarted || s_soundMuted ) {
		return;
	}

	if ( sfxHandle <= 0 || sfxHandle >= s_numSfx ) {
		return;
	}

	if ( !origin && ( entityNum < 0 || entityNum >= MAX_GENTITIES ) ) {
		Com_Printf( S_COLOR_YELLOW "S_AL_StartSound: bad entity number %d\n", entityNum );
		return;
	}

	for ( int i = 0; i < s_alNumChannels; ++i ) {
		if ( !s_alChannels[i].active || s_alChannels[i].looping ) {
			continue;
		}
		if ( s_alChannels[i].entnum == entityNum && s_alChannels[i].sfxHandle == sfxHandle &&
			cls.realtime - s_alChannels[i].allocTime < 20 ) {
			return;
		}
	}

	channel = S_AL_AllocChannel( entityNum, entchannel, qfalse );
	if ( !channel ) {
		return;
	}

	S_AL_ResetChannelState( channel );
	channel->active = qtrue;
	channel->entnum = entityNum;
	channel->entchannel = entchannel;
	channel->sfxHandle = sfxHandle;
	channel->allocTime = cls.realtime;
	channel->masterGain = LEGACY_MASTER_GAIN;
	channel->localSound = qfalse;
	channel->looping = qfalse;
	channel->fixedOrigin = origin != nullptr;

	if ( origin ) {
		VectorCopy( origin, channel->origin );
	} else if ( entityNum >= 0 && entityNum < MAX_GENTITIES ) {
		VectorCopy( s_entityOrigins[entityNum], channel->origin );
	}

	VectorClear( channel->velocity );
	S_AL_StartChannelPlayback( channel );
}

static void S_AL_StartLocalSound( sfxHandle_t sfxHandle, int channelNum )
{
	alChannel_t *channel;

	if ( !s_soundStarted || s_soundMuted || sfxHandle <= 0 || sfxHandle >= s_numSfx ) {
		return;
	}

	channel = S_AL_AllocChannel( s_listenerEntity, channelNum, qtrue );
	if ( !channel ) {
		return;
	}

	S_AL_ResetChannelState( channel );
	channel->active = qtrue;
	channel->entnum = s_listenerEntity;
	channel->entchannel = channelNum;
	channel->sfxHandle = sfxHandle;
	channel->allocTime = cls.realtime;
	channel->masterGain = LEGACY_MASTER_GAIN;
	channel->localSound = qtrue;
	channel->looping = qfalse;
	channel->fixedOrigin = qfalse;
	VectorClear( channel->velocity );
	S_AL_StartChannelPlayback( channel );
}

static void S_AL_StartBackgroundTrack( const char *intro, const char *loop )
{
	if ( !intro || !intro[0] ) {
		S_AL_StopBackgroundTrack();
		return;
	}

	S_AL_StopBackgroundTrack();
	s_music.stream = S_CodecOpenStream( intro );
	if ( !s_music.stream ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: couldn't open music file %s\n", intro );
		return;
	}

	Q_strncpyz( s_music.loopName, ( loop && loop[0] ) ? loop : intro, sizeof( s_music.loopName ) );
}

static void S_AL_RawSamples( int samples, int rate, int width, int channels, const byte *data, float volume )
{
	ALuint buffer = 0;
	ALenum format;
	ALint state = AL_STOPPED;

	if ( !s_soundStarted || s_soundMuted || !s_rawSource || !data || samples <= 0 ) {
		return;
	}

	S_AL_UpdateRawStream();
	if ( s_rawQueuedBuffers >= 16 ) {
		return;
	}

	format = S_AL_GetBufferFormat( channels, width );
	if ( format == AL_NONE ) {
		return;
	}

	alGenBuffers( 1, &buffer );
	alBufferData( buffer, format, data, samples * width * channels, rate );
	alSourceQueueBuffers( s_rawSource, 1, &buffer );
	++s_rawQueuedBuffers;

	alSourcef( s_rawSource, AL_GAIN, volume );
	alGetSourcei( s_rawSource, AL_SOURCE_STATE, &state );
	if ( state != AL_PLAYING ) {
		alSourcePlay( s_rawSource );
	}
}

static void S_AL_StopAllSounds( void )
{
	S_AL_StopAllSources();
	S_AL_StopBackgroundTrack();
	S_AL_ClearRawStream();
}

static void S_AL_ClearLoopingSounds( qboolean killall )
{
	for ( int i = 0; i < MAX_GENTITIES; ++i ) {
		if ( killall || s_loopSounds[i].kill ) {
			s_loopSounds[i].active = qfalse;
		}
	}
}

static void S_AL_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle )
{
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES || sfxHandle <= 0 || sfxHandle >= s_numSfx ) {
		return;
	}

	VectorCopy( origin, s_loopSounds[entityNum].origin );
	VectorCopy( velocity, s_loopSounds[entityNum].velocity );
	s_loopSounds[entityNum].sfxHandle = sfxHandle;
	s_loopSounds[entityNum].active = qtrue;
	s_loopSounds[entityNum].kill = qtrue;
	s_loopSounds[entityNum].realLoop = qfalse;
}

static void S_AL_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle )
{
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES || sfxHandle <= 0 || sfxHandle >= s_numSfx ) {
		return;
	}

	VectorCopy( origin, s_loopSounds[entityNum].origin );
	VectorCopy( velocity, s_loopSounds[entityNum].velocity );
	s_loopSounds[entityNum].sfxHandle = sfxHandle;
	s_loopSounds[entityNum].active = qtrue;
	s_loopSounds[entityNum].kill = qfalse;
	s_loopSounds[entityNum].realLoop = qtrue;
}

static void S_AL_StopLoopingSound( int entityNum )
{
	alChannel_t *channel;

	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return;
	}

	s_loopSounds[entityNum].active = qfalse;
	channel = S_AL_FindLoopChannel( entityNum );
	if ( channel ) {
		S_AL_StopChannel( channel );
	}
}

static void S_AL_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater )
{
	s_listenerEntity = entityNum;
	s_listenerInWater = inwater;
	VectorCopy( origin, s_listenerOrigin );
	VectorCopy( axis[0], s_listenerAxis[0] );
	VectorCopy( axis[1], s_listenerAxis[1] );
	VectorCopy( axis[2], s_listenerAxis[2] );
}

static void S_AL_UpdateEntityPosition( int entityNum, const vec3_t origin )
{
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return;
	}

	VectorCopy( origin, s_entityOrigins[entityNum] );
}

static void S_AL_Update( int msec )
{
	ALfloat position[3];
	ALfloat orientation[6];

	if ( !s_soundStarted ) {
		return;
	}

	S_AL_CopyVectorToOpenAL( s_listenerOrigin, position );
	S_AL_CopyVectorToOpenAL( s_listenerAxis[0], orientation );
	S_AL_CopyVectorToOpenAL( s_listenerAxis[2], orientation + 3 );

	alListener3f( AL_POSITION, position[0], position[1], position[2] );
	alListenerfv( AL_ORIENTATION, orientation );
	alListenerf( AL_GAIN, S_AL_IsMuted() ? 0.0f : s_volume->value );
	alDistanceModel( AL_LINEAR_DISTANCE_CLAMPED );
	alDopplerFactor( s_doppler->integer ? 1.0f : 0.0f );

	S_AL_UpdateOcclusionFilters();
	S_AL_RefreshReverbState( msec );
	S_AL_UpdateBackgroundTrack();
	S_AL_UpdateRawStream();

	for ( int i = 0; i < s_alNumChannels; ++i ) {
		ALint state = AL_STOPPED;
		alChannel_t *channel = &s_alChannels[i];

		if ( !channel->active || channel->looping ) {
			continue;
		}

		alGetSourcei( channel->source, AL_SOURCE_STATE, &state );
		if ( state != AL_PLAYING ) {
			S_AL_StopChannel( channel );
			continue;
		}

		S_AL_UpdateChannel( channel );
	}

	S_AL_UpdateLoopingSounds();
}

static void S_AL_DisableSounds( void )
{
	s_soundMuted = qtrue;
	S_AL_StopAllSources();
	S_AL_ClearRawStream();
}

static void S_AL_BeginRegistration( void )
{
	s_soundMuted = qfalse;

	if ( s_numSfx == 0 ) {
		Com_Memset( s_knownSfx, 0, sizeof( s_knownSfx ) );
		Com_Memset( s_sfxHash, 0, sizeof( s_sfxHash ) );
		s_numSfx = 1;
		S_AL_RegisterSound( "sound/feedback/hit.wav", qfalse );
	}
}

static sfxHandle_t S_AL_RegisterSound( const char *sample, qboolean compressed )
{
	alSfx_t *sfx;
	(void)compressed;

	if ( !sample || !sample[0] ) {
		return 0;
	}
	if ( strlen( sample ) >= MAX_QPATH ) {
		return 0;
	}

	sfx = S_AL_FindSfx( sample );
	if ( !sfx ) {
		return 0;
	}

	if ( !sfx->buffer && !sfx->failed ) {
		if ( !S_AL_LoadSfx( sfx, static_cast<int>( sfx - s_knownSfx ) ) ) {
			return 0;
		}
	}

	return static_cast<sfxHandle_t>( sfx - s_knownSfx );
}

static void S_AL_ClearSoundBuffer( void )
{
	S_AL_StopAllSources();
	S_AL_ClearRawStream();
	Com_Memset( s_loopSounds, 0, sizeof( s_loopSounds ) );
}

static void S_AL_SoundInfo( void )
{
	Com_Printf( "----- OpenAL Info -----\n" );
	Com_Printf( "AL_VENDOR: %s\n", alGetString( AL_VENDOR ) );
	Com_Printf( "AL_RENDERER: %s\n", alGetString( AL_RENDERER ) );
	Com_Printf( "AL_VERSION: %s\n", alGetString( AL_VERSION ) );
	Com_Printf( "OpenAL sources: %d\n", s_alNumChannels );
	Com_Printf( "HRTF available: %s\n", s_alHrtfAvailable ? "yes" : "no" );
	Com_Printf( "EFX available: %s\n", s_alEfxAvailable ? "yes" : "no" );
	Com_Printf( "Current reverb: %s\n", S_AL_GetPreset( s_targetReverbPreset )->name );
	Com_Printf( "Reverb zones: %d\n", s_numReverbZones );
	Com_Printf( "BSP reverb avg: %.1f (%s)\n", s_bspReverbAverage, S_AL_GetPreset( s_bspReverbPreset )->name );
	Com_Printf( "-----------------------\n" );
}

static void S_AL_SoundList( void )
{
	int totalBytes = 0;

	for ( int i = 1; i < s_numSfx; ++i ) {
		if ( !s_knownSfx[i].name[0] ) {
			continue;
		}
		Com_Printf( "%6d : %s\n", s_knownSfx[i].bufferBytes, s_knownSfx[i].name );
		totalBytes += s_knownSfx[i].bufferBytes;
	}

	Com_Printf( "Total OpenAL sample memory: %d bytes\n", totalBytes );
}

} // namespace

qboolean S_AL_Init( soundInterface_t *si )
{
	ALCint contextAttributes[5];
	ALCint hrtfStatus = 0;
	ALCint maxAuxiliarySends = 0;
	const char *deviceName;

	if ( !si ) {
		return qfalse;
	}

	cvar_t *const khz = Cvar_Get( "s_khz", "22", CVAR_ARCHIVE_ND | CVAR_LATCH );
	s_openalHrtf = Cvar_Get( "s_openalHrtf", "1", CVAR_ARCHIVE | CVAR_LATCH );
	s_openalHrtfAvailable = Cvar_Get( "s_openalHrtfAvailable", "0", CVAR_ROM );
	s_openalReverb = Cvar_Get( "s_openalReverb", "1", CVAR_ARCHIVE );
	s_openalEfxAvailable = Cvar_Get( "s_openalEfxAvailable", "0", CVAR_ROM );
	s_openalEaxAvailable = Cvar_Get( "s_openalEaxAvailable", "0", CVAR_ROM );
	s_openalReverbPreset = Cvar_Get( "s_openalReverbPreset", "-1", CVAR_ARCHIVE );
	s_openalReverbLevel = Cvar_Get( "s_openalReverbLevel", "1.0", CVAR_ARCHIVE );
	s_openalOcclusion = Cvar_Get( "s_openalOcclusion", "1", CVAR_ARCHIVE );
	s_openalOcclusionStrength = Cvar_Get( "s_openalOcclusionStrength", "0.7", CVAR_ARCHIVE );
	s_openalZoneDebug = Cvar_Get( "s_openalZoneDebug", "0", CVAR_CHEAT );

	Cvar_CheckRange( khz, "0", "48", CV_INTEGER );
	Cvar_CheckRange( s_openalHrtf, "0", "1", CV_INTEGER );
	Cvar_CheckRange( s_openalReverb, "0", "1", CV_INTEGER );
	Cvar_CheckRange( s_openalOcclusion, "0", "1", CV_INTEGER );
	Cvar_CheckRange( s_openalReverbLevel, "0", "2", CV_FLOAT );
	Cvar_CheckRange( s_openalOcclusionStrength, "0", "1", CV_FLOAT );

	Cvar_SetDescription( khz, "Specifies the sound sampling rate, (8, 11, 22, 44, 48) in kHz. Default value is 22." );
	Cvar_SetDescription( s_openalHrtf, "Enables OpenAL Soft HRTF headphone virtualization." );
	Cvar_SetDescription( s_openalReverb, "Enables EFX/EAX environmental reverb when using the OpenAL backend." );
	Cvar_SetDescription( s_openalReverbPreset, "Selects the OpenAL reverb preset. -1 uses automatic zone selection." );
	Cvar_SetDescription( s_openalReverbLevel, "Scales the amount of OpenAL environmental reverb." );
	Cvar_SetDescription( s_openalOcclusion, "Enables wall occlusion filtering for OpenAL spatial audio." );
	Cvar_SetDescription( s_openalOcclusionStrength, "Controls how aggressively occluded sounds are filtered." );
	Cvar_SetDescription( s_openalZoneDebug, "Prints OpenAL reverb zone parsing and preset-selection diagnostics." );

	s_alEfxAvailable = qfalse;
	s_alEaxReverbAvailable = qfalse;
	s_alHrtfAvailable = qfalse;
	s_alSourceSpatializeAvailable = qfalse;
	S_AL_PublishCapabilities();

	s_alDevice = alcOpenDevice( nullptr );
	if ( !s_alDevice ) {
		return qfalse;
	}

	s_alHrtfAvailable = alcIsExtensionPresent( s_alDevice, "ALC_SOFT_HRTF" ) ? qtrue : qfalse;
	contextAttributes[0] = ALC_MAX_AUXILIARY_SENDS;
	contextAttributes[1] = 1;
	contextAttributes[2] = s_alHrtfAvailable ? ALC_HRTF_SOFT : 0;
	contextAttributes[3] = s_alHrtfAvailable ? ( s_openalHrtf->integer ? ALC_TRUE : ALC_FALSE ) : 0;
	contextAttributes[4] = 0;

	s_alContext = alcCreateContext( s_alDevice, contextAttributes[2] ? contextAttributes : nullptr );
	if ( !s_alContext ) {
		alcCloseDevice( s_alDevice );
		s_alDevice = nullptr;
		return qfalse;
	}
	alcMakeContextCurrent( s_alContext );

	s_alEfxAvailable = alcIsExtensionPresent( s_alDevice, "ALC_EXT_EFX" ) ? qtrue : qfalse;
	s_alSourceSpatializeAvailable = alIsExtensionPresent( "AL_SOFT_source_spatialize" ) ? qtrue : qfalse;
	if ( s_alEfxAvailable ) {
		alcGetIntegerv( s_alDevice, ALC_MAX_AUXILIARY_SENDS, 1, &maxAuxiliarySends );
		if ( maxAuxiliarySends < 1 ) {
			s_alEfxAvailable = qfalse;
		}
	}

	if ( s_alHrtfAvailable ) {
		alcGetIntegerv( s_alDevice, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus );
	}

	for ( int i = 0; i < MAX_CHANNELS; ++i ) {
		alGenSources( 1, &s_alChannels[i].source );
		if ( alGetError() != AL_NO_ERROR ) {
			break;
		}
		++s_alNumChannels;
	}

	if ( s_alNumChannels < 16 ) {
		S_AL_Shutdown();
		return qfalse;
	}

	alGenSources( 1, &s_music.source );
	alGenSources( 1, &s_rawSource );
	for ( int i = 0; i < MAX_STREAM_BUFFERS; ++i ) {
		alGenBuffers( 1, &s_music.buffers[i] );
	}

	alSourcei( s_music.source, AL_SOURCE_RELATIVE, AL_TRUE );
	alSourcei( s_rawSource, AL_SOURCE_RELATIVE, AL_TRUE );
	if ( s_alSourceSpatializeAvailable ) {
		alSourcei( s_music.source, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE );
		alSourcei( s_rawSource, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE );
	}
	alSource3f( s_music.source, AL_POSITION, 0.0f, 0.0f, 0.0f );
	alSource3f( s_rawSource, AL_POSITION, 0.0f, 0.0f, 0.0f );
	alSource3f( s_music.source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	alSource3f( s_rawSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	alSourcef( s_music.source, AL_ROLLOFF_FACTOR, 0.0f );
	alSourcef( s_rawSource, AL_ROLLOFF_FACTOR, 0.0f );
	if ( s_alEfxAvailable ) {
		alSourcei( s_music.source, AL_DIRECT_FILTER, 0 );
		alSourcei( s_rawSource, AL_DIRECT_FILTER, 0 );
		alSource3i( s_music.source, AL_AUXILIARY_SEND_FILTER, 0, 0, 0 );
		alSource3i( s_rawSource, AL_AUXILIARY_SEND_FILTER, 0, 0, 0 );
	}

	if ( s_alEfxAvailable ) {
		alGetError();
		alGenEffects( 1, &s_reverbEffect );
		alEffecti( s_reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
		s_alEaxReverbAvailable = alGetError() == AL_NO_ERROR ? qtrue : qfalse;
		if ( !s_alEaxReverbAvailable ) {
			alEffecti( s_reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
		}

		alGenAuxiliaryEffectSlots( 1, &s_reverbSlot );
		alGenFilters( 1, &s_occlusionDirectFilter );
		alGenFilters( 1, &s_occlusionSendFilter );
		S_AL_UpdateOcclusionFilters();

		s_currentReverb = kDefaultReverb;
		s_targetReverb = kDefaultReverb;
		S_AL_ApplyReverb( s_currentReverb );

		if ( alGetError() != AL_NO_ERROR || !s_reverbEffect || !s_reverbSlot || !s_occlusionDirectFilter || !s_occlusionSendFilter ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL EFX initialization failed, disabling environmental reverb.\n" );
			if ( s_occlusionDirectFilter ) {
				alDeleteFilters( 1, &s_occlusionDirectFilter );
				s_occlusionDirectFilter = 0;
			}
			if ( s_occlusionSendFilter ) {
				alDeleteFilters( 1, &s_occlusionSendFilter );
				s_occlusionSendFilter = 0;
			}
			if ( s_reverbSlot ) {
				alDeleteAuxiliaryEffectSlots( 1, &s_reverbSlot );
				s_reverbSlot = 0;
			}
			if ( s_reverbEffect ) {
				alDeleteEffects( 1, &s_reverbEffect );
				s_reverbEffect = 0;
			}
			s_alEfxAvailable = qfalse;
			s_alEaxReverbAvailable = qfalse;
		}
	}

	S_AL_PublishCapabilities();

	VectorSet( s_listenerAxis[0], 1.0f, 0.0f, 0.0f );
	VectorSet( s_listenerAxis[1], 0.0f, 1.0f, 0.0f );
	VectorSet( s_listenerAxis[2], 0.0f, 0.0f, 1.0f );

	deviceName = alcGetString( s_alDevice, ALC_DEVICE_SPECIFIER );
	Com_Printf(
		"OpenAL initialized: %s (%d sources, HRTF %s, EFX %s)\n",
		deviceName ? deviceName : "unknown device",
		s_alNumChannels,
		hrtfStatus == ALC_HRTF_ENABLED_SOFT ? "enabled" : "disabled",
		s_alEfxAvailable ? "enabled" : "disabled"
	);

	s_soundStarted = qtrue;
	s_soundMuted = qtrue;
	s_targetReverbPreset = AL_REVERB_DEFAULT;

	si->Shutdown = S_AL_Shutdown;
	si->StartSound = S_AL_StartSound;
	si->StartLocalSound = S_AL_StartLocalSound;
	si->StartBackgroundTrack = S_AL_StartBackgroundTrack;
	si->StopBackgroundTrack = S_AL_StopBackgroundTrack;
	si->RawSamples = S_AL_RawSamples;
	si->StopAllSounds = S_AL_StopAllSounds;
	si->ClearLoopingSounds = S_AL_ClearLoopingSounds;
	si->AddLoopingSound = S_AL_AddLoopingSound;
	si->AddRealLoopingSound = S_AL_AddRealLoopingSound;
	si->StopLoopingSound = S_AL_StopLoopingSound;
	si->Respatialize = S_AL_Respatialize;
	si->UpdateEntityPosition = S_AL_UpdateEntityPosition;
	si->Update = S_AL_Update;
	si->DisableSounds = S_AL_DisableSounds;
	si->BeginRegistration = S_AL_BeginRegistration;
	si->RegisterSound = S_AL_RegisterSound;
	si->ClearSoundBuffer = S_AL_ClearSoundBuffer;
	si->SoundInfo = S_AL_SoundInfo;
	si->SoundList = S_AL_SoundList;

	return qtrue;
}

#else

qboolean S_AL_Init( soundInterface_t *si )
{
	(void)si;
	return qfalse;
}

#endif
