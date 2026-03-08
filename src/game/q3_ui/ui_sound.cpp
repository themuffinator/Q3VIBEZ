// Copyright (C) 1999-2000 Id Software, Inc.
//
/*
=======================================================================

SOUND OPTIONS MENU

=======================================================================
*/

#include "ui_local.h"

#include <array>


#define ART_FRAMEL			"menu/art/frame2_l"
#define ART_FRAMER			"menu/art/frame1_r"
#define ART_BACK0			"menu/art/back_0"
#define ART_BACK1			"menu/art/back_1"

#define ID_GRAPHICS				10
#define ID_DISPLAY				11
#define ID_SOUND				12
#define ID_NETWORK				13
#define ID_EFFECTSVOLUME		14
#define ID_MUSICVOLUME			15
#define ID_QUALITY				16
#define ID_BACKEND				17
#define ID_HRTF					18
#define ID_REVERB				19
#define ID_REVERB_PRESET		20
#define ID_REVERB_LEVEL			21
#define ID_OCCLUSION			22
#define ID_OCCLUSION_STRENGTH	23
#define ID_BACK					24

static void UI_SoundOptionsMenu_Event( void *ptr, int event );

namespace {

constexpr std::array<const char *, 3> QualityItems = {
	"Low",
	"High",
	nullptr
};

constexpr std::array<const char *, 3> BackendItems = {
	"Native",
	"OpenAL",
	nullptr
};

constexpr std::array<const char *, 3> ToggleItems = {
	"Off",
	"On",
	nullptr
};

constexpr std::array<const char *, 38> ReverbPresetItems = {
	"Auto",
	"Generic",
	"Underwater",
	"Abandoned",
	"Alley",
	"Arena",
	"Auditorium",
	"Bathroom",
	"Carpeted Hallway",
	"Cave",
	"Chapel",
	"City",
	"City Streets",
	"Concert Hall",
	"Dizzy",
	"Drugged",
	"Dusty Room",
	"Forest",
	"Hallway",
	"Hangar",
	"Library",
	"Living Room",
	"Mountains",
	"Museum",
	"Padded Cell",
	"Parking Lot",
	"Plain",
	"Psychotic",
	"Quarry",
	"Room",
	"Sewer Pipe",
	"Small Water Room",
	"Stone Corridor",
	"Stone Room",
	"Subway",
	"Underpass",
	nullptr
};

void ApplySoundQuality( const int qualityIndex )
{
	if ( qualityIndex != 0 ) {
		trap_Cvar_SetValue( "s_khz", 22 );
		trap_Cvar_SetValue( "s_compression", 0 );
		return;
	}

	trap_Cvar_SetValue( "s_khz", 11 );
	trap_Cvar_SetValue( "s_compression", 1 );
}

const char *BackendValueName( const int backendIndex )
{
	return backendIndex == 1 ? "openal" : "native";
}

int CurrentBackendIndex( void )
{
	char backend[MAX_CVAR_VALUE_STRING];

	trap_Cvar_VariableStringBuffer( "s_backend", backend, sizeof( backend ) );
	return Q_stricmp( backend, "openal" ) == 0 ? 1 : 0;
}

qboolean OpenALAvailable( void )
{
	return trap_Cvar_VariableValue( "s_openalAvailable" ) != 0.0f;
}

qboolean OpenALHrtfAvailable( void )
{
	return trap_Cvar_VariableValue( "s_openalHrtfAvailable" ) != 0.0f;
}

qboolean OpenALEfxAvailable( void )
{
	return trap_Cvar_VariableValue( "s_openalEfxAvailable" ) != 0.0f;
}

void InitializeSoundMenuTextEntry( menutext_s &item, const int id, const int y, const char *const label, const int flags )
{
	item.generic.type = MTYPE_PTEXT;
	item.generic.flags = flags;
	item.generic.id = id;
	item.generic.callback = UI_SoundOptionsMenu_Event;
	item.generic.x = 216;
	item.generic.y = y;
	item.string = const_cast<char *>( label );
	item.style = UI_RIGHT;
	item.color = color_red;
}

void InitializeSoundSpinControl( menulist_s &item, const int id, const int y, const char *const label, const char *const *values )
{
	item.generic.type = MTYPE_SPINCONTROL;
	item.generic.name = label;
	item.generic.flags = QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	item.generic.callback = UI_SoundOptionsMenu_Event;
	item.generic.id = id;
	item.generic.x = 400;
	item.generic.y = y;
	item.itemnames = const_cast<const char **>( values );
}

void InitializeSoundSlider( menuslider_s &item, const int id, const int y, const char *const label, const float minValue, const float maxValue )
{
	item.generic.type = MTYPE_SLIDER;
	item.generic.name = label;
	item.generic.flags = QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	item.generic.callback = UI_SoundOptionsMenu_Event;
	item.generic.id = id;
	item.generic.x = 400;
	item.generic.y = y;
	item.minvalue = minValue;
	item.maxvalue = maxValue;
}

} // namespace

