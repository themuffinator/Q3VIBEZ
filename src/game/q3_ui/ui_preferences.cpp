// Copyright (C) 1999-2000 Id Software, Inc.
//
/*
=======================================================================

GAME OPTIONS MENU

=======================================================================
*/


#include "ui_local.h"

#include <array>


#define ART_FRAMEL				"menu/art/frame2_l"
#define ART_FRAMER				"menu/art/frame1_r"
#define ART_BACK0				"menu/art/back_0"
#define ART_BACK1				"menu/art/back_1"

#define PREFERENCES_X_POS		360

#define ID_CROSSHAIR			127
#define ID_SIMPLEITEMS			128
#define ID_HIGHQUALITYSKY		129
#define ID_EJECTINGBRASS		130
#define ID_WALLMARKS			131
#define ID_DYNAMICLIGHTS		132
#define ID_IDENTIFYTARGET		133
#define ID_SYNCEVERYFRAME		134
#define ID_FORCEMODEL			135
#define ID_DRAWTEAMOVERLAY		136
#define ID_ALLOWDOWNLOAD			137
#define ID_BACK					138

#define	NUM_CROSSHAIRS			10


typedef struct {
	menuframework_s		menu;

	menutext_s			banner;
	menubitmap_s		framel;
	menubitmap_s		framer;

	menulist_s			crosshair;
	menuradiobutton_s	simpleitems;
	menuradiobutton_s	brass;
	menuradiobutton_s	wallmarks;
	menuradiobutton_s	dynamiclights;
	menuradiobutton_s	identifytarget;
	menuradiobutton_s	highqualitysky;
	menuradiobutton_s	synceveryframe;
	menuradiobutton_s	forcemodel;
	menulist_s			drawteamoverlay;
	menuradiobutton_s	allowdownload;
	menubitmap_s		back;

	std::array<qhandle_t, NUM_CROSSHAIRS> crosshairShader;
} preferences_t;

static preferences_t s_preferences;

static std::array<const char *, 5> teamoverlay_names = {
	"off",
	"upper right",
	"lower right",
	"lower left",
	nullptr
};

static void Preferences_Event( void* ptr, int notification );
static void Crosshair_Draw( void *self );

static void InitializeCrosshairControl( menulist_s &crosshair, const int y ) {
	crosshair.generic.type		= MTYPE_TEXT;
	crosshair.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT | QMF_NODEFAULTINIT | QMF_OWNERDRAW;
	crosshair.generic.x			= PREFERENCES_X_POS;
	crosshair.generic.y			= y;
	crosshair.generic.name		= "Crosshair:";
	crosshair.generic.callback	= Preferences_Event;
	crosshair.generic.ownerdraw	= Crosshair_Draw;
	crosshair.generic.id		= ID_CROSSHAIR;
	crosshair.generic.top		= y - 4;
	crosshair.generic.bottom	= y + 20;
	crosshair.generic.left		= PREFERENCES_X_POS - ( ( strlen( crosshair.generic.name ) + 1 ) * SMALLCHAR_WIDTH );
	crosshair.generic.right		= PREFERENCES_X_POS + 48;
}

static void InitializePreferenceToggle( menuradiobutton_s &button, const char *name, const int id, const int y ) {
	button.generic.type		= MTYPE_RADIOBUTTON;
	button.generic.name		= name;
	button.generic.flags	= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	button.generic.callback	= Preferences_Event;
	button.generic.id		= id;
	button.generic.x		= PREFERENCES_X_POS;
	button.generic.y		= y;
}

static void InitializePreferenceList( menulist_s &list, const char *name, const int id, const int y, const char **itemNames ) {
	list.generic.type		= MTYPE_SPINCONTROL;
	list.generic.name		= name;
	list.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	list.generic.callback	= Preferences_Event;
	list.generic.id			= id;
	list.generic.x			= PREFERENCES_X_POS;
	list.generic.y			= y;
	list.itemnames			= itemNames;
}

static void InitializeBackButton( menubitmap_s &button ) {
	button.generic.type		= MTYPE_BITMAP;
	button.generic.name		= ART_BACK0;
	button.generic.flags	= QMF_LEFT_JUSTIFY | QMF_PULSEIFFOCUS;
	button.generic.callback	= Preferences_Event;
	button.generic.id		= ID_BACK;
	button.generic.x		= 0;
	button.generic.y		= 480 - 64;
	button.width			= 128;
	button.height			= 64;
	button.focuspic		= ART_BACK1;
}

