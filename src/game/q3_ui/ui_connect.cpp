// Copyright (C) 1999-2000 Id Software, Inc.
//
#include "ui_local.h"

#include <array>
#include <algorithm>

/*
===============================================================================

CONNECTION SCREEN

===============================================================================
*/

//qboolean	passwordNeeded = qtrue;
//menufield_s passwordField;

static connstate_t	lastConnState;
static char			lastLoadingText[MAX_INFO_VALUE];

namespace {

using DownloadInfoBuffer = std::array<char, 64>;

[[nodiscard]] int GetDownloadCvarValue( const char *name ) {
	std::array<char, 64> buffer{};
	trap_Cvar_VariableStringBuffer( name, buffer.data(), static_cast<int>( buffer.size() ) );
	return atoi( buffer.data() );
}

[[nodiscard]] int MaxConnectionLabelWidth( const int style ) {
	const auto labels = std::to_array<const char *>( { "Downloading:", "Estimated time left:", "Transfer rate:" } );
	int maxWidth = 0;
	for ( const char *label : labels ) {
		maxWidth = std::max( maxWidth, static_cast<int>( UI_ProportionalStringWidth( label ) * UI_ProportionalSizeScale( style ) ) );
	}
	return maxWidth + 16;
}

void DrawCopiedAmount( const int leftWidth, const int y, const DownloadInfoBuffer &downloaded, const DownloadInfoBuffer &total, const int style ) {
	UI_DrawProportionalString( leftWidth, y, va( "(%s of %s copied)", downloaded.data(), total.data() ), style, color_white );
}

void UI_ReadableSize ( char *buf, int bufsize, int value )
{
	if (value > 1024*1024*1024 ) { // gigs
		Com_sprintf( buf, bufsize, "%d", value / (1024*1024*1024) );
		Com_sprintf( buf+strlen(buf), bufsize-strlen(buf), ".%02d GB", 
			(value % (1024*1024*1024))*100 / (1024*1024*1024) );
	} else if (value > 1024*1024 ) { // megs
		Com_sprintf( buf, bufsize, "%d", value / (1024*1024) );
		Com_sprintf( buf+strlen(buf), bufsize-strlen(buf), ".%02d MB", 
			(value % (1024*1024))*100 / (1024*1024) );
	} else if (value > 1024 ) { // kilos
		Com_sprintf( buf, bufsize, "%d KB", value / 1024 );
	} else { // bytes
		Com_sprintf( buf, bufsize, "%d bytes", value );
	}
}

// Assumes time is in msec
void UI_PrintTime ( char *buf, int bufsize, int time ) {
	time /= 1000;  // change to seconds

	if (time > 3600) { // in the hours range
		Com_sprintf( buf, bufsize, "%d hr %d min", time / 3600, (time % 3600) / 60 );
	} else if (time > 60) { // mins
		Com_sprintf( buf, bufsize, "%d min %d sec", time / 60, time % 60 );
	} else  { // secs
		Com_sprintf( buf, bufsize, "%d sec", time );
	}
}

} // namespace

