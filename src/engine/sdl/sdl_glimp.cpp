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
#ifdef USE_VULKAN_API
#	include "SDL3/SDL_vulkan.h"
#endif
#else
#	include <SDL3/SDL.h>
#ifdef USE_VULKAN_API
#	include <SDL3/SDL_vulkan.h>
#endif
#endif

#include "../client/client.h"
#include "../renderercommon/tr_public.h"
#include "sdl_glw.h"
#include "sdl_icon.h"

typedef enum {
	RSERR_OK,
	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,
	RSERR_FATAL_ERROR,
	RSERR_UNKNOWN
} rserr_t;

template<typename Ret, typename... Args>
static void *FunctionPointerToVoidPtr( Ret ( *ptr )( Args... ) ) noexcept
{
	return reinterpret_cast<void *>( reinterpret_cast<intptr_t>( ptr ) );
}

glwstate_t glw_state;

SDL_Window *SDL_window = NULL;
static SDL_GLContext SDL_glContext = NULL;
#ifdef USE_VULKAN_API
static PFN_vkGetInstanceProcAddr qvkGetInstanceProcAddr;
#endif

cvar_t *r_stereoEnabled;
cvar_t *in_nograb;

typedef struct
{
	byte *data;
	size_t size;
} sdlClipboardBitmap_t;

static const char *s_clipboardBitmapMimeTypes[] = {
	"image/bmp"
};

static const void *GLW_ClipboardBitmapData( void *userdata, const char *mime_type, size_t *size )
{
	sdlClipboardBitmap_t *clipboardData = (sdlClipboardBitmap_t *)userdata;

	if ( !clipboardData || SDL_strcmp( mime_type, "image/bmp" ) != 0 ) {
		*size = 0;
		return NULL;
	}

	*size = clipboardData->size;
	return clipboardData->data;
}

static void GLW_ClipboardBitmapCleanup( void *userdata )
{
	sdlClipboardBitmap_t *clipboardData = (sdlClipboardBitmap_t *)userdata;

	if ( !clipboardData )
		return;

	SDL_free( clipboardData->data );
	SDL_free( clipboardData );
}

static void GLW_SetCursorVisible( qboolean visible )
{
	if ( visible )
		SDL_ShowCursor();
	else
		SDL_HideCursor();
}

static qboolean GLW_UseBorderlessWindow( qboolean fullscreen )
{
	return !fullscreen
		&& ( ( r_borderless && r_borderless->integer ) || ( r_noborder && r_noborder->integer ) );
}

