// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_info.c -- display information while data is being loading

#include "cg_local.h"

#include <array>
#include <cstring>
#include <span>

#define MAX_LOADING_PLAYER_ICONS	16
#define MAX_LOADING_ITEM_ICONS		26

static int			loadingPlayerIconCount;
static int			loadingItemIconCount;
static std::array<qhandle_t, MAX_LOADING_PLAYER_ICONS> loadingPlayerIcons{};
static std::array<qhandle_t, MAX_LOADING_ITEM_ICONS> loadingItemIcons{};

namespace {

[[nodiscard]] auto LoadingPlayerIconEntries() noexcept -> std::span<const qhandle_t> {
	return { loadingPlayerIcons.data(), static_cast<std::size_t>( loadingPlayerIconCount ) };
}

[[nodiscard]] auto LoadingItemIconEntries() noexcept -> std::span<const qhandle_t> {
	return { loadingItemIcons.data(), static_cast<std::size_t>( loadingItemIconCount ) };
}

void NormalizeLoadingText( char *text ) {
	for ( char &character : std::span{ text, std::strlen( text ) } ) {
		// convert to normal unless UI font will support extended characters
		character &= 127;
	}
}

void SetLoadingText( const char *text ) {
	Q_strncpyz( cg.infoScreenText, DecodedString( text ), sizeof( cg.infoScreenText ) );
	NormalizeLoadingText( cg.infoScreenText );
}

[[nodiscard]] const char *LoadingClientSkinName( char *modelName ) {
	char *const skin = Q_strrchr( modelName, '/' );
	if ( skin ) {
		*skin = '\0';
		return skin + 1;
	}

	return "default";
}

void FormatLoadingClientIconPath( std::array<char, MAX_QPATH> &iconPath, const char *modelName, const char *skinName, const qboolean charactersPath ) {
	Com_sprintf(
		iconPath.data(),
		iconPath.size(),
		charactersPath ? "models/players/characters/%s/icon_%s.tga" : "models/players/%s/icon_%s.tga",
		modelName,
		skinName
	);
}

[[nodiscard]] qhandle_t RegisterLoadingClientIcon( char *modelName ) {
	const char *const skinName = LoadingClientSkinName( modelName );
	std::array<char, MAX_QPATH> iconPath{};

	FormatLoadingClientIconPath( iconPath, modelName, skinName, qfalse );
	qhandle_t icon = trap_R_RegisterShaderNoMip( iconPath.data() );
	if ( icon ) {
		return icon;
	}

	FormatLoadingClientIconPath( iconPath, modelName, skinName, qtrue );
	icon = trap_R_RegisterShaderNoMip( iconPath.data() );
	if ( icon ) {
		return icon;
	}

	FormatLoadingClientIconPath( iconPath, DEFAULT_MODEL, "default", qfalse );
	return trap_R_RegisterShaderNoMip( iconPath.data() );
}

void TryAddLoadingPlayerIcon( const char *info ) {
	if ( loadingPlayerIconCount >= MAX_LOADING_PLAYER_ICONS ) {
		return;
	}

	std::array<char, MAX_QPATH> model{};
	Q_strncpyz( model.data(), Info_ValueForKey( info, "model" ), model.size() );
	const qhandle_t icon = RegisterLoadingClientIcon( model.data() );
	if ( icon ) {
		loadingPlayerIcons[loadingPlayerIconCount++] = icon;
	}
}

void DrawCenteredLoadingInfo( const int y, const char *text ) {
	UI_DrawProportionalString( 320, y, text, UI_CENTER|UI_SMALLFONT|UI_DROPSHADOW, colorWhite );
}

[[nodiscard]] const char *GametypeDescription( const int gametype, std::array<char, 1024> &fallbackBuffer ) {
	switch ( gametype ) {
	case GT_FFA:
		return "Free For All";
	case GT_SINGLE_PLAYER:
		return "Single Player";
	case GT_TOURNAMENT:
		return "Tournament";
	case GT_TEAM:
		return "Team Deathmatch";
	case GT_CTF:
		return "Capture The Flag";
#ifdef MISSIONPACK
	case GT_1FCTF:
		return "One Flag CTF";
	case GT_OBELISK:
		return "Overload";
	case GT_HARVESTER:
		return "Harvester";
#endif
	default:
		BG_sprintf( fallbackBuffer.data(), "Gametype #%i", gametype );
		return fallbackBuffer.data();
	}
}

} // namespace


/*
===================
CG_DrawLoadingIcons
===================
*/
static void CG_DrawLoadingIcons( void ) {
	int iconIndex = 0;

	for ( const qhandle_t icon : LoadingPlayerIconEntries() ) {
		const int x = 16 + iconIndex * 78;
		const int y = 324 - 40;
		CG_DrawPic( x, y, 64, 64, icon );
		++iconIndex;
	}

	iconIndex = 0;
	for ( const qhandle_t icon : LoadingItemIconEntries() ) {
		int y = 400 - 40;
		if ( iconIndex >= 13 ) {
			y += 40;
		}
		const int x = 16 + iconIndex % 13 * 48;
		CG_DrawPic( x, y, 32, 32, icon );
		++iconIndex;
	}
}


/*
======================
CG_LoadingString

======================
*/
void CG_LoadingString( const char *s ) {
	SetLoadingText( s );
	trap_UpdateScreen();
}

