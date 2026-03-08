// Copyright (C) 1999-2000 Id Software, Inc.
//
//
// ui_team.c
//

#include "ui_local.h"

#include <array>


#define TEAMMAIN_FRAME	"menu/art/cut_frame"

#define INGAME_TEAM_VERTICAL_SPACING 23

#define ID_JOINRED		100
#define ID_JOINBLUE		101
#define ID_JOINGAME		102
#define ID_SPECTATE		103


typedef struct
{
	menuframework_s	menu;
	menubitmap_s	frame;
	menutext_s		joinred;
	menutext_s		joinblue;
	menutext_s		joingame;
	menutext_s		spectate;
} teammain_t;

static teammain_t	s_teammain;

static void TeamMain_MenuEvent( void* ptr, int event );

namespace {

std::array<char, BIG_INFO_STRING> ServerInfoBuffer() {
	return {};
}

gametype_t TeamMenuGameType() {
	auto info = ServerInfoBuffer();
	trap_GetConfigString( CS_SERVERINFO, info.data(), static_cast<int>( info.size() ) );
	return static_cast<gametype_t>( atoi( Info_ValueForKey( info.data(), "g_gametype" ) ) );
}

void InitializeTeamMenuEntry( menutext_s &item, const int id, const int y, const char *const label ) {
	item.generic.type = MTYPE_PTEXT;
	item.generic.flags = QMF_CENTER_JUSTIFY | QMF_PULSEIFFOCUS;
	item.generic.id = id;
	item.generic.callback = TeamMain_MenuEvent;
	item.generic.x = 320;
	item.generic.y = y;
	item.string = const_cast<char *>( label );
	item.style = UI_CENTER | UI_SMALLFONT;
	item.color = colorRed;
}

}

/*
===============
TeamMain_MenuEvent
===============
*/
static void TeamMain_MenuEvent( void* ptr, int event ) {
	if( event != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	case ID_JOINRED:
		trap_Cmd_ExecuteText( EXEC_APPEND, "cmd team red\n" );
		UI_ForceMenuOff();
		break;

	case ID_JOINBLUE:
		trap_Cmd_ExecuteText( EXEC_APPEND, "cmd team blue\n" );
		UI_ForceMenuOff();
		break;

	case ID_JOINGAME:
		trap_Cmd_ExecuteText( EXEC_APPEND, "cmd team free\n" );
		UI_ForceMenuOff();
		break;

	case ID_SPECTATE:
		trap_Cmd_ExecuteText( EXEC_APPEND, "cmd team spectator\n" );
		UI_ForceMenuOff();
		break;
	}
}


/*
===============
TeamMain_MenuInit
===============
*/
void TeamMain_MenuInit( void ) {
	const gametype_t gametype = TeamMenuGameType();
	int		y;

	s_teammain = {};

	TeamMain_Cache();

	s_teammain.menu.wrapAround = qtrue;
	s_teammain.menu.fullscreen = qfalse;

	s_teammain.frame.generic.type   = MTYPE_BITMAP;
	s_teammain.frame.generic.flags	= QMF_INACTIVE;
	s_teammain.frame.generic.name   = TEAMMAIN_FRAME;
	s_teammain.frame.width			= 300;
	s_teammain.frame.height			= 225;
	s_teammain.frame.generic.x		= (640-s_teammain.frame.width)/2;
	s_teammain.frame.generic.y		= (480-s_teammain.frame.height)/2;

	y = 195; // 188

	InitializeTeamMenuEntry( s_teammain.joinred, ID_JOINRED, y, "JOIN RED" );
	y += INGAME_TEAM_VERTICAL_SPACING;

	InitializeTeamMenuEntry( s_teammain.joinblue, ID_JOINBLUE, y, "JOIN BLUE" );
	y += INGAME_TEAM_VERTICAL_SPACING;

	InitializeTeamMenuEntry( s_teammain.joingame, ID_JOINGAME, y, "JOIN GAME" );
	y += INGAME_TEAM_VERTICAL_SPACING;

	InitializeTeamMenuEntry( s_teammain.spectate, ID_SPECTATE, y, "SPECTATE" );
	y += INGAME_TEAM_VERTICAL_SPACING;
			      
	// set initial states
	switch( gametype ) {
	case GT_SINGLE_PLAYER:
	case GT_FFA:
	case GT_TOURNAMENT:
		s_teammain.joinred.generic.flags  |= QMF_GRAYED;
		s_teammain.joinblue.generic.flags |= QMF_GRAYED;
		break;

	default:
	case GT_TEAM:
	case GT_CTF:
		s_teammain.joingame.generic.flags |= QMF_GRAYED;
		break;
	}

	Menu_AddItem( &s_teammain.menu, (void*) &s_teammain.frame );
	Menu_AddItem( &s_teammain.menu, (void*) &s_teammain.joinred );
	Menu_AddItem( &s_teammain.menu, (void*) &s_teammain.joinblue );
	Menu_AddItem( &s_teammain.menu, (void*) &s_teammain.joingame );
	Menu_AddItem( &s_teammain.menu, (void*) &s_teammain.spectate );
}


/*
===============
TeamMain_Cache
===============
*/
void TeamMain_Cache( void ) {
	trap_R_RegisterShaderNoMip( TEAMMAIN_FRAME );
}


/*
===============
UI_TeamMainMenu
===============
*/
void UI_TeamMainMenu( void ) {
	TeamMain_MenuInit();
	UI_PushMenu ( &s_teammain.menu );
}