static SDL_Window *GLW_CreateWindow( const char *title, int x, int y, int w, int h, SDL_WindowFlags flags )
{
	SDL_PropertiesID props;
	SDL_Window *window;

	props = SDL_CreateProperties();
	if ( !props )
		return NULL;

	SDL_SetStringProperty( props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, flags );

	window = SDL_CreateWindowWithProperties( props );
	SDL_DestroyProperties( props );

	return window;
}

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( qboolean unloadDLL )
{
	const char* drv = SDL_GetCurrentVideoDriver();

	IN_Shutdown();

	if ( glw_state.isFullscreen ) {
		if ( drv && strcmp( drv, "x11" ) == 0 ) {
			SDL_WarpMouseGlobal( glw_state.desktop_width / 2, glw_state.desktop_height / 2 );
		} else {
			GLW_SetCursorVisible( qtrue );
		}
	}

	if ( SDL_window ) {
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	if ( unloadDLL )
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
}


/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize( void )
{
	SDL_MinimizeWindow( SDL_window );
}


/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( const char *comment )
{
}


static SDL_DisplayID FindNearestDisplay( int *x, int *y, int w, int h )
{
	const int cx = *x + w / 2;
	const int cy = *y + h / 2;
	int i, index, numDisplays;
	SDL_DisplayID chosen;
	SDL_DisplayID *displays;
	SDL_Rect *list, *m;

	index = -1; // selected display index
	chosen = 0;

	displays = SDL_GetDisplays( &numDisplays );
	if ( displays == NULL || numDisplays <= 0 )
		return 0;

	glw_state.monitorCount = numDisplays;

	list = Z_Malloc( numDisplays * sizeof( list[0] ) );

	for ( i = 0; i < numDisplays; i++ )
	{
		if ( !SDL_GetDisplayBounds( displays[i], list + i ) )
		{
			Com_DPrintf( "SDL_GetDisplayBounds() failed: %s\n", SDL_GetError() );
			list[i].x = 0;
			list[i].y = 0;
			list[i].w = 0;
			list[i].h = 0;
		}
		//Com_Printf( "[%i]: x=%i, y=%i, w=%i, h=%i\n", i, list[i].x, list[i].y, list[i].w, list[i].h );
	}

	// select display by window center intersection
	for ( i = 0; i < numDisplays; i++ )
	{
		m = list + i;
		if ( cx >= m->x && cx < (m->x + m->w) && cy >= m->y && cy < (m->y + m->h) )
		{
			index = i;
			break;
		}
	}

	// select display by nearest distance between window center and display center
	if ( index == -1 )
	{
		unsigned long nearest, dist;
		int dx, dy;
		nearest = ~0UL;
		for ( i = 0; i < numDisplays; i++ )
		{
			m = list + i;
			dx = (m->x + m->w/2) - cx;
			dy = (m->y + m->h/2) - cy;
			dist = ( dx * dx ) + ( dy * dy );
			if ( dist < nearest )
			{
				nearest = dist;
				index = i;
			}
		}
	}

	// adjust x and y coordinates if needed
	if ( index >= 0 )
	{
		m = list + index;
		chosen = displays[index];
		if ( *x < m->x )
			*x = m->x;

		if ( *y < m->y )
			*y = m->y;
	}

	Z_Free( list );
	SDL_free( displays );

	return chosen;
}


static SDL_HitTestResult SDL_HitTestFunc( SDL_Window *win, const SDL_Point *area, void *data )
{
	int width, height;
	int borderSize, cornerSize;
	float displayScale;

	if ( !GLW_UseBorderlessWindow( qfalse ) )
		return SDL_HITTEST_NORMAL;

	if ( !SDL_GetWindowSize( win, &width, &height ) )
		return keys[ K_ALT ].down ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;

	displayScale = SDL_GetWindowDisplayScale( win );
	if ( displayScale <= 0.0f )
		displayScale = 1.0f;

	borderSize = MAX( 8, (int)( 8.0f * displayScale ) );
	cornerSize = borderSize * 2;

	if ( area->x < borderSize )
	{
		if ( area->y < cornerSize )
			return SDL_HITTEST_RESIZE_TOPLEFT;
		if ( area->y >= ( height - cornerSize ) )
			return SDL_HITTEST_RESIZE_BOTTOMLEFT;
		return SDL_HITTEST_RESIZE_LEFT;
	}

	if ( area->x >= ( width - borderSize ) )
	{
		if ( area->y < cornerSize )
			return SDL_HITTEST_RESIZE_TOPRIGHT;
		if ( area->y >= ( height - cornerSize ) )
			return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		return SDL_HITTEST_RESIZE_RIGHT;
	}

	if ( area->y < borderSize )
		return SDL_HITTEST_RESIZE_TOP;

	if ( area->y >= ( height - borderSize ) )
		return SDL_HITTEST_RESIZE_BOTTOM;

	if ( keys[ K_ALT ].down )
		return SDL_HITTEST_DRAGGABLE;

	return SDL_HITTEST_NORMAL;
}


/*
===============
GLimp_SetMode
===============
*/
static rserr_t GLW_SetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	glconfig_t *config = glw_state.config;
	int perChannelColorBits;
	int colorBits, depthBits, stencilBits;
	int i;
	const SDL_DisplayMode *desktopMode;
	SDL_DisplayID display;
	int x;
	int y;
	SDL_WindowFlags flags = 0;

#ifdef USE_VULKAN_API
	if ( vulkan ) {
		flags |= SDL_WINDOW_VULKAN;
		Com_Printf( "Initializing Vulkan display\n");
	} else
#endif
	{
		flags |= SDL_WINDOW_OPENGL;
		Com_Printf( "Initializing OpenGL display\n");
	}

	// If a window exists, note its display index
	if ( SDL_window != NULL )
	{
		display = SDL_GetDisplayForWindow( SDL_window );
		if ( display == 0 )
		{
			Com_DPrintf( "SDL_GetDisplayForWindow() failed: %s\n", SDL_GetError() );
		}
	}
	else
	{
		x = vid_xpos->integer;
		y = vid_ypos->integer;

		// find out to which display our window belongs to
		// according to previously stored \vid_xpos and \vid_ypos coordinates
		display = FindNearestDisplay( &x, &y, 640, 480 );

		//Com_Printf("Selected display: %i\n", display );
	}

	desktopMode = display ? SDL_GetDesktopDisplayMode( display ) : NULL;
	if ( desktopMode != NULL )
	{
		glw_state.desktop_width = desktopMode->w;
		glw_state.desktop_height = desktopMode->h;
	}
	else
	{
		glw_state.desktop_width = 640;
		glw_state.desktop_height = 480;
	}

	config->isFullscreen = fullscreen;
	glw_state.isFullscreen = fullscreen;

	Com_Printf( "...setting mode %d:", mode );

	if ( !CL_GetModeInfo( &config->vidWidth, &config->vidHeight, &config->windowAspect, mode, modeFS, glw_state.desktop_width, glw_state.desktop_height, fullscreen ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}

	Com_Printf( " %d %d\n", config->vidWidth, config->vidHeight );

	// Destroy existing state if it exists
	if ( SDL_glContext != NULL )
	{
		SDL_GL_DestroyContext( SDL_glContext );
		SDL_glContext = NULL;
	}

	if ( SDL_window != NULL )
	{
		SDL_GetWindowPosition( SDL_window, &x, &y );
		Com_DPrintf( "Existing window at %dx%d before being destroyed\n", x, y );
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	gw_active = qfalse;
	gw_minimized = qtrue;

	if ( GLW_UseBorderlessWindow( fullscreen ) )
		flags |= SDL_WINDOW_BORDERLESS;

	if ( !fullscreen )
		flags |= SDL_WINDOW_RESIZABLE;

	//flags |= SDL_WINDOW_ALLOW_HIGHDPI;

	colorBits = r_colorbits->value;

	if ( colorBits == 0 || colorBits > 24 )
		colorBits = 24;

	if ( cl_depthbits->integer == 0 )
	{
		// implicitly assume Z-buffer depth == desktop color depth
		if ( colorBits > 16 )
			depthBits = 24;
		else
			depthBits = 16;
	}
	else
		depthBits = cl_depthbits->integer;

	stencilBits = cl_stencilbits->integer;

	// do not allow stencil if Z-buffer depth likely won't contain it
	if ( depthBits < 24 )
		stencilBits = 0;

	for ( i = 0; i < 16; i++ )
	{
		int testColorBits, testDepthBits, testStencilBits;
		int realColorBits[3];

		// 0 - default
		// 1 - minus colorBits
		// 2 - minus depthBits
		// 3 - minus stencil
		if ((i % 4) == 0 && i)
		{
			// one pass, reduce
			switch (i / 4)
			{
				case 2 :
					if (colorBits == 24)
						colorBits = 16;
					break;
				case 1 :
					if (depthBits == 24)
						depthBits = 16;
					else if (depthBits == 16)
						depthBits = 8;
				case 3 :
					if (stencilBits == 24)
						stencilBits = 16;
					else if (stencilBits == 16)
						stencilBits = 8;
			}
		}

		testColorBits = colorBits;
		testDepthBits = depthBits;
		testStencilBits = stencilBits;

		if ((i % 4) == 3)
		{ // reduce colorBits
			if (testColorBits == 24)
				testColorBits = 16;
		}

		if ((i % 4) == 2)
		{ // reduce depthBits
			if (testDepthBits == 24)
				testDepthBits = 16;
		}

		if ((i % 4) == 1)
		{ // reduce stencilBits
			if (testStencilBits == 8)
				testStencilBits = 0;
		}

		if ( testColorBits == 24 )
			perChannelColorBits = 8;
		else
			perChannelColorBits = 4;

#ifdef USE_VULKAN_API
		if ( !vulkan )
#endif
		{
	
#ifdef __sgi /* Fix for SGIs grabbing too many bits of color */
			if (perChannelColorBits == 4)
				perChannelColorBits = 0; /* Use minimum size for 16-bit color */

			/* Need alpha or else SGIs choose 36+ bit RGB mode */
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 1 );
#endif

			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, testDepthBits );
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, testStencilBits );

			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );

			if ( r_stereoEnabled->integer )
			{
				config->stereoEnabled = qtrue;
				SDL_GL_SetAttribute( SDL_GL_STEREO, 1 );
			}
			else
			{
				config->stereoEnabled = qfalse;
				SDL_GL_SetAttribute( SDL_GL_STEREO, 0 );
			}
		
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

			if ( !r_allowSoftwareGL->integer )
				SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );
		}

		if ( ( SDL_window = GLW_CreateWindow( cl_title, x, y, config->vidWidth, config->vidHeight, flags ) ) == NULL )
		{
			Com_DPrintf( "GLW_CreateWindow failed: %s\n", SDL_GetError() );
			continue;
		}

		if ( fullscreen )
		{
			SDL_DisplayMode mode;
			const SDL_DisplayMode *appliedMode = NULL;

			SDL_zero( mode );
			mode.displayID = display;
			switch ( testColorBits )
			{
				case 16: mode.format = SDL_PIXELFORMAT_RGB565; break;
				case 24: mode.format = SDL_PIXELFORMAT_RGB24;  break;
				default: Com_DPrintf( "testColorBits is %d, can't fullscreen\n", testColorBits ); continue;
			}

			mode.w = config->vidWidth;
			mode.h = config->vidHeight;
			mode.pixel_density = 1.0f;
			mode.refresh_rate = (float)Cvar_VariableIntegerValue( "r_displayRefresh" );
			mode.refresh_rate_numerator = 0;
			mode.refresh_rate_denominator = 0;
			mode.internal = NULL;

#ifdef MACOS_X
			if ( !SDL_SetWindowFullscreenMode( SDL_window, NULL ) )
			{
				Com_DPrintf( "SDL_SetWindowFullscreenMode failed: %s\n", SDL_GetError( ) );
				SDL_DestroyWindow( SDL_window );
				SDL_window = NULL;
				continue;
			}
#else
			if ( !SDL_SetWindowFullscreenMode( SDL_window, &mode ) )
			{
				Com_DPrintf( "SDL_SetWindowFullscreenMode failed: %s\n", SDL_GetError( ) );
				SDL_DestroyWindow( SDL_window );
				SDL_window = NULL;
				continue;
			}
#endif

			if ( !SDL_SetWindowFullscreen( SDL_window, true ) )
			{
				Com_DPrintf( "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError( ) );
				SDL_DestroyWindow( SDL_window );
				SDL_window = NULL;
				continue;
			}

			appliedMode = SDL_GetWindowFullscreenMode( SDL_window );
			if ( appliedMode != NULL )
			{
				config->displayFrequency = (int)appliedMode->refresh_rate;
				config->vidWidth = appliedMode->w;
				config->vidHeight = appliedMode->h;
			}
			else if ( desktopMode != NULL )
			{
				config->displayFrequency = (int)desktopMode->refresh_rate;
				config->vidWidth = desktopMode->w;
				config->vidHeight = desktopMode->h;
			}
		}

#ifdef USE_VULKAN_API
		if ( vulkan )
		{
			config->colorBits = testColorBits;
			config->depthBits = testDepthBits;
			config->stencilBits = testStencilBits;
		}
		else
#endif
		{
			if ( !SDL_glContext )
			{
				if ( ( SDL_glContext = SDL_GL_CreateContext( SDL_window ) ) == NULL )
				{
					Com_DPrintf( "SDL_GL_CreateContext failed: %s\n", SDL_GetError( ) );
					SDL_DestroyWindow( SDL_window );
					SDL_window = NULL;
					continue;
				}
			}

			if ( !SDL_GL_SetSwapInterval( r_swapInterval->integer ) )
			{
				Com_DPrintf( "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError( ) );
			}

			SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &realColorBits[0] );
			SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &realColorBits[1] );
			SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &realColorBits[2] );
			SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &config->depthBits );
			SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &config->stencilBits );

			config->colorBits = realColorBits[0] + realColorBits[1] + realColorBits[2];
		} // if ( !vulkan )


		Com_Printf( "Using %d color bits, %d depth, %d stencil display.\n",	config->colorBits, config->depthBits, config->stencilBits );

		break;
	}

	if ( SDL_window )
	{
#ifdef USE_ICON
		SDL_Surface *icon = SDL_CreateSurfaceFrom(
			CLIENT_WINDOW_ICON.width,
			CLIENT_WINDOW_ICON.height,
			SDL_GetPixelFormatForMasks(
				CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
#ifdef Q3_LITTLE_ENDIAN
				0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
				0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
			),
			(void *)CLIENT_WINDOW_ICON.pixel_data,
			CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width
		);
		if ( icon )
		{
			SDL_SetWindowIcon( SDL_window, icon );
			SDL_DestroySurface( icon );
		}
#endif
	}
	else
	{
		Com_Printf( "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	if ( GLW_UseBorderlessWindow( fullscreen ) )
		SDL_SetWindowHitTest( SDL_window, SDL_HitTestFunc, NULL );

	if ( !SDL_GetWindowSizeInPixels( SDL_window, &config->vidWidth, &config->vidHeight ) )
	{
		Com_DPrintf( "SDL_GetWindowSizeInPixels failed: %s\n", SDL_GetError() );
	}

	// save render dimensions as renderer may change it in advance
	glw_state.window_width = config->vidWidth;
	glw_state.window_height = config->vidHeight;

	SDL_WarpMouseInWindow( SDL_window, glw_state.window_width / 2, glw_state.window_height / 2 );

	return RSERR_OK;
}


/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static rserr_t GLimp_StartDriverAndSetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	rserr_t err;

	if ( fullscreen && in_nograb->integer )
	{
		Com_Printf( "Fullscreen not allowed with \\in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		const char *driverName;

		if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) )
		{
			Com_Printf( "SDL_InitSubSystem( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError() );
			return RSERR_FATAL_ERROR;
		}

		driverName = SDL_GetCurrentVideoDriver();

		Com_Printf( "SDL using driver \"%s\"\n", driverName );
	}

	err = GLW_SetMode( mode, modeFS, fullscreen, vulkan );

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
			return err;
		case RSERR_INVALID_MODE:
			Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
			return err;
		default:
			break;
	}

	return RSERR_OK;
}


