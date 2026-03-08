// Copyright (C) 1999-2000 Id Software, Inc.
//
/*
=======================================================================

SETUP MENU

=======================================================================
*/


#include "ui_local.h"


#define SETUP_MENU_VERTICAL_SPACING		34

#define ART_BACK0		"menu/art/back_0"
#define ART_BACK1		"menu/art/back_1"	
#define ART_FRAMEL		"menu/art/frame2_l"
#define ART_FRAMER		"menu/art/frame1_r"

#define ID_CUSTOMIZEPLAYER		10
#define ID_CUSTOMIZECONTROLS	11
#define ID_SYSTEMCONFIG			12
#define ID_GAME					13
#define ID_CDKEY				14
#define ID_LOAD					15
#define ID_SAVE					16
#define ID_DEFAULTS				17
#define ID_BACK					18


typedef struct {
	menuframework_s	menu;

	menutext_s		banner;
	menubitmap_s	framel;
	menubitmap_s	framer;
	menutext_s		setupplayer;
	menutext_s		setupcontrols;
	menutext_s		setupsystem;
	menutext_s		game;
	menutext_s		cdkey;
//	menutext_s		load;
//	menutext_s		save;
	menutext_s		defaults;
	menubitmap_s	back;
} setupMenuInfo_t;

static setupMenuInfo_t	setupMenuInfo;

static void UI_SetupMenu_Event( void *ptr, int event );

namespace {

void InitializeSetupTextItem( menutext_s &item, const int id, const int y, const char *const label ) {
	item.generic.type = MTYPE_PTEXT;
	item.generic.flags = QMF_CENTER_JUSTIFY | QMF_PULSEIFFOCUS;
	item.generic.x = 320;
	item.generic.y = y;
	item.generic.id = id;
	item.generic.callback = UI_SetupMenu_Event;
	item.string = const_cast<char *>( label );
	item.color = color_red;
	item.style = UI_CENTER;
}

qboolean ShouldShowSetupDefaults() {
	return !trap_Cvar_VariableValue( "cl_paused" );
}

}


/*
=================
Setup_ResetDefaults_Action
=================
*/
static void Setup_ResetDefaults_Action( qboolean result ) {
	if( !result ) {
		return;
	}
	trap_Cmd_ExecuteText( EXEC_APPEND, "exec default.cfg\n");
	trap_Cmd_ExecuteText( EXEC_APPEND, "cvar_restart\n");
	trap_Cmd_ExecuteText( EXEC_APPEND, "vid_restart\n" );
}


/*
=================
Setup_ResetDefaults_Draw
=================
*/
static void Setup_ResetDefaults_Draw( void ) {
	UI_DrawProportionalString( SCREEN_WIDTH/2, 356 + PROP_HEIGHT * 0, "WARNING: This will reset *ALL*", UI_CENTER|UI_SMALLFONT, color_yellow );
	UI_DrawProportionalString( SCREEN_WIDTH/2, 356 + PROP_HEIGHT * 1, "options to their default values.", UI_CENTER|UI_SMALLFONT, color_yellow );
}


