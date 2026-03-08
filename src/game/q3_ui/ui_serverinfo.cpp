// Copyright (C) 1999-2000 Id Software, Inc.
//
#include "ui_local.h"

#include <algorithm>
#include <array>
#include <cstring>

#define SERVERINFO_FRAMEL	"menu/art/frame2_l"
#define SERVERINFO_FRAMER	"menu/art/frame1_r"
#define SERVERINFO_BACK0	"menu/art/back_0"
#define SERVERINFO_BACK1	"menu/art/back_1"

#define ID_ADD	 100
#define ID_BACK	 101

#define MAX_INFO_LINES		64
#define INFO_LINE_WIDTH		51

namespace {

constexpr std::array<const char *, 4> ServerInfoArtList = {
	SERVERINFO_FRAMEL,
	SERVERINFO_FRAMER,
	SERVERINFO_BACK0,
	SERVERINFO_BACK1
};

using FavoriteAddressBuffer = std::array<char, 128>;
using ServerInfoLines = std::array<std::array<char, INFO_LINE_WIDTH * 3>, MAX_INFO_LINES>;
using ServerInfoItems = std::array<const char *, MAX_INFO_LINES>;

FavoriteAddressBuffer ServerAddressBuffer() {
	return {};
}

qboolean IsAvailableFavoriteSlot( const char *address, const int bestSlot ) {
	return bestSlot == 0 && ( address[0] < '0' || address[0] > '9' );
}

void BuildServerInfoLine( std::array<char, INFO_LINE_WIDTH * 3> &destination, const char *key, const char *value, const int maxKeyLength ) {
	std::array<char, MAX_INFO_VALUE * 2> line{};
	const int padding = maxKeyLength - static_cast<int>( std::strlen( key ) );

	std::fill_n( line.data(), padding, ' ' );
	BG_sprintf( line.data() + padding, "%s ^3%s", key, value );
	line[destination.size() - 1] = '\0';
	Q_strncpyz( destination.data(), line.data(), static_cast<int>( destination.size() ) );
}

}

typedef struct
{
	menuframework_s	menu;
	menutext_s		banner;
	menubitmap_s	framel;
	menubitmap_s	framer;
	menubitmap_s	back;
	menutext_s		add;
	menulist_s		list;
	char			info[MAX_INFO_STRING];
} serverinfo_t;

static serverinfo_t	s_serverinfo;

static ServerInfoItems itemnames;
static ServerInfoLines show_info;

/*
=================
Favorites_Add

Add current server to favorites
=================
*/
void Favorites_Add( void )
{
	auto adrstr = ServerAddressBuffer();
	auto serverbuff = ServerAddressBuffer();
	int		i;
	int		best;

	trap_Cvar_VariableStringBuffer( "cl_currentServerAddress", serverbuff.data(), static_cast<int>( serverbuff.size() ) );
	if( !serverbuff[0] )
		return;

	best = 0;
	for( i = 0; i < MAX_FAVORITESERVERS; i++ ) {
		trap_Cvar_VariableStringBuffer( va("server%d",i+1), adrstr.data(), static_cast<int>( adrstr.size() ) );
		if( !Q_stricmp( serverbuff.data(), adrstr.data() ) ) {
			// already in list
			return;
		}
		
		// use first empty or non-numeric available slot
		if( IsAvailableFavoriteSlot( adrstr.data(), best ) ) {
			best = i+1;
		}
	}

	if( best ) {
		trap_Cvar_Set( va("server%d",best), serverbuff.data() );
	}
}


/*
=================
ServerInfo_Event
=================
*/
static void ServerInfo_Event( void* ptr, int event )
{
	switch (((menucommon_s*)ptr)->id)
	{
		case ID_ADD:
			if (event != QM_ACTIVATED)
				break;
		
			Favorites_Add();
			UI_PopMenu();
			break;

		case ID_BACK:
			if (event != QM_ACTIVATED)
				break;

			UI_PopMenu();
			break;
	}
}

/*
=================
ServerInfo_MenuKey
=================
*/
static sfxHandle_t ServerInfo_MenuKey( int key )
{
	return ( Menu_DefaultKey( &s_serverinfo.menu, key ) );
}

/*
=================
ServerInfo_Cache
=================
*/
void ServerInfo_Cache( void )
{
	for( const char *const shader : ServerInfoArtList ) {
		trap_R_RegisterShaderNoMip( shader );
	}
}