/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
void GLimp_Init( glconfig_t *config )
{
	rserr_t err;

#ifndef _WIN32
	InitSig();
#endif

	Com_DPrintf( "GLimp_Init()\n" );

	glw_state.config = config; // feedback renderer configuration

	in_nograb = Cvar_Get( "in_nograb", "0", 0 );
	Cvar_SetDescription( in_nograb, "Do not capture mouse in game, may be useful during online streaming." );

	r_allowSoftwareGL = Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );

	r_swapInterval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( r_stereoEnabled, "Enable stereo rendering for techniques like shutter glasses." );

	// Create the window and set up the context
	err = GLimp_StartDriverAndSetMode( r_mode->integer, r_modeFullscreen->string, r_fullscreen->integer, qfalse );
	if ( err != RSERR_OK )
	{
		if ( err == RSERR_FATAL_ERROR )
		{
			Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
			return;
		}

		if ( r_mode->integer != 3 || ( r_fullscreen->integer && atoi( r_modeFullscreen->string ) != 3 ) )
		{
			Com_Printf( "Setting \\r_mode %d failed, falling back on \\r_mode %d\n", r_mode->integer, 3 );
			if ( GLimp_StartDriverAndSetMode( 3, "", r_fullscreen->integer, qfalse ) != RSERR_OK )
			{
				// Nothing worked, give up
				Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
				return;
			}
		}
	}

	// These values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init();

	HandleEvents();

	Key_ClearStates();
}