static void Preferences_SetMenuItems( void ) {
	s_preferences.crosshair.curvalue		= (int)trap_Cvar_VariableValue( "cg_drawCrosshair" ) % NUM_CROSSHAIRS;
	s_preferences.simpleitems.curvalue		= trap_Cvar_VariableValue( "cg_simpleItems" ) != 0;
	s_preferences.brass.curvalue			= trap_Cvar_VariableValue( "cg_brassTime" ) != 0;
	s_preferences.wallmarks.curvalue		= trap_Cvar_VariableValue( "cg_marks" ) != 0;
	s_preferences.identifytarget.curvalue	= trap_Cvar_VariableValue( "cg_drawCrosshairNames" ) != 0;
	s_preferences.dynamiclights.curvalue	= trap_Cvar_VariableValue( "r_dynamiclight" ) != 0;
	s_preferences.highqualitysky.curvalue	= trap_Cvar_VariableValue ( "r_fastsky" ) == 0;
	s_preferences.synceveryframe.curvalue	= trap_Cvar_VariableValue( "r_swapinterval" ) != 0;
	s_preferences.forcemodel.curvalue		= trap_Cvar_VariableValue( "cg_forcemodel" ) != 0;
	s_preferences.drawteamoverlay.curvalue	= Com_Clamp( 0, 3, trap_Cvar_VariableValue( "cg_drawTeamOverlay" ) );
	s_preferences.allowdownload.curvalue	= trap_Cvar_VariableValue( "cl_allowDownload" ) != 0;
}


