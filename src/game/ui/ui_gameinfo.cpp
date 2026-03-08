// Copyright (C) 1999-2000 Id Software, Inc.
//
//
// gameinfo.c
//

#include "ui_local.h"

#include <array>
#include <span>


//
// arena and bot info
//


int				ui_numBots;
static std::array<char *, MAX_BOTS> ui_botInfos{};

static int		ui_numArenas;
static std::array<char *, MAX_ARENAS> ui_arenaInfos{};

#ifndef MISSIONPACK // bk001206
static int		ui_numSinglePlayerArenas;
static int		ui_numSpecialSinglePlayerArenas;
#endif

template <std::size_t Size>
[[nodiscard]] static bool ReadTextFile( const char *filename, std::array<char, Size> &buffer ) {
	fileHandle_t f;
	const int maxTextSize = static_cast<int>( buffer.size() - 1 );
	const int len = trap_FS_FOpenFile( filename, &f, FS_READ );
	if ( !f ) {
		trap_Print( va( S_COLOR_RED "file not found: %s\n", filename ) );
		return false;
	}
	if ( len >= maxTextSize ) {
		trap_Print( va( S_COLOR_RED "file too large: %s is %i, max allowed is %i", filename, len, maxTextSize ) );
		trap_FS_FCloseFile( f );
		return false;
	}

	trap_FS_Read( buffer.data(), len, f );
	buffer[len] = '\0';
	trap_FS_FCloseFile( f );
	return true;
}

template <std::size_t Size>
static void BuildScriptPath( std::array<char, Size> &path, const char *filename ) {
	Q_strncpyz( path.data(), "scripts/", static_cast<int>( path.size() ) );
	Q_strcat( path.data(), static_cast<int>( path.size() ), filename );
}

template <std::size_t Size>
[[nodiscard]] static auto RemainingInfoSlots( std::array<char *, Size> &infos, const int currentCount ) noexcept -> std::span<char *> {
	const auto currentIndex = static_cast<std::size_t>( currentCount );
	return std::span{ infos }.subspan( currentIndex, infos.size() - currentIndex );
}

template <std::size_t DirectoryListSize, std::size_t PathSize, typename Loader>
void LoadScriptDirectoryFiles( const char *extension, std::array<char, DirectoryListSize> &dirlist, std::array<char, PathSize> &filename, Loader &&loader ) {
	const int numdirs = trap_FS_GetFileList( "scripts", extension, dirlist.data(), static_cast<int>( dirlist.size() ) );
	char *dirptr = dirlist.data();
	for ( int i = 0; i < numdirs; ++i ) {
		const int dirlen = strlen( dirptr );
		BuildScriptPath( filename, dirptr );
		loader( filename.data() );
		dirptr += dirlen + 1;
	}
}

template <typename Loader>
void LoadConfiguredOrDefaultScript( const char *configuredPath, const char *defaultPath, Loader &&loader ) {
	if ( configuredPath != nullptr && configuredPath[0] != '\0' ) {
		loader( configuredPath );
		return;
	}

	loader( defaultPath );
}

void ApplyArenaTypeBits( mapInfo &map, const char *type ) {
	// if no type specified, it will be treated as "ffa"
	if ( type == nullptr || *type == '\0' ) {
		map.typeBits |= ( 1 << GT_FFA );
		return;
	}

	if ( strstr( type, "ffa" ) ) {
		map.typeBits |= ( 1 << GT_FFA );
	}
	if ( strstr( type, "tourney" ) ) {
		map.typeBits |= ( 1 << GT_TOURNAMENT );
	}
	if ( strstr( type, "ctf" ) ) {
		map.typeBits |= ( 1 << GT_CTF );
	}
	if ( strstr( type, "oneflag" ) ) {
		map.typeBits |= ( 1 << GT_1FCTF );
	}
	if ( strstr( type, "overload" ) ) {
		map.typeBits |= ( 1 << GT_OBELISK );
	}
	if ( strstr( type, "harvester" ) ) {
		map.typeBits |= ( 1 << GT_HARVESTER );
	}
}

void InitializeMapInfoFromArena( const char *arenaInfo ) {
	auto &map = uiInfo.mapList[uiInfo.mapCount];
	map.cinematic = -1;
	map.mapLoadName = String_Alloc( Info_ValueForKey( arenaInfo, "map" ) );
	map.mapName = String_Alloc( Info_ValueForKey( arenaInfo, "longname" ) );
	map.levelShot = -1;
	map.imageName = String_Alloc( va( "levelshots/%s", map.mapLoadName ) );
	map.typeBits = 0;
	ApplyArenaTypeBits( map, Info_ValueForKey( arenaInfo, "type" ) );
}