/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
	// don't flip if drawing to front buffer
	if ( Q_stricmp( cl_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		SDL_GL_SwapWindow( SDL_window );
	}
}


/*
===============
GL_GetProcAddress

Used by opengl renderers to resolve all qgl* function pointers
===============
*/
void *GL_GetProcAddress( const char *symbol )
{
	return FunctionPointerToVoidPtr( SDL_GL_GetProcAddress( symbol ) );
}


#ifdef USE_VULKAN_API
/*
===============
VKimp_Init

This routine is responsible for initializing the OS specific portions
of Vulkan
===============
*/
void VKimp_Init( glconfig_t *config )
{
	rserr_t err;

#ifndef _WIN32
	InitSig();
#endif

	Com_DPrintf( "VKimp_Init()\n" );

	in_nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_nograb, "Do not capture mouse in game, may be useful during online streaming." );

	r_swapInterval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( r_stereoEnabled, "Enable stereo rendering for techniques like shutter glasses." );

	// feedback to renderer configuration
	glw_state.config = config;

	// Create the window and set up the context
	err = GLimp_StartDriverAndSetMode( r_mode->integer, r_modeFullscreen->string, r_fullscreen->integer, qtrue /* Vulkan */ );
	if ( err != RSERR_OK )
	{
		if ( err == RSERR_FATAL_ERROR )
		{
			Com_Error( ERR_FATAL, "VKimp_Init() - could not load Vulkan subsystem" );
			return;
		}

		Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n", r_mode->integer, 3 );

		err = GLimp_StartDriverAndSetMode( 3, "", r_fullscreen->integer, qtrue /* Vulkan */ );
		if( err != RSERR_OK )
		{
			// Nothing worked, give up
			Com_Error( ERR_FATAL, "VKimp_Init() - could not load Vulkan subsystem" );
			return;
		}
	}

	qvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();

	if ( qvkGetInstanceProcAddr == NULL )
	{
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		Com_Error( ERR_FATAL, "VKimp_Init: qvkGetInstanceProcAddr is NULL" );
	}

	// These values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init();

	HandleEvents();

	Key_ClearStates();
}