/*
=================
UI_ServerInfoMenu
=================
*/
void UI_ServerInfoMenu( void )
{
	const char		*s;
	std::array<char, MAX_INFO_KEY> key{};
	std::array<char, MAX_INFO_VALUE> value{};
	int				len, max;

	// zero set all our globals
	s_serverinfo = {};
	itemnames.fill( nullptr );

	ServerInfo_Cache();

	s_serverinfo.menu.key        = ServerInfo_MenuKey;
	s_serverinfo.menu.wrapAround = qtrue;
	s_serverinfo.menu.fullscreen = qtrue;

	s_serverinfo.banner.generic.type  = MTYPE_BTEXT;
	s_serverinfo.banner.generic.x	  = 320;
	s_serverinfo.banner.generic.y	  = 16;
	s_serverinfo.banner.string		  = "SERVER INFO";
	s_serverinfo.banner.color	      = color_white;
	s_serverinfo.banner.style	      = UI_CENTER;

	s_serverinfo.framel.generic.type  = MTYPE_BITMAP;
	s_serverinfo.framel.generic.name  = SERVERINFO_FRAMEL;
	s_serverinfo.framel.generic.flags = QMF_INACTIVE;
	s_serverinfo.framel.generic.x	  = 0;  
	s_serverinfo.framel.generic.y	  = 78;
	s_serverinfo.framel.width  	      = 256;
	s_serverinfo.framel.height  	  = 329;

	s_serverinfo.framer.generic.type  = MTYPE_BITMAP;
	s_serverinfo.framer.generic.name  = SERVERINFO_FRAMER;
	s_serverinfo.framer.generic.flags = QMF_INACTIVE;
	s_serverinfo.framer.generic.x	  = 376;
	s_serverinfo.framer.generic.y	  = 76;
	s_serverinfo.framer.width  	      = 256;
	s_serverinfo.framer.height  	  = 334;

	s_serverinfo.add.generic.type	  = MTYPE_PTEXT;
	s_serverinfo.add.generic.flags    = QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
	s_serverinfo.add.generic.callback = ServerInfo_Event;
	s_serverinfo.add.generic.id	      = ID_ADD;
	s_serverinfo.add.generic.x		  = 320;
	s_serverinfo.add.generic.y		  = 371;
	s_serverinfo.add.string  		  = "ADD TO FAVORITES";
	s_serverinfo.add.style  		  = UI_CENTER|UI_SMALLFONT;
	s_serverinfo.add.color			  =	color_red;
	if( trap_Cvar_VariableValue( "sv_running" ) ) {
		s_serverinfo.add.generic.flags |= QMF_GRAYED;
	}

	s_serverinfo.back.generic.type	   = MTYPE_BITMAP;
	s_serverinfo.back.generic.name     = SERVERINFO_BACK0;
	s_serverinfo.back.generic.flags    = QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	s_serverinfo.back.generic.callback = ServerInfo_Event;
	s_serverinfo.back.generic.id	   = ID_BACK;
	s_serverinfo.back.generic.x		   = 0;
	s_serverinfo.back.generic.y		   = 480-64;
	s_serverinfo.back.width  		   = 128;
	s_serverinfo.back.height  		   = 64;
	s_serverinfo.back.focuspic         = SERVERINFO_BACK1;

	trap_GetConfigString( CS_SERVERINFO, s_serverinfo.info, MAX_INFO_STRING );

	max = 0;
	s = s_serverinfo.info;
	do {
		s = Info_NextPair( s, key.data(), value.data() );
		if ( key[0] == '\0' ) {
			break;
		}
		len = static_cast<int>( std::strlen( key.data() ) );
		if ( len > max )
			max = len;
	} while ( *s != '\0' );

	s_serverinfo.list.generic.type		= MTYPE_SCROLLLIST;
	s_serverinfo.list.generic.flags		= QMF_PULSEIFFOCUS;
	s_serverinfo.list.generic.id		= 123;
	s_serverinfo.list.generic.x			= 120;
	s_serverinfo.list.generic.y			= 132;
	s_serverinfo.list.width				= INFO_LINE_WIDTH;
	s_serverinfo.list.height			= 14;
	s_serverinfo.list.columns			= 1;
	s_serverinfo.list.scroll			= 1;

	s_serverinfo.list.itemnames = itemnames.data();

	s_serverinfo.list.numitems = 0;
	s = s_serverinfo.info;
	do {
		s = Info_NextPair( s, key.data(), value.data() );
		if ( key[0] == '\0' )
			break;

		BuildServerInfoLine( show_info[s_serverinfo.list.numitems], key.data(), value.data(), max );
		s_serverinfo.list.itemnames[s_serverinfo.list.numitems] = show_info[s_serverinfo.list.numitems].data();
		s_serverinfo.list.numitems++;
		if ( s_serverinfo.list.numitems >= MAX_INFO_LINES )
			break;
	} while ( *s != '\0' );

	Menu_AddItem( &s_serverinfo.menu, (void*) &s_serverinfo.banner );
	Menu_AddItem( &s_serverinfo.menu, (void*) &s_serverinfo.framel );
	Menu_AddItem( &s_serverinfo.menu, (void*) &s_serverinfo.framer );
	Menu_AddItem( &s_serverinfo.menu, (void*) &s_serverinfo.add );
	Menu_AddItem( &s_serverinfo.menu, (void*) &s_serverinfo.back );
	Menu_AddItem( &s_serverinfo.menu, (void*) &s_serverinfo.list );

	UI_PushMenu( &s_serverinfo.menu );
}