static void UI_DisplayDownloadInfo( const char *downloadName ) {
	static constexpr char dlText[]	= "Downloading:";
	static constexpr char etaText[]	= "Estimated time left:";
	static constexpr char xferText[]	= "Transfer rate:";

	int downloadSize, downloadCount, downloadTime, percentage;
	DownloadInfoBuffer dlSizeBuf{};
	DownloadInfoBuffer totalSizeBuf{};
	DownloadInfoBuffer xferRateBuf{};
	DownloadInfoBuffer dlTimeBuf{};
	int xferRate;
	int leftWidth, div;
	int style = UI_LEFT|UI_SMALLFONT|UI_DROPSHADOW;
	const char *s;

	downloadSize = GetDownloadCvarValue( "cl_downloadSize" );
	downloadCount = GetDownloadCvarValue( "cl_downloadCount" );
	downloadTime = GetDownloadCvarValue( "cl_downloadTime" );

#if 0 // bk010104
	fprintf( stderr, "\n\n-----------------------------------------------\n");
	fprintf( stderr, "DB: downloadSize:  %16d\n", downloadSize );
	fprintf( stderr, "DB: downloadCount: %16d\n", downloadCount );
	fprintf( stderr, "DB: downloadTime:  %16d\n", downloadTime );  
  	fprintf( stderr, "DB: UI realtime:   %16d\n", uis.realtime );	// bk
	fprintf( stderr, "DB: UI frametime:  %16d\n", uis.frametime );	// bk
#endif

	leftWidth = MaxConnectionLabelWidth( style );

	UI_DrawProportionalString( 8, 128, dlText, style, color_white );
	UI_DrawProportionalString( 8, 160, etaText, style, color_white );
	UI_DrawProportionalString( 8, 224, xferText, style, color_white );

	if (downloadSize > 0) {
		if ( downloadCount > 21474836 ) {// x100 could cause overflow!
			div = downloadSize >> 8;
			if ( div )
				percentage = (downloadCount >> 8) * 100 / div;
			else
				percentage = 0;
		} else
			percentage = downloadCount * 100 / downloadSize;
		if ( percentage > 100 ) 
			percentage = 100;
		s = va( "%s (%d%%)", downloadName, percentage );
	} else {
		s = downloadName;
	}

	UI_DrawProportionalString( leftWidth, 128, s, style, color_white );

	UI_ReadableSize( dlSizeBuf.data(), static_cast<int>( dlSizeBuf.size() ), downloadCount );
	UI_ReadableSize( totalSizeBuf.data(), static_cast<int>( totalSizeBuf.size() ), downloadSize );

	if (downloadCount < 4096 || !downloadTime) {
		UI_DrawProportionalString( leftWidth, 160, "estimating", style, color_white );
		DrawCopiedAmount( leftWidth, 192, dlSizeBuf, totalSizeBuf, style );
	} else {
	  // bk010108
	  //float elapsedTime = (float)(uis.realtime - downloadTime); // current - start (msecs)
	  //elapsedTime = elapsedTime * 0.001f; // in seconds
	  //if ( elapsedTime <= 0.0f ) elapsedTime == 0.0f;
	  if ( (uis.realtime - downloadTime) / 1000) {
			xferRate = downloadCount / ((uis.realtime - downloadTime) / 1000);
		  //xferRate = (int)( ((float)downloadCount) / elapsedTime);
		} else {
			xferRate = 0;
		}

	  //fprintf( stderr, "DB: elapsedTime:  %16.8f\n", elapsedTime );	// bk
	  //fprintf( stderr, "DB: xferRate:   %16d\n", xferRate );	// bk

		UI_ReadableSize( xferRateBuf.data(), static_cast<int>( xferRateBuf.size() ), xferRate );

		// Extrapolate estimated completion time
		if (downloadSize && xferRate) {
			int n = downloadSize / xferRate; // estimated time for entire d/l in secs

			// We do it in K (/1024) because we'd overflow around 4MB
			n = (n - (((downloadCount/1024) * n) / (downloadSize/1024))) * 1000;
			
			UI_PrintTime ( dlTimeBuf.data(), static_cast<int>( dlTimeBuf.size() ), n ); // bk010104
				//(n - (((downloadCount/1024) * n) / (downloadSize/1024))) * 1000);

			UI_DrawProportionalString( leftWidth, 160, 
				dlTimeBuf.data(), style, color_white );
			DrawCopiedAmount( leftWidth, 192, dlSizeBuf, totalSizeBuf, style );
		} else {
			UI_DrawProportionalString( leftWidth, 160, 
				"estimating", style, color_white );
			if (downloadSize) {
				DrawCopiedAmount( leftWidth, 192, dlSizeBuf, totalSizeBuf, style );
			} else {
				UI_DrawProportionalString( leftWidth, 192, 
					va("(%s copied)", dlSizeBuf.data()), style, color_white );
			}
		}

		if (xferRate) {
			UI_DrawProportionalString( leftWidth, 224, 
				va("%s/Sec", xferRateBuf.data()), style, color_white );
		}
	}
}