/*
===============
VK_GetInstanceProcAddr
===============
*/
void *VK_GetInstanceProcAddr( VkInstance instance, const char *name )
{
	return FunctionPointerToVoidPtr( qvkGetInstanceProcAddr( instance, name ) );
}


/*
===============
VK_CreateSurface
===============
*/
qboolean VK_CreateSurface( VkInstance instance, VkSurfaceKHR *surface )
{
	if ( SDL_Vulkan_CreateSurface( SDL_window, instance, NULL, surface ) )
		return qtrue;
	else
		return qfalse;
}


/*
===============
VKimp_Shutdown
===============
*/
void VKimp_Shutdown( qboolean unloadDLL )
{
	const char* drv = SDL_GetCurrentVideoDriver();

	IN_Shutdown();

	if ( glw_state.isFullscreen ) {
		if ( drv && strcmp( drv, "x11" ) == 0 ) {
			SDL_WarpMouseGlobal( glw_state.desktop_width / 2, glw_state.desktop_height / 2 );
		} else {
			GLW_SetCursorVisible( qtrue );
		}
	}

	if ( SDL_window ) {
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	if ( unloadDLL )
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
}
#endif // USE_VULKAN_API


/*
================
GLW_HideFullscreenWindow
================
*/
void GLW_HideFullscreenWindow( void ) {
	if ( SDL_window && glw_state.isFullscreen ) {
		SDL_HideWindow( SDL_window );
	}
}


/*
===============
Sys_GetClipboardData
===============
*/
char *Sys_GetClipboardData( void )
{
#ifdef DEDICATED
	return NULL;
#else
	char *data = NULL;
	char *cliptext;

	if ( ( cliptext = SDL_GetClipboardText() ) != NULL ) {
		if ( cliptext[0] != '\0' ) {
			size_t bufsize = strlen( cliptext ) + 1;

			data = Z_Malloc( bufsize );
			Q_strncpyz( data, cliptext, bufsize );

			// find first listed char and set to '\0'
			strtok( data, "\n\r\b" );
		}
		SDL_free( cliptext );
	}
	return data;
#endif
}


/*
===============
Sys_SetClipboardBitmap
===============
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
	sdlClipboardBitmap_t *clipboard;

	if ( !bitmap || length <= 0 )
		return;

	clipboard = (sdlClipboardBitmap_t *)SDL_calloc( 1, sizeof( *clipboard ) );
	if ( !clipboard ) {
		Com_DPrintf( "Sys_SetClipboardBitmap: SDL_calloc failed: %s\n", SDL_GetError() );
		return;
	}

	clipboard->data = (byte *)SDL_malloc( length );
	if ( !clipboard->data ) {
		Com_DPrintf( "Sys_SetClipboardBitmap: SDL_malloc failed: %s\n", SDL_GetError() );
		SDL_free( clipboard );
		return;
	}

	memcpy( clipboard->data, bitmap, length );
	clipboard->size = (size_t)length;

	if ( !SDL_SetClipboardData( GLW_ClipboardBitmapData, GLW_ClipboardBitmapCleanup, clipboard,
		s_clipboardBitmapMimeTypes, ARRAY_LEN( s_clipboardBitmapMimeTypes ) ) )
	{
		Com_DPrintf( "Sys_SetClipboardBitmap: SDL_SetClipboardData failed: %s\n", SDL_GetError() );
		GLW_ClipboardBitmapCleanup( clipboard );
	}
}