typedef struct {
	menuframework_s		menu;

	menutext_s			banner;
	menubitmap_s		framel;
	menubitmap_s		framer;

	menutext_s			graphics;
	menutext_s			display;
	menutext_s			sound;
	menutext_s			network;

	menuslider_s		sfxvolume;
	menuslider_s		musicvolume;
	menulist_s			quality;
	menulist_s			backend;
	menulist_s			hrtf;
	menulist_s			reverb;
	menulist_s			reverbPreset;
	menuslider_s		reverbLevel;
	menulist_s			occlusion;
	menuslider_s		occlusionStrength;

	menubitmap_s		back;
} soundOptionsInfo_t;

static soundOptionsInfo_t soundOptionsInfo;

static void UI_SoundOptionsMenu_UpdateState( void )
{
	const qboolean openalAvailable = OpenALAvailable();
	const qboolean openalActive = openalAvailable && soundOptionsInfo.backend.curvalue == 1;
	const qboolean nativeActive = !openalActive;
	const qboolean hrtfAvailable = openalActive && OpenALHrtfAvailable();
	const qboolean efxAvailable = openalActive && OpenALEfxAvailable();
	const qboolean reverbEnabled = efxAvailable && soundOptionsInfo.reverb.curvalue != 0;
	const qboolean occlusionEnabled = efxAvailable && soundOptionsInfo.occlusion.curvalue != 0;

	if ( openalAvailable ) {
		soundOptionsInfo.backend.generic.flags &= ~QMF_GRAYED;
	} else {
		soundOptionsInfo.backend.generic.flags |= QMF_GRAYED;
	}

	if ( nativeActive ) {
		soundOptionsInfo.quality.generic.flags &= ~QMF_GRAYED;
	} else {
		soundOptionsInfo.quality.generic.flags |= QMF_GRAYED;
	}

	if ( hrtfAvailable ) {
		soundOptionsInfo.hrtf.generic.flags &= ~QMF_GRAYED;
	} else {
		soundOptionsInfo.hrtf.generic.flags |= QMF_GRAYED;
	}

	if ( efxAvailable ) {
		soundOptionsInfo.reverb.generic.flags &= ~QMF_GRAYED;
		soundOptionsInfo.occlusion.generic.flags &= ~QMF_GRAYED;
	} else {
		soundOptionsInfo.reverb.generic.flags |= QMF_GRAYED;
		soundOptionsInfo.occlusion.generic.flags |= QMF_GRAYED;
	}

	if ( reverbEnabled ) {
		soundOptionsInfo.reverbPreset.generic.flags &= ~QMF_GRAYED;
		soundOptionsInfo.reverbLevel.generic.flags &= ~QMF_GRAYED;
	} else {
		soundOptionsInfo.reverbPreset.generic.flags |= QMF_GRAYED;
		soundOptionsInfo.reverbLevel.generic.flags |= QMF_GRAYED;
	}

	if ( occlusionEnabled ) {
		soundOptionsInfo.occlusionStrength.generic.flags &= ~QMF_GRAYED;
	} else {
		soundOptionsInfo.occlusionStrength.generic.flags |= QMF_GRAYED;
	}
}