/*
===============
UI_SetupMenu_Event
===============
*/
static void UI_SetupMenu_Event( void *ptr, int event ) {
	if( event != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	case ID_CUSTOMIZEPLAYER:
		UI_PlayerSettingsMenu();
		break;

	case ID_CUSTOMIZECONTROLS:
		UI_ControlsMenu();
		break;

	case ID_SYSTEMCONFIG:
		UI_GraphicsOptionsMenu();
		break;

	case ID_GAME:
		UI_PreferencesMenu();
		break;

	case ID_CDKEY:
		UI_CDKeyMenu();
		break;

//	case ID_LOAD:
//		UI_LoadConfigMenu();
//		break;

//	case ID_SAVE:
//		UI_SaveConfigMenu();
//		break;

	case ID_DEFAULTS:
		UI_ConfirmMenu( "SET TO DEFAULTS?", Setup_ResetDefaults_Draw, Setup_ResetDefaults_Action );
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
===============
UI_SetupMenu_Init
===============
*/
static void UI_SetupMenu_Init( void ) {
	int				y;

	UI_SetupMenu_Cache();

	setupMenuInfo = {};
	setupMenuInfo.menu.wrapAround = qtrue;
	setupMenuInfo.menu.fullscreen = qtrue;

	setupMenuInfo.banner.generic.type				= MTYPE_BTEXT;
	setupMenuInfo.banner.generic.x					= 320;
	setupMenuInfo.banner.generic.y					= 16;
	setupMenuInfo.banner.string						= "SETUP";
	setupMenuInfo.banner.color						= color_white;
	setupMenuInfo.banner.style						= UI_CENTER;

	setupMenuInfo.framel.generic.type				= MTYPE_BITMAP;
	setupMenuInfo.framel.generic.name				= ART_FRAMEL;
	setupMenuInfo.framel.generic.flags				= QMF_INACTIVE;
	setupMenuInfo.framel.generic.x					= 0;  
	setupMenuInfo.framel.generic.y					= 78;
	setupMenuInfo.framel.width  					= 256;
	setupMenuInfo.framel.height  					= 329;

	setupMenuInfo.framer.generic.type				= MTYPE_BITMAP;
	setupMenuInfo.framer.generic.name				= ART_FRAMER;
	setupMenuInfo.framer.generic.flags				= QMF_INACTIVE;
	setupMenuInfo.framer.generic.x					= 376;
	setupMenuInfo.framer.generic.y					= 76;
	setupMenuInfo.framer.width  					= 256;
	setupMenuInfo.framer.height  					= 334;

	y = 134;
	InitializeSetupTextItem( setupMenuInfo.setupplayer, ID_CUSTOMIZEPLAYER, y, "PLAYER" );

	y += SETUP_MENU_VERTICAL_SPACING;
	InitializeSetupTextItem( setupMenuInfo.setupcontrols, ID_CUSTOMIZECONTROLS, y, "CONTROLS" );

	y += SETUP_MENU_VERTICAL_SPACING;
	InitializeSetupTextItem( setupMenuInfo.setupsystem, ID_SYSTEMCONFIG, y, "SYSTEM" );

	y += SETUP_MENU_VERTICAL_SPACING;
	InitializeSetupTextItem( setupMenuInfo.game, ID_GAME, y, "GAME OPTIONS" );

	y += SETUP_MENU_VERTICAL_SPACING;
	InitializeSetupTextItem( setupMenuInfo.cdkey, ID_CDKEY, y, "CD Key" );

	if( ShouldShowSetupDefaults() ) {
#if 0
		y += SETUP_MENU_VERTICAL_SPACING;
		setupMenuInfo.load.generic.type					= MTYPE_PTEXT;
		setupMenuInfo.load.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
		setupMenuInfo.load.generic.x					= 320;
		setupMenuInfo.load.generic.y					= y;
		setupMenuInfo.load.generic.id					= ID_LOAD;
		setupMenuInfo.load.generic.callback				= UI_SetupMenu_Event; 
		setupMenuInfo.load.string						= "LOAD";
		setupMenuInfo.load.color						= color_red;
		setupMenuInfo.load.style						= UI_CENTER;

		y += SETUP_MENU_VERTICAL_SPACING;
		setupMenuInfo.save.generic.type					= MTYPE_PTEXT;
		setupMenuInfo.save.generic.flags				= QMF_CENTER_JUSTIFY|QMF_PULSEIFFOCUS;
		setupMenuInfo.save.generic.x					= 320;
		setupMenuInfo.save.generic.y					= y;
		setupMenuInfo.save.generic.id					= ID_SAVE;
		setupMenuInfo.save.generic.callback				= UI_SetupMenu_Event; 
		setupMenuInfo.save.string						= "SAVE";
		setupMenuInfo.save.color						= color_red;
		setupMenuInfo.save.style						= UI_CENTER;
#endif

		y += SETUP_MENU_VERTICAL_SPACING;
		InitializeSetupTextItem( setupMenuInfo.defaults, ID_DEFAULTS, y, "DEFAULTS" );
	}

	setupMenuInfo.back.generic.type					= MTYPE_BITMAP;
	setupMenuInfo.back.generic.name					= ART_BACK0;
	setupMenuInfo.back.generic.flags				= QMF_LEFT_JUSTIFY|QMF_PULSEIFFOCUS;
	setupMenuInfo.back.generic.id					= ID_BACK;
	setupMenuInfo.back.generic.callback				= UI_SetupMenu_Event;
	setupMenuInfo.back.generic.x					= 0;
	setupMenuInfo.back.generic.y					= 480-64;
	setupMenuInfo.back.width						= 128;
	setupMenuInfo.back.height						= 64;
	setupMenuInfo.back.focuspic						= ART_BACK1;

	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.banner );
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.framel );
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.framer );
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.setupplayer );
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.setupcontrols );
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.setupsystem );
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.game );
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.cdkey );
//	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.load );
//	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.save );
	if( ShouldShowSetupDefaults() ) {
		Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.defaults );
	}
	Menu_AddItem( &setupMenuInfo.menu, &setupMenuInfo.back );
}


/*
=================
UI_SetupMenu_Cache
=================
*/
void UI_SetupMenu_Cache( void ) {
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );
	trap_R_RegisterShaderNoMip( ART_FRAMEL );
	trap_R_RegisterShaderNoMip( ART_FRAMER );
}


/*
===============
UI_SetupMenu
===============
*/
void UI_SetupMenu( void ) {
	UI_SetupMenu_Init();
	UI_PushMenu( &setupMenuInfo.menu );
}