/*
===================
CG_LoadingItem
===================
*/
void CG_LoadingItem( int itemNum ) {
	gitem_t		*item;

	item = &bg_itemlist[itemNum];
	
	if ( item->icon && loadingItemIconCount < MAX_LOADING_ITEM_ICONS ) {
		loadingItemIcons[loadingItemIconCount] = trap_R_RegisterShaderNoMip( item->icon );
		loadingItemIconCount++;
	}

	CG_LoadingString( item->pickup_name );
}

/*
===================
CG_LoadingClient
===================
*/
void CG_LoadingClient( int clientNum ) {
	const char		*info;
	std::array<char, MAX_QPATH> personality{};

	info = CG_ConfigString( CS_PLAYERS + clientNum );
	TryAddLoadingPlayerIcon( info );

	BG_CleanName( Info_ValueForKey( info, "n" ), personality.data(), personality.size(), "unknown client" );
	BG_StripColor( personality.data() );

	if ( cgs.gametype == GT_SINGLE_PLAYER ) {
		trap_S_RegisterSound( va( "sound/player/announce/%s.wav", personality.data() ), qtrue );
	}

	CG_LoadingString( personality.data() );
}


/*
====================
CG_DrawInformation

Draw all the status / pacifier stuff during level loading
====================
*/
void CG_DrawInformation( void ) {
	const char	*s;
	const char	*info;
	const char	*sysInfo;
	int			y;
	int			value;
	qhandle_t	levelshot;
	qhandle_t	detail;
	std::array<char, 1024> buf{};
	char		*ptr;

	info = CG_ConfigString( CS_SERVERINFO );
	sysInfo = CG_ConfigString( CS_SYSTEMINFO );

	s = Info_ValueForKey( info, "mapname" );
	levelshot = trap_R_RegisterShaderNoMip( va( "levelshots/%s.tga", s ) );
	if ( !levelshot ) {
		levelshot = trap_R_RegisterShaderNoMip( "menu/art/unknownmap" );
	}

	trap_R_SetColor( nullptr );
	// fill whole screen, not just 640x480 virtual rectangle
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 1, 1, levelshot );

	// blend a detail texture over it
	detail = trap_R_RegisterShader( "levelShotDetail" );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 2.5, 2, detail );

	// draw the icons of things as they are loaded
	CG_DrawLoadingIcons();

	// the first 150 rows are reserved for the client connection
	// screen to write into
	if ( cg.infoScreenText[0] ) {
		DrawCenteredLoadingInfo( 128 - 32, va("Loading... %s", cg.infoScreenText) );
	} else {
		DrawCenteredLoadingInfo( 128 - 32, "Awaiting snapshot..." );
	}

	// draw info string information

	y = 180-32;

	// don't print server lines if playing a local game
	//trap_Cvar_VariableStringBuffer( "sv_running", buf, sizeof( buf ) );
	//if ( !atoi( buf ) ) 
	{
		// server hostname
		Q_strncpyz( buf.data(), Info_ValueForKey( info, "sv_hostname" ), buf.size() );
		Q_CleanStr( buf.data() );
		DrawCenteredLoadingInfo( y, buf.data() );
		y += PROP_HEIGHT;

		buf[0] = '\0';
		ptr = buf.data();

		// unlagged server
		s = Info_ValueForKey( info, "g_unlagged" );
		if ( s[0] == '1' ) {
			ptr = Q_stradd( ptr, "Unlagged" );
		}

		// pure server
		s = Info_ValueForKey( sysInfo, "sv_pure" );
		if ( s[0] == '1' ) {
			if ( buf[0] ) {
				ptr = Q_stradd( ptr, ", " );
			}
			ptr = Q_stradd( ptr, "Pure" );
		}

		if ( buf[0] ) {
			ptr = Q_stradd( ptr, " Server" );
			DrawCenteredLoadingInfo( y, buf.data() );
			y += PROP_HEIGHT;
		}

		// server-specific message of the day
		s = CG_ConfigString( CS_MOTD );
		if ( s[0] ) {
			DrawCenteredLoadingInfo( y, s );
			y += PROP_HEIGHT;
		}

		// some extra space after hostname and motd
		y += 10;
	}

	// map-specific message (long map name)
	s = CG_ConfigString( CS_MESSAGE );
	if ( s[0] ) {
		DrawCenteredLoadingInfo( y, s );
		y += PROP_HEIGHT;
	}

	// cheats warning
	s = Info_ValueForKey( sysInfo, "sv_cheats" );
	if ( s[0] == '1' ) {
		DrawCenteredLoadingInfo( y, "CHEATS ARE ENABLED" );
		y += PROP_HEIGHT;
	}

	// game type
	s = GametypeDescription( cgs.gametype, buf );
	DrawCenteredLoadingInfo( y, s );
	y += PROP_HEIGHT;
		
	value = atoi( Info_ValueForKey( info, "timelimit" ) );
	if ( value ) {
		DrawCenteredLoadingInfo( y, va( "timelimit %i", value ) );
		y += PROP_HEIGHT;
	}

	if (cgs.gametype < GT_CTF ) {
		value = atoi( Info_ValueForKey( info, "fraglimit" ) );
		if ( value ) {
			DrawCenteredLoadingInfo( y, va( "fraglimit %i", value ) );
			y += PROP_HEIGHT;
		}
	}

	if (cgs.gametype >= GT_CTF) {
		value = atoi( Info_ValueForKey( info, "capturelimit" ) );
		if ( value ) {
			DrawCenteredLoadingInfo( y, va( "capturelimit %i", value ) );
			y += PROP_HEIGHT;
		}
	}
}