/*
=================
UI_SoundOptionsMenu_Event
=================
*/
static void UI_SoundOptionsMenu_Event( void *ptr, int event )
{
	if ( event != QM_ACTIVATED ) {
		return;
	}

	switch ( ( (menucommon_s *)ptr )->id ) {
	case ID_GRAPHICS:
		UI_PopMenu();
		UI_GraphicsOptionsMenu();
		break;

	case ID_DISPLAY:
		UI_PopMenu();
		UI_DisplayOptionsMenu();
		break;

	case ID_SOUND:
		break;

	case ID_NETWORK:
		UI_PopMenu();
		UI_NetworkOptionsMenu();
		break;

	case ID_EFFECTSVOLUME:
		trap_Cvar_SetValue( "s_volume", soundOptionsInfo.sfxvolume.curvalue / 10.0f );
		break;

	case ID_MUSICVOLUME:
		trap_Cvar_SetValue( "s_musicVolume", soundOptionsInfo.musicvolume.curvalue / 10.0f );
		break;

	case ID_QUALITY:
		ApplySoundQuality( soundOptionsInfo.quality.curvalue );
		UI_ForceMenuOff();
		trap_Cmd_ExecuteText( EXEC_APPEND, "snd_restart\n" );
		break;

	case ID_BACKEND:
		trap_Cvar_Set( "s_backend", BackendValueName( soundOptionsInfo.backend.curvalue ) );
		UI_SoundOptionsMenu_UpdateState();
		UI_ForceMenuOff();
		trap_Cmd_ExecuteText( EXEC_APPEND, "snd_restart\n" );
		break;

	case ID_HRTF:
		trap_Cvar_SetValue( "s_openalHrtf", soundOptionsInfo.hrtf.curvalue );
		UI_ForceMenuOff();
		trap_Cmd_ExecuteText( EXEC_APPEND, "snd_restart\n" );
		break;

	case ID_REVERB:
		trap_Cvar_SetValue( "s_openalReverb", soundOptionsInfo.reverb.curvalue );
		UI_SoundOptionsMenu_UpdateState();
		break;

	case ID_REVERB_PRESET:
		trap_Cvar_SetValue( "s_openalReverbPreset", soundOptionsInfo.reverbPreset.curvalue - 1 );
		break;

	case ID_REVERB_LEVEL:
		trap_Cvar_SetValue( "s_openalReverbLevel", soundOptionsInfo.reverbLevel.curvalue / 10.0f );
		break;

	case ID_OCCLUSION:
		trap_Cvar_SetValue( "s_openalOcclusion", soundOptionsInfo.occlusion.curvalue );
		UI_SoundOptionsMenu_UpdateState();
		break;

	case ID_OCCLUSION_STRENGTH:
		trap_Cvar_SetValue( "s_openalOcclusionStrength", soundOptionsInfo.occlusionStrength.curvalue / 10.0f );
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
===============
UI_SoundOptionsMenu_Init
===============
*/
static void UI_SoundOptionsMenu_Init( void )
{
	int y;
	const int lineHeight = BIGCHAR_HEIGHT + 2;

	soundOptionsInfo = {};

	UI_SoundOptionsMenu_Cache();
	soundOptionsInfo.menu.wrapAround = qtrue;
	soundOptionsInfo.menu.fullscreen = qtrue;

	soundOptionsInfo.banner.generic.type		= MTYPE_BTEXT;
	soundOptionsInfo.banner.generic.flags		= QMF_CENTER_JUSTIFY;
	soundOptionsInfo.banner.generic.x			= 320;
	soundOptionsInfo.banner.generic.y			= 16;
	soundOptionsInfo.banner.string				= "SYSTEM SETUP";
	soundOptionsInfo.banner.color				= color_white;
	soundOptionsInfo.banner.style				= UI_CENTER;

	soundOptionsInfo.framel.generic.type		= MTYPE_BITMAP;
	soundOptionsInfo.framel.generic.name		= ART_FRAMEL;
	soundOptionsInfo.framel.generic.flags		= QMF_INACTIVE;
	soundOptionsInfo.framel.generic.x			= 0;
	soundOptionsInfo.framel.generic.y			= 78;
	soundOptionsInfo.framel.width				= 256;
	soundOptionsInfo.framel.height				= 329;

	soundOptionsInfo.framer.generic.type		= MTYPE_BITMAP;
	soundOptionsInfo.framer.generic.name		= ART_FRAMER;
	soundOptionsInfo.framer.generic.flags		= QMF_INACTIVE;
	soundOptionsInfo.framer.generic.x			= 376;
	soundOptionsInfo.framer.generic.y			= 76;
	soundOptionsInfo.framer.width				= 256;
	soundOptionsInfo.framer.height				= 334;

	InitializeSoundMenuTextEntry( soundOptionsInfo.graphics, ID_GRAPHICS, 240 - 2 * PROP_HEIGHT, "GRAPHICS", QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS );
	InitializeSoundMenuTextEntry( soundOptionsInfo.display, ID_DISPLAY, 240 - PROP_HEIGHT, "DISPLAY", QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS );
	InitializeSoundMenuTextEntry( soundOptionsInfo.sound, ID_SOUND, 240, "SOUND", QMF_RIGHT_JUSTIFY );
	InitializeSoundMenuTextEntry( soundOptionsInfo.network, ID_NETWORK, 240 + PROP_HEIGHT, "NETWORK", QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS );

	y = 150;
	InitializeSoundSlider( soundOptionsInfo.sfxvolume, ID_EFFECTSVOLUME, y, "Effects Volume:", 0, 10 );
	y += lineHeight;
	InitializeSoundSlider( soundOptionsInfo.musicvolume, ID_MUSICVOLUME, y, "Music Volume:", 0, 10 );
	y += lineHeight;
	InitializeSoundSpinControl( soundOptionsInfo.quality, ID_QUALITY, y, "Sound Quality:", QualityItems.data() );
	y += lineHeight;
	InitializeSoundSpinControl( soundOptionsInfo.backend, ID_BACKEND, y, "Audio Backend:", BackendItems.data() );
	y += lineHeight;
	InitializeSoundSpinControl( soundOptionsInfo.hrtf, ID_HRTF, y, "HRTF:", ToggleItems.data() );
	y += lineHeight;
	InitializeSoundSpinControl( soundOptionsInfo.reverb, ID_REVERB, y, "Environment Reverb:", ToggleItems.data() );
	y += lineHeight;
	InitializeSoundSpinControl( soundOptionsInfo.reverbPreset, ID_REVERB_PRESET, y, "Reverb Preset:", ReverbPresetItems.data() );
	y += lineHeight;
	InitializeSoundSlider( soundOptionsInfo.reverbLevel, ID_REVERB_LEVEL, y, "Reverb Level:", 0, 20 );
	y += lineHeight;
	InitializeSoundSpinControl( soundOptionsInfo.occlusion, ID_OCCLUSION, y, "Occlusion:", ToggleItems.data() );
	y += lineHeight;
	InitializeSoundSlider( soundOptionsInfo.occlusionStrength, ID_OCCLUSION_STRENGTH, y, "Occlusion Strength:", 0, 10 );

	soundOptionsInfo.back.generic.type			= MTYPE_BITMAP;
	soundOptionsInfo.back.generic.name			= ART_BACK0;
	soundOptionsInfo.back.generic.flags			= QMF_LEFT_JUSTIFY | QMF_PULSEIFFOCUS;
	soundOptionsInfo.back.generic.callback		= UI_SoundOptionsMenu_Event;
	soundOptionsInfo.back.generic.id			= ID_BACK;
	soundOptionsInfo.back.generic.x				= 0;
	soundOptionsInfo.back.generic.y				= 480 - 64;
	soundOptionsInfo.back.width					= 128;
	soundOptionsInfo.back.height				= 64;
	soundOptionsInfo.back.focuspic				= ART_BACK1;

	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.banner );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.framel );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.framer );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.graphics );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.display );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.sound );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.network );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.sfxvolume );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.musicvolume );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.quality );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.backend );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.hrtf );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.reverb );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.reverbPreset );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.reverbLevel );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.occlusion );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.occlusionStrength );
	Menu_AddItem( &soundOptionsInfo.menu, (void *)&soundOptionsInfo.back );

	soundOptionsInfo.sfxvolume.curvalue = trap_Cvar_VariableValue( "s_volume" ) * 10.0f;
	soundOptionsInfo.musicvolume.curvalue = trap_Cvar_VariableValue( "s_musicVolume" ) * 10.0f;
	soundOptionsInfo.quality.curvalue = !trap_Cvar_VariableValue( "s_compression" );
	soundOptionsInfo.backend.curvalue = CurrentBackendIndex();
	soundOptionsInfo.hrtf.curvalue = trap_Cvar_VariableValue( "s_openalHrtf" ) != 0.0f;
	soundOptionsInfo.reverb.curvalue = trap_Cvar_VariableValue( "s_openalReverb" ) != 0.0f;
	soundOptionsInfo.reverbPreset.curvalue = Com_Clamp( 0, static_cast<int>( ReverbPresetItems.size() ) - 2,
		static_cast<int>( trap_Cvar_VariableValue( "s_openalReverbPreset" ) ) + 1 );
	soundOptionsInfo.reverbLevel.curvalue = Com_Clamp( 0, 20, trap_Cvar_VariableValue( "s_openalReverbLevel" ) * 10.0f );
	soundOptionsInfo.occlusion.curvalue = trap_Cvar_VariableValue( "s_openalOcclusion" ) != 0.0f;
	soundOptionsInfo.occlusionStrength.curvalue = Com_Clamp( 0, 10, trap_Cvar_VariableValue( "s_openalOcclusionStrength" ) * 10.0f );

	UI_SoundOptionsMenu_UpdateState();
}


/*
===============
UI_SoundOptionsMenu_Cache
===============
*/
void UI_SoundOptionsMenu_Cache( void )
{
	trap_R_RegisterShaderNoMip( ART_FRAMEL );
	trap_R_RegisterShaderNoMip( ART_FRAMER );
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );
}


/*
===============
UI_SoundOptionsMenu
===============
*/
void UI_SoundOptionsMenu( void )
{
	UI_SoundOptionsMenu_Init();
	UI_PushMenu( &soundOptionsInfo.menu );
	Menu_SetCursorToItem( &soundOptionsInfo.menu, &soundOptionsInfo.sound );
}