/*
===============
UI_ParseInfos
===============
*/
int UI_ParseInfos( char *buf, const std::span<char *> infos ) {
	char	*token;
	int		count;
	std::array<char, MAX_TOKEN_CHARS> key{};
	std::array<char, MAX_INFO_STRING> info{};

	count = 0;

	while ( 1 ) {
		token = COM_Parse( &buf );
		if ( !token[0] ) {
			break;
		}
		if ( strcmp( token, "{" ) ) {
			Com_Printf( "Missing { in info file\n" );
			break;
		}

		if ( count == static_cast<int>( infos.size() ) ) {
			Com_Printf( "Max infos exceeded\n" );
			break;
		}

		info[0] = '\0';
		while ( 1 ) {
			token = COM_ParseExt( &buf, qtrue );
			if ( !token[0] ) {
				Com_Printf( "Unexpected end of info file\n" );
				break;
			}
			if ( !strcmp( token, "}" ) ) {
				break;
			}
			Q_strncpyz( key.data(), token, static_cast<int>( key.size() ) );

			token = COM_ParseExt( &buf, qfalse );
			Info_SetValueForKey( info.data(), key.data(), token[0] ? token : "<NULL>" );
		}
		//NOTE: extra space for arena number
		infos[count] = UI_Alloc( strlen( info.data() ) + strlen("\\num\\") + strlen(va("%d", MAX_ARENAS)) + 1 );
		if (infos[count]) {
			strcpy( infos[count], info.data() );
			count++;
		}
	}
	return count;
}

/*
===============
UI_LoadArenasFromFile
===============
*/
static void UI_LoadArenasFromFile( const char *filename ) {
	std::array<char, MAX_ARENAS_TEXT + 1> buffer{};
	if ( !ReadTextFile( filename, buffer ) ) {
		return;
	}

	ui_numArenas += UI_ParseInfos( buffer.data(), RemainingInfoSlots( ui_arenaInfos, ui_numArenas ) );
}

/*
===============
UI_LoadArenas
===============
*/
void UI_LoadArenas( void ) {
	vmCvar_t	arenasFile;
	std::array<char, 128> filename{};
	std::array<char, 1024> dirlist{};
	int			n;

	ui_numArenas = 0;
	uiInfo.mapCount = 0;

	trap_Cvar_Register( &arenasFile, "g_arenasFile", "", CVAR_INIT|CVAR_ROM );
	LoadConfiguredOrDefaultScript( arenasFile.string, "scripts/arenas.txt", UI_LoadArenasFromFile );

	// get all arenas from .arena files
	LoadScriptDirectoryFiles( ".arena", dirlist, filename, UI_LoadArenasFromFile );
	trap_Print( va( "%i arenas parsed\n", ui_numArenas ) );
	if (UI_OutOfMemory()) {
		trap_Print(S_COLOR_YELLOW"WARNING: not anough memory in pool to load all arenas\n");
	}

	for( n = 0; n < ui_numArenas; n++ ) {
		InitializeMapInfoFromArena( ui_arenaInfos[n] );
		uiInfo.mapCount++;
		if (uiInfo.mapCount >= MAX_MAPS) {
			break;
		}
	}
}


/*
===============
UI_LoadBotsFromFile
===============
*/
static void UI_LoadBotsFromFile( const char *filename ) {
	std::array<char, MAX_BOTS_TEXT + 1> buffer{};
	if ( !ReadTextFile( filename, buffer ) ) {
		return;
	}

	COM_Compress( buffer.data() );

	ui_numBots += UI_ParseInfos( buffer.data(), RemainingInfoSlots( ui_botInfos, ui_numBots ) );
}

/*
===============
UI_LoadBots
===============
*/
void UI_LoadBots( void ) {
	vmCvar_t	botsFile;
	std::array<char, 128> filename{};
	std::array<char, 1024> dirlist{};

	ui_numBots = 0;

	trap_Cvar_Register( &botsFile, "g_botsFile", "", CVAR_INIT|CVAR_ROM );
	LoadConfiguredOrDefaultScript( botsFile.string, "scripts/bots.txt", UI_LoadBotsFromFile );

	// get all bots from .bot files
	LoadScriptDirectoryFiles( ".bot", dirlist, filename, UI_LoadBotsFromFile );
	trap_Print( va( "%i bots parsed\n", ui_numBots ) );
}


/*
===============
UI_GetBotInfoByNumber
===============
*/
char *UI_GetBotInfoByNumber( int num ) {
	if( num < 0 || num >= ui_numBots ) {
		trap_Print( va( S_COLOR_RED "Invalid bot number: %i\n", num ) );
		return nullptr;
	}
	return ui_botInfos[num];
}


/*
===============
UI_GetBotInfoByName
===============
*/
char *UI_GetBotInfoByName( const char *name ) {
	int		n;
	const char	*value;

	for ( n = 0; n < ui_numBots ; n++ ) {
		value = Info_ValueForKey( ui_botInfos[n], "name" );
		if ( !Q_stricmp( value, name ) ) {
			return ui_botInfos[n];
		}
	}

	return nullptr;
}

int UI_GetNumBots() {
	return ui_numBots;
}


char *UI_GetBotNameByNumber( int num ) {
	char *info = UI_GetBotInfoByNumber(num);
	if (info) {
		return Info_ValueForKey( info, "name" );
	}
	return "Sarge";
}