/*
========================
UI_DrawConnectScreen

This will also be overlaid on the cgame info screen during loading
to prevent it from blinking away too rapidly on local or lan games.
========================
*/
void UI_DrawConnectScreen( qboolean overlay ) {
	char			*s;
	uiClientState_t	cstate;
	std::array<char, MAX_INFO_VALUE> info{};

	UI_VideoCheck( trap_Milliseconds() );

	Menu_Cache();

	if ( !overlay ) {
		// draw the dialog background
		UI_SetColor( color_white );
		// fill whole screen, not just 640x480 virtual rectangle
		trap_R_DrawStretchPic( 0, 0, uis.glconfig.vidWidth, uis.glconfig.vidHeight, 0, 0, 1, 1, uis.menuBackShader );
	}

	// see what information we should display
	trap_GetClientState( &cstate );

	info[0] = '\0';
	if( trap_GetConfigString( CS_SERVERINFO, info.data(), static_cast<int>( info.size() ) ) ) {
		UI_DrawProportionalString( 320, 16, va( "Loading %s", Info_ValueForKey( info.data(), "mapname" ) ), UI_BIGFONT|UI_CENTER|UI_DROPSHADOW, color_white );
	}

	UI_DrawProportionalString( 320, 64, va("Connecting to %s", cstate.servername), UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, menu_text_color );
	//UI_DrawProportionalString( 320, 96, "Press Esc to abort", UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, menu_text_color );

	// display global MOTD at bottom
	UI_DrawProportionalString( SCREEN_WIDTH/2, SCREEN_HEIGHT-32, 
		Info_ValueForKey( cstate.updateInfoString, "motd" ), UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, menu_text_color );
	
	// print any server info (server full, bad version, etc)
	if ( cstate.connState < CA_CONNECTED ) {
		UI_DrawProportionalString_AutoWrapped( 320, 192, 630, 20, cstate.messageString, UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, menu_text_color );
	}

#if 0
	// display password field
	if ( passwordNeeded ) {
		s_ingame_menu.x = SCREEN_WIDTH * 0.50 - 128;
		s_ingame_menu.nitems = 0;
		s_ingame_menu.wrapAround = qtrue;

		passwordField.generic.type = MTYPE_FIELD;
		passwordField.generic.name = "Password:";
		passwordField.generic.callback = 0;
		passwordField.generic.x		= 10;
		passwordField.generic.y		= 180;
		Field_Clear( &passwordField.field );
		passwordField.width = 256;
		passwordField.field.widthInChars = 16;
		Q_strncpyz( passwordField.field.buffer, Cvar_VariableString("password"), 
			sizeof(passwordField.field.buffer) );

		Menu_AddItem( &s_ingame_menu, ( void * ) &s_customize_player_action );

		MField_Draw( &passwordField );
	}
#endif

	if ( lastConnState > cstate.connState ) {
		lastLoadingText[0] = '\0';
	}
	lastConnState = cstate.connState;

	switch ( cstate.connState ) {
	case CA_CONNECTING:
		s = va("Awaiting challenge...%i", cstate.connectPacketCount);
		break;
	case CA_CHALLENGING:
		s = va("Awaiting connection...%i", cstate.connectPacketCount);
		break;
	case CA_CONNECTED: {
		std::array<char, MAX_INFO_VALUE> downloadName{};

			trap_Cvar_VariableStringBuffer( "cl_downloadName", downloadName.data(), static_cast<int>( downloadName.size() ) );
			if ( downloadName.front() != '\0' ) {
				UI_DisplayDownloadInfo( downloadName.data() );
				return;
			}
		}
		s = "Awaiting gamestate...";
		break;
	case CA_LOADING:
		return;
	case CA_PRIMED:
		return;
	default:
		return;
	}

	UI_DrawProportionalString( 320, 128, s, UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, color_white );

	// password required / connection rejected information goes here
}


/*
===================
UI_KeyConnect
===================
*/
void UI_KeyConnect( int key ) {
	if ( key == K_ESCAPE ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "disconnect\n" );
		return;
	}
}