static void Preferences_Event( void* ptr, int notification ) {
	if( notification != QM_ACTIVATED ) {
		return;
	}

	switch( ((menucommon_s*)ptr)->id ) {
	case ID_CROSSHAIR:
		s_preferences.crosshair.curvalue++;
		if( s_preferences.crosshair.curvalue == NUM_CROSSHAIRS ) {
			s_preferences.crosshair.curvalue = 0;
		}
		trap_Cvar_SetValue( "cg_drawCrosshair", s_preferences.crosshair.curvalue );
		break;

	case ID_SIMPLEITEMS:
		trap_Cvar_SetValue( "cg_simpleItems", s_preferences.simpleitems.curvalue );
		break;

	case ID_HIGHQUALITYSKY:
		trap_Cvar_SetValue( "r_fastsky", !s_preferences.highqualitysky.curvalue );
		break;

	case ID_EJECTINGBRASS:
		if ( s_preferences.brass.curvalue )
			trap_Cvar_Reset( "cg_brassTime" );
		else
			trap_Cvar_SetValue( "cg_brassTime", 0 );
		break;

	case ID_WALLMARKS:
		trap_Cvar_SetValue( "cg_marks", s_preferences.wallmarks.curvalue );
		break;

	case ID_DYNAMICLIGHTS:
		trap_Cvar_SetValue( "r_dynamiclight", s_preferences.dynamiclights.curvalue );
		break;		

	case ID_IDENTIFYTARGET:
		trap_Cvar_SetValue( "cg_drawCrosshairNames", s_preferences.identifytarget.curvalue );
		break;

	case ID_SYNCEVERYFRAME:
		trap_Cvar_SetValue( "r_swapinterval", s_preferences.synceveryframe.curvalue );
		break;

	case ID_FORCEMODEL:
		trap_Cvar_SetValue( "cg_forcemodel", s_preferences.forcemodel.curvalue );
		break;

	case ID_DRAWTEAMOVERLAY:
		trap_Cvar_SetValue( "cg_drawTeamOverlay", s_preferences.drawteamoverlay.curvalue );
		break;

	case ID_ALLOWDOWNLOAD:
		trap_Cvar_SetValue( "cl_allowDownload", s_preferences.allowdownload.curvalue );
		trap_Cvar_SetValue( "sv_allowDownload", s_preferences.allowdownload.curvalue );
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
=================
Crosshair_Draw
=================
*/
static void Crosshair_Draw( void *self ) {
	menulist_s	*s;
	float		*color;
	int			x, y;
	int			style;
	qboolean	focus;

	s = (menulist_s *)self;
	x = s->generic.x;
	y =	s->generic.y;

	style = UI_SMALLFONT;
	focus = (s->generic.parent->cursor == s->generic.menuPosition);

	if ( s->generic.flags & QMF_GRAYED )
		color = text_color_disabled;
	else if ( focus )
	{
		color = text_color_highlight;
		style |= UI_PULSE;
	}
	else if ( s->generic.flags & QMF_BLINK )
	{
		color = text_color_highlight;
		style |= UI_BLINK;
	}
	else
		color = text_color_normal;

	if ( focus )
	{
		// draw cursor
		UI_FillRect( s->generic.left, s->generic.top, s->generic.right-s->generic.left+1, s->generic.bottom-s->generic.top+1, listbar_color ); 
		UI_DrawChar( x, y, 13, UI_CENTER|UI_BLINK|UI_SMALLFONT, color);
	}

	UI_DrawString( x - SMALLCHAR_WIDTH, y, s->generic.name, style|UI_RIGHT, color );
	if( !s->curvalue ) {
		return;
	}
	UI_DrawHandlePic( x + SMALLCHAR_WIDTH, y - 4, 24, 24, s_preferences.crosshairShader[s->curvalue] );
}


static void Preferences_MenuInit( void ) {
	int				y;

	s_preferences = {};

	Preferences_Cache();

	s_preferences.menu.wrapAround = qtrue;
	s_preferences.menu.fullscreen = qtrue;

	s_preferences.banner.generic.type  = MTYPE_BTEXT;
	s_preferences.banner.generic.x	   = 320;
	s_preferences.banner.generic.y	   = 16;
	s_preferences.banner.string		   = "GAME OPTIONS";
	s_preferences.banner.color         = color_white;
	s_preferences.banner.style         = UI_CENTER;

	s_preferences.framel.generic.type  = MTYPE_BITMAP;
	s_preferences.framel.generic.name  = ART_FRAMEL;
	s_preferences.framel.generic.flags = QMF_INACTIVE;
	s_preferences.framel.generic.x	   = 0;
	s_preferences.framel.generic.y	   = 78;
	s_preferences.framel.width  	   = 256;
	s_preferences.framel.height  	   = 329;

	s_preferences.framer.generic.type  = MTYPE_BITMAP;
	s_preferences.framer.generic.name  = ART_FRAMER;
	s_preferences.framer.generic.flags = QMF_INACTIVE;
	s_preferences.framer.generic.x	   = 376;
	s_preferences.framer.generic.y	   = 76;
	s_preferences.framer.width  	   = 256;
	s_preferences.framer.height  	   = 334;

	y = 144;
	InitializeCrosshairControl( s_preferences.crosshair, y );

	y += BIGCHAR_HEIGHT+2+4;
	InitializePreferenceToggle( s_preferences.simpleitems, "Simple Items:", ID_SIMPLEITEMS, y );

	y += BIGCHAR_HEIGHT;
	InitializePreferenceToggle( s_preferences.wallmarks, "Marks on Walls:", ID_WALLMARKS, y );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceToggle( s_preferences.brass, "Ejecting Brass:", ID_EJECTINGBRASS, y );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceToggle( s_preferences.dynamiclights, "Dynamic Lights:", ID_DYNAMICLIGHTS, y );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceToggle( s_preferences.identifytarget, "Identify Target:", ID_IDENTIFYTARGET, y );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceToggle( s_preferences.highqualitysky, "High Quality Sky:", ID_HIGHQUALITYSKY, y );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceToggle( s_preferences.synceveryframe, "Sync Every Frame:", ID_SYNCEVERYFRAME, y );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceToggle( s_preferences.forcemodel, "Force Player Models:", ID_FORCEMODEL, y );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceList( s_preferences.drawteamoverlay, "Draw Team Overlay:", ID_DRAWTEAMOVERLAY, y, teamoverlay_names.data() );

	y += BIGCHAR_HEIGHT+2;
	InitializePreferenceToggle( s_preferences.allowdownload, "Automatic Downloading:", ID_ALLOWDOWNLOAD, y );

	y += BIGCHAR_HEIGHT+2;
	InitializeBackButton( s_preferences.back );

	Menu_AddItem( &s_preferences.menu, &s_preferences.banner );
	Menu_AddItem( &s_preferences.menu, &s_preferences.framel );
	Menu_AddItem( &s_preferences.menu, &s_preferences.framer );

	Menu_AddItem( &s_preferences.menu, &s_preferences.crosshair );
	Menu_AddItem( &s_preferences.menu, &s_preferences.simpleitems );
	Menu_AddItem( &s_preferences.menu, &s_preferences.wallmarks );
	Menu_AddItem( &s_preferences.menu, &s_preferences.brass );
	Menu_AddItem( &s_preferences.menu, &s_preferences.dynamiclights );
	Menu_AddItem( &s_preferences.menu, &s_preferences.identifytarget );
	Menu_AddItem( &s_preferences.menu, &s_preferences.highqualitysky );
	Menu_AddItem( &s_preferences.menu, &s_preferences.synceveryframe );
	Menu_AddItem( &s_preferences.menu, &s_preferences.forcemodel );
	Menu_AddItem( &s_preferences.menu, &s_preferences.drawteamoverlay );
	Menu_AddItem( &s_preferences.menu, &s_preferences.allowdownload );

	Menu_AddItem( &s_preferences.menu, &s_preferences.back );

	Preferences_SetMenuItems();
}


/*
===============
Preferences_Cache
===============
*/
void Preferences_Cache( void ) {
	int		n;

	trap_R_RegisterShaderNoMip( ART_FRAMEL );
	trap_R_RegisterShaderNoMip( ART_FRAMER );
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );
	for( n = 0; n < NUM_CROSSHAIRS; n++ ) {
		s_preferences.crosshairShader[n] = trap_R_RegisterShaderNoMip( va("gfx/2d/crosshair%c", 'a' + n ) );
	}
}


/*
===============
UI_PreferencesMenu
===============
*/
void UI_PreferencesMenu( void ) {
	Preferences_MenuInit();
	UI_PushMenu( &s_preferences.menu );
}
