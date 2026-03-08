// Copyright (C) 1999-2000 Id Software, Inc.
//
// g_bot.c

#include "g_local.h"

#include <algorithm>
#include <array>
#include <span>


static int		g_numBots;
static std::array<char *, MAX_BOTS> g_botInfos{};


int				g_numArenas;
static std::array<char *, MAX_ARENAS> g_arenaInfos{};


#define BOT_BEGIN_DELAY_BASE		2000
#define BOT_BEGIN_DELAY_INCREMENT	1500

#define BOT_SPAWN_QUEUE_DEPTH	16

typedef struct {
	int		clientNum;
	int		spawnTime;
} botSpawnQueue_t;

static std::array<botSpawnQueue_t, BOT_SPAWN_QUEUE_DEPTH> botSpawnQueue{};

vmCvar_t bot_minplayers;

extern gentity_t	*podium1;
extern gentity_t	*podium2;
extern gentity_t	*podium3;

extern char mapname[ MAX_QPATH ];

template <std::size_t Size>
[[nodiscard]] static bool ReadTextFile( const char *filename, std::array<char, Size> &buffer ) {
	fileHandle_t	f;
	const int maxTextSize = static_cast<int>( buffer.size() - 1 );
	const int len = trap_FS_FOpenFile( filename, &f, FS_READ );
	if ( f == FS_INVALID_HANDLE ) {
		trap_Print( va( S_COLOR_RED "file not found: %s\n", filename ) );
		return false;
	}
	if ( len >= maxTextSize ) {
		trap_Print( va( S_COLOR_RED "file too large: %s is %i, max allowed is %i\n", filename, len, maxTextSize ) );
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

[[nodiscard]] static auto BotSpawnQueueEntries() noexcept -> std::span<botSpawnQueue_t> {
	return botSpawnQueue;
}

[[nodiscard]] static botSpawnQueue_t *FindFreeBotSpawnSlot() noexcept {
	const auto slot = std::find_if( botSpawnQueue.begin(), botSpawnQueue.end(), []( const botSpawnQueue_t &entry ) {
		return entry.spawnTime == 0;
	} );
	return slot != botSpawnQueue.end() ? &*slot : nullptr;
}

[[nodiscard]] static botSpawnQueue_t *FindQueuedBotSpawnSlot( const int clientNum ) noexcept {
	const auto slot = std::find_if( botSpawnQueue.begin(), botSpawnQueue.end(), [clientNum]( const botSpawnQueue_t &entry ) {
		return entry.clientNum == clientNum;
	} );
	return slot != botSpawnQueue.end() ? &*slot : nullptr;
}

[[nodiscard]] static float ClampBotSkill( const float skill ) noexcept {
	return std::clamp( skill, 1.0f, 5.0f );
}

[[nodiscard]] static float ConfiguredSinglePlayerBotSkill() {
	const float configuredSkill = trap_Cvar_VariableValue( "g_spSkill" );
	const float skill = ClampBotSkill( configuredSkill );
	if ( configuredSkill < 1.0f ) {
		trap_Cvar_Set( "g_spSkill", "1" );
	}
	else if ( configuredSkill > 5.0f ) {
		trap_Cvar_Set( "g_spSkill", "5" );
	}
	return skill;
}

[[nodiscard]] static const char *InfoValueOrDefault( const char *info, const char *key, const char *fallback ) {
	const char *value = Info_ValueForKey( info, key );
	return *value ? value : fallback;
}

template <std::size_t Size>
static void CopyInfoValueOrDefault( std::array<char, Size> &buffer, const char *info, const char *key, const char *fallback ) {
	Q_strncpyz( buffer.data(), InfoValueOrDefault( info, key, fallback ), static_cast<int>( buffer.size() ) );
}

float trap_Cvar_VariableValue( const char *var_name ) {
	std::array<char, 128> buffer{};

	trap_Cvar_VariableStringBuffer( var_name, buffer.data(), static_cast<int>( buffer.size() ) );
	return atof( buffer.data() );
}



/*
===============
G_ParseInfos
===============
*/
int G_ParseInfos( char *buf, const std::span<char *> infos ) {
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
		infos[count] = G_Alloc( strlen( info.data() ) + strlen( "\\num\\" ) + strlen( va( "%d", MAX_ARENAS ) ) + 1 );
		if ( infos[count] ) {
			strcpy( infos[count], info.data() );
			count++;
		}
	}
	return count;
}


/*
===============
G_LoadArenasFromFile
===============
*/
static void G_LoadArenasFromFile( const char *filename ) {
	std::array<char, MAX_ARENAS_TEXT + 1> buffer{};
	if ( !ReadTextFile( filename, buffer ) ) {
		return;
	}

	g_numArenas += G_ParseInfos( buffer.data(), RemainingInfoSlots( g_arenaInfos, g_numArenas ) );
}


/*
===============
G_LoadArenas
===============
*/
static void G_LoadArenas( void ) {
	int			numdirs;
	vmCvar_t	arenasFile;
	std::array<char, 128> filename{};
	std::array<char, 1024> dirlist{};
	char*		dirptr;
	int			i, n;
	int			dirlen;

	g_numArenas = 0;

	trap_Cvar_Register( &arenasFile, "g_arenasFile", "", CVAR_INIT|CVAR_ROM );
	if( *arenasFile.string ) {
		G_LoadArenasFromFile(arenasFile.string);
	}
	else {
		G_LoadArenasFromFile("scripts/arenas.txt");
	}

	// get all arenas from .arena files
	numdirs = trap_FS_GetFileList( "scripts", ".arena", dirlist.data(), static_cast<int>( dirlist.size() ) );
	dirptr  = dirlist.data();
	for (i = 0; i < numdirs; i++, dirptr += dirlen+1) {
		dirlen = (int)strlen(dirptr);
		BuildScriptPath( filename, dirptr );
		G_LoadArenasFromFile( filename.data() );
	}
	trap_Print( va( "%i arenas parsed\n", g_numArenas ) );
	
	for( n = 0; n < g_numArenas; n++ ) {
		Info_SetValueForKey( g_arenaInfos[n], "num", va( "%i", n ) );
	}
}


/*
===============
G_GetArenaInfoByNumber
===============
*/
const char *G_GetArenaInfoByMap( const char *map ) {
	int			n;

	for( n = 0; n < g_numArenas; n++ ) {
		if( Q_stricmp( Info_ValueForKey( g_arenaInfos[n], "map" ), map ) == 0 ) {
			return g_arenaInfos[n];
		}
	}

	return nullptr;
}


/*
=================
PlayerIntroSound
=================
*/
static void PlayerIntroSound( const char *modelAndSkin ) {
	std::array<char, MAX_QPATH> model{};
	char	*skin;

	Q_strncpyz( model.data(), modelAndSkin, static_cast<int>( model.size() ) );
	skin = Q_strrchr( model.data(), '/' );
	if ( skin ) {
		*skin++ = '\0';
	}
	else {
		skin = model.data();
	}

	if( Q_stricmp( skin, "default" ) == 0 ) {
		skin = model.data();
	}

	trap_SendConsoleCommand( EXEC_APPEND, va( "play sound/player/announce/%s.wav\n", skin ) );
}


/*
===============
G_AddRandomBot
===============
*/
void G_AddRandomBot( team_t team ) {
	int		i, n, num;
	float	skill;
	char	*value, netname[36], *teamstr, *skillstr;
	gclient_t	*cl;

	num = 0;
	for ( n = 0; n < g_numBots ; n++ ) {
		value = Info_ValueForKey( g_botInfos[n], "name" );
		//
		for ( i = 0 ; i < level.maxclients ; i++ ) {
			cl = level.clients + i;
			if ( cl->pers.connected != CON_CONNECTED ) {
				continue;
			}
			if ( !(g_entities[i].r.svFlags & SVF_BOT) ) {
				continue;
			}
			if ( team >= 0 && cl->sess.sessionTeam != team ) {
				continue;
			}
			if ( !Q_stricmp( value, cl->pers.netname ) ) {
				break;
			}
		}
		if (i >= level.maxclients) {
			num++;
		}
	}
	num = random() * num;
	for ( n = 0; n < g_numBots ; n++ ) {

		value = Info_ValueForKey( g_botInfos[ n ], "name" );

		skillstr = Info_ValueForKey( g_botInfos[ n ], "skill" );
		if ( *skillstr )
			skill = atof( skillstr );
		else
			skill = ConfiguredSinglePlayerBotSkill();

		for ( i = 0 ; i < level.maxclients ; i++ ) {
			cl = level.clients + i;
			if ( cl->pers.connected != CON_CONNECTED ) {
				continue;
			}
			if ( !(g_entities[i].r.svFlags & SVF_BOT) ) {
				continue;
			}
			if ( team >= 0 && cl->sess.sessionTeam != team ) {
				continue;
			}
			if ( !Q_stricmp( value, cl->pers.netname ) ) {
				break;
			}
		}
		if (i >= level.maxclients) {
			num--;
			if ( num <= 0 ) {
				if (team == TEAM_RED) teamstr = "red";
				else if (team == TEAM_BLUE) teamstr = "blue";
				else teamstr = "";
				Q_strncpyz(netname, value, sizeof(netname));
				Q_CleanStr(netname);
				trap_SendConsoleCommand( EXEC_INSERT, va( "addbot %s %1.2f %s 0\n", netname, skill, teamstr ) );
				return;
			}
		}
	}
}


/*
===============
G_RemoveRandomBot
===============
*/
int G_RemoveRandomBot( int team ) {
	int i;
	char netname[36];
	gclient_t	*cl;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		cl = level.clients + i;
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( !(g_entities[i].r.svFlags & SVF_BOT) ) {
			continue;
		}
		if ( team >= 0 && cl->sess.sessionTeam != team ) {
			continue;
		}
		strcpy(netname, cl->pers.netname);
		Q_CleanStr(netname);
		trap_SendConsoleCommand( EXEC_INSERT, va("kick %s\n", netname) );
		return qtrue;
	}
	return qfalse;
}


/*
===============
G_CountHumanPlayers
===============
*/
static int G_CountHumanPlayers( int team ) {
	int i, num;
	gclient_t	*cl;

	num = 0;
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		cl = level.clients + i;
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( g_entities[i].r.svFlags & SVF_BOT ) {
			continue;
		}
		if ( team >= 0 && cl->sess.sessionTeam != team ) {
			continue;
		}
		num++;
	}
	return num;
}


/*
===============
G_CountBotPlayers
===============
*/
static int G_CountBotPlayers( int team ) {
	int i, n, num;
	gclient_t	*cl;

	num = 0;
	for ( i=0 ; i< level.maxclients ; i++ ) {
		cl = level.clients + i;
		if ( cl->pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( !(g_entities[i].r.svFlags & SVF_BOT) ) {
			continue;
		}
		if ( team >= 0 && cl->sess.sessionTeam != team ) {
			continue;
		}
		num++;
	}
	for( n = 0; n < BOT_SPAWN_QUEUE_DEPTH; n++ ) {
		if( !botSpawnQueue[n].spawnTime ) {
			continue;
		}
		if ( botSpawnQueue[n].spawnTime > level.time ) {
			continue;
		}
		num++;
	}
	return num;
}


/*
===============
G_CheckMinimumPlayers
===============
*/
void G_CheckMinimumPlayers( void ) {
	int minplayers;
	int humanplayers, botplayers;
	static int checkminimumplayers_time;

	if ( level.intermissiontime )
		return;

	//only check once each 10 seconds
	if ( checkminimumplayers_time > level.time - 10000 )
		return;

	if ( level.time - level.startTime < 2000 )
		return;

	checkminimumplayers_time = level.time;
	trap_Cvar_Update(&bot_minplayers);
	minplayers = bot_minplayers.integer;
	if (minplayers <= 0) return;

	if (g_gametype.integer >= GT_TEAM) {
		if (minplayers >= level.maxclients / 2) {
			minplayers = (level.maxclients / 2) -1;
		}

		humanplayers = G_CountHumanPlayers( TEAM_RED );
		botplayers = G_CountBotPlayers(	TEAM_RED );
		//
		if (humanplayers + botplayers < minplayers) {
			G_AddRandomBot( TEAM_RED );
		} else if (humanplayers + botplayers > minplayers && botplayers) {
			G_RemoveRandomBot( TEAM_RED );
		}
		//
		humanplayers = G_CountHumanPlayers( TEAM_BLUE );
		botplayers = G_CountBotPlayers( TEAM_BLUE );
		//
		if (humanplayers + botplayers < minplayers) {
			G_AddRandomBot( TEAM_BLUE );
		} else if (humanplayers + botplayers > minplayers && botplayers) {
			G_RemoveRandomBot( TEAM_BLUE );
		}
	}
	else if (g_gametype.integer == GT_TOURNAMENT ) {
		if (minplayers >= level.maxclients) {
			minplayers = level.maxclients-1;
		}
		humanplayers = G_CountHumanPlayers( -1 );
		botplayers = G_CountBotPlayers( -1 );
		//
		if (humanplayers + botplayers < minplayers) {
			G_AddRandomBot( TEAM_FREE );
		} else if (humanplayers + botplayers > minplayers && botplayers) {
			// try to remove spectators first
			if (!G_RemoveRandomBot( TEAM_SPECTATOR )) {
				// just remove the bot that is playing
				G_RemoveRandomBot( -1 );
			}
		}
	}
	else if (g_gametype.integer == GT_FFA) {
		if (minplayers >= level.maxclients) {
			minplayers = level.maxclients-1;
		}
		humanplayers = G_CountHumanPlayers( TEAM_FREE );
		botplayers = G_CountBotPlayers( TEAM_FREE );
		//
		if (humanplayers + botplayers < minplayers) {
			G_AddRandomBot( TEAM_FREE );
		} else if (humanplayers + botplayers > minplayers && botplayers) {
			G_RemoveRandomBot( TEAM_FREE );
		}
	}
}


/*
===============
G_CheckBotSpawn
===============
*/
void G_CheckBotSpawn( void ) {
	std::array<char, MAX_INFO_VALUE> userinfo{};

	G_CheckMinimumPlayers();

	for ( botSpawnQueue_t &queuedBot : BotSpawnQueueEntries() ) {
		if ( !queuedBot.spawnTime ) {
			continue;
		}
		if ( queuedBot.spawnTime > level.time ) {
			continue;
		}
		ClientBegin( queuedBot.clientNum );
		queuedBot.spawnTime = 0;

		if( g_gametype.integer == GT_SINGLE_PLAYER ) {
			trap_GetUserinfo( queuedBot.clientNum, userinfo.data(), static_cast<int>( userinfo.size() ) );
			PlayerIntroSound( Info_ValueForKey( userinfo.data(), "model" ) );
		}
	}
}


/*
===============
AddBotToSpawnQueue
===============
*/
static void AddBotToSpawnQueue( int clientNum, int delay ) {
	if ( botSpawnQueue_t *slot = FindFreeBotSpawnSlot(); slot != nullptr ) {
		slot->spawnTime = level.time + delay;
		slot->clientNum = clientNum;
		return;
	}

	G_Printf( S_COLOR_YELLOW "Unable to delay bot spawn\n" );

	ClientBegin( clientNum );
}


/*
===============
G_RemoveQueuedBotBegin

Called on client disconnect to make sure the delayed spawn
doesn't happen on a freed index
===============
*/
void G_RemoveQueuedBotBegin( int clientNum ) {
	if ( botSpawnQueue_t *slot = FindQueuedBotSpawnSlot( clientNum ); slot != nullptr ) {
		slot->spawnTime = 0;
	}
}


/*
===============
G_BotConnect
===============
*/
qboolean G_BotConnect( int clientNum, qboolean restart ) {
	bot_settings_t	settings;
	std::array<char, MAX_INFO_STRING> userinfo{};

	trap_GetUserinfo( clientNum, userinfo.data(), static_cast<int>( userinfo.size() ) );

	Q_strncpyz( settings.characterfile, Info_ValueForKey( userinfo.data(), "characterfile" ), sizeof(settings.characterfile) );
	settings.skill = atof( Info_ValueForKey( userinfo.data(), "skill" ) );
	Q_strncpyz( settings.team, Info_ValueForKey( userinfo.data(), "team" ), sizeof(settings.team) );

	if (!BotAISetupClient( clientNum, &settings, restart )) {
		trap_DropClient( clientNum, "BotAISetupClient failed" );
		return qfalse;
	}

	return qtrue;
}


/*
===============
G_AddBot
===============
*/
static void G_AddBot( const char *name, float skill, const char *team, int delay, const char *altname ) {
	int				clientNum;
	char			*botinfo;
	const char		*botname;
	const char		*model;
	const char		*headmodel;
	const char		*aifile;
	std::array<char, MAX_INFO_STRING> userinfo{};
	std::array<char, MAX_CVAR_VALUE_STRING> nm{};

	// get the botinfo from bots.txt
	botinfo = G_GetBotInfoByName( name );
	if ( !botinfo ) {
		G_Printf( S_COLOR_RED "Error: Bot '%s' not defined\n", name );
		return;
	}

	// create the bot's userinfo
	userinfo[0] = '\0';

	botname = Info_ValueForKey( botinfo, "funname" );
	if( !botname[0] ) {
		botname = Info_ValueForKey( botinfo, "name" );
	}
	// check for an alternative name
	if (altname && altname[0]) {
		botname = altname;
	}

	BG_CleanName( botname, nm.data(), static_cast<int>( nm.size() ), "unnamed bot" );
	Info_SetValueForKey( userinfo.data(), "name", nm.data() );

	Info_SetValueForKey( userinfo.data(), "rate", "25000" );
	Info_SetValueForKey( userinfo.data(), "snaps", va( "%i", sv_fps.integer ) );
	Info_SetValueForKey( userinfo.data(), "skill", va("%1.2f", skill) );

	if ( skill >= 1 && skill < 2 ) {
		Info_SetValueForKey( userinfo.data(), "handicap", "50" );
	}
	else if ( skill >= 2 && skill < 3 ) {
		Info_SetValueForKey( userinfo.data(), "handicap", "70" );
	}
	else if ( skill >= 3 && skill < 4 ) {
		Info_SetValueForKey( userinfo.data(), "handicap", "90" );
	}

	model = InfoValueOrDefault( botinfo, "model", "visor/default" );
	Info_SetValueForKey( userinfo.data(), "model", model );
	//key = "team_model";
	//Info_SetValueForKey( userinfo, key, model );

	headmodel = InfoValueOrDefault( botinfo, "headmodel", model );
	Info_SetValueForKey( userinfo.data(), "headmodel", headmodel );
	//key = "team_headmodel";
	//Info_SetValueForKey( userinfo, key, headmodel );

	Info_SetValueForKey( userinfo.data(), "sex", InfoValueOrDefault( botinfo, "gender", "male" ) );

	Info_SetValueForKey( userinfo.data(), "color1", InfoValueOrDefault( botinfo, "color1", "4" ) );

	Info_SetValueForKey( userinfo.data(), "color2", InfoValueOrDefault( botinfo, "color2", "5" ) );

	aifile = Info_ValueForKey( botinfo, "aifile" );
	if (!*aifile ) {
		trap_Print( S_COLOR_RED "Error: bot has no aifile specified\n" );
		return;
	}

	// have the server allocate a client slot
	clientNum = trap_BotAllocateClient();
	if ( clientNum == -1 ) {
		G_Printf( S_COLOR_RED "Unable to add bot.  All player slots are in use.\n" );
		G_Printf( S_COLOR_RED "Start server with more 'open' slots (or check setting of sv_maxclients cvar).\n" );
		return;
	}

	// cleanup previous data manually
	// because client may silently (re)connect without ClientDisconnect in case of crash for example
	if ( level.clients[ clientNum ].pers.connected != CON_DISCONNECTED ) {
		ClientDisconnect( clientNum );
	}

	Info_SetValueForKey( userinfo.data(), "characterfile", aifile );
	Info_SetValueForKey( userinfo.data(), "skill", va( "%1.2f", skill ) );
	Info_SetValueForKey( userinfo.data(), "team", team );

	gentity_t *bot = &g_entities[ clientNum ];
	bot->r.svFlags |= SVF_BOT;
	bot->inuse = qtrue;

	// register the userinfo
	trap_SetUserinfo( clientNum, userinfo.data() );

	// have it connect to the game as a normal client
	if ( ClientConnect( clientNum, qtrue, qtrue ) ) {
		return;
	}

	if ( delay == 0 ) {
		ClientBegin( clientNum );
		return;
	}

	AddBotToSpawnQueue( clientNum, delay );
}


/*
===============
Svcmd_AddBot_f
===============
*/
void Svcmd_AddBot_f( void ) {
	float			skill;
	int				delay;
	std::array<char, MAX_TOKEN_CHARS> name{};
	std::array<char, MAX_TOKEN_CHARS> altname{};
	std::array<char, MAX_TOKEN_CHARS> string{};
	std::array<char, MAX_TOKEN_CHARS> team{};

	// are bots enabled?
	if ( !trap_Cvar_VariableIntegerValue( "bot_enable" ) ) {
		return;
	}

	// name
	trap_Argv( 1, name.data(), static_cast<int>( name.size() ) );
	if ( !name[0] ) {
		trap_Print( "Usage: Addbot <botname> [skill 1-5] [team] [msec delay] [altname]\n" );
		return;
	}

	// skill
	trap_Argv( 2, string.data(), static_cast<int>( string.size() ) );
	if ( !string[0] ) {
		skill = 4;
	}
	else {
		skill = ClampBotSkill( atof( string.data() ) );
	}

	// team
	trap_Argv( 3, team.data(), static_cast<int>( team.size() ) );

	// delay
	trap_Argv( 4, string.data(), static_cast<int>( string.size() ) );
	if ( !string[0] ) {
		delay = 0;
	}
	else {
		delay = atoi( string.data() );
	}

	// alternative name
	trap_Argv( 5, altname.data(), static_cast<int>( altname.size() ) );

	G_AddBot( name.data(), skill, team.data(), delay, altname.data() );

	// if this was issued during gameplay and we are playing locally,
	// go ahead and load the bot's media immediately
	if ( level.time - level.startTime > 1000 &&
		trap_Cvar_VariableIntegerValue( "cl_running" ) ) {
		trap_SendServerCommand( -1, "loaddeferred\n" );	// FIXME: spelled wrong, but not changing for demo
	}
}

/*
===============
Svcmd_BotList_f
===============
*/
void Svcmd_BotList_f( void ) {
	int i;
	std::array<char, MAX_NETNAME> name{};
	std::array<char, MAX_NETNAME> funname{};
	std::array<char, MAX_QPATH> model{};
	std::array<char, MAX_QPATH> aifile{};

	trap_Print( S_COLOR_RED "name             model            aifile              funname\n" );
	for ( i = 0; i < g_numBots; i++ ) {
		CopyInfoValueOrDefault( name, g_botInfos[i], "name", "UnnamedPlayer" );
		CopyInfoValueOrDefault( funname, g_botInfos[i], "funname", "" );
		CopyInfoValueOrDefault( model, g_botInfos[i], "model", "visor/default" );
		CopyInfoValueOrDefault( aifile, g_botInfos[i], "aifile", "bots/default_c.c" );
		trap_Print( va( "%-16s %-16s %-20s %-20s\n", name.data(), model.data(), aifile.data(), funname.data() ) );
	}
}


/*
===============
G_SpawnBots
===============
*/
static void G_SpawnBots( const char *botList, int baseDelay ) {
	char		*bot;
	char		*p;
	float		skill;
	int			delay;
	std::array<char, MAX_INFO_VALUE> bots{};

	podium1 = nullptr;
	podium2 = nullptr;
	podium3 = nullptr;

	skill = ConfiguredSinglePlayerBotSkill();

	Q_strncpyz( bots.data(), botList, static_cast<int>( bots.size() ) );
	p = bots.data();
	delay = baseDelay;
	while( *p ) {
		//skip spaces
		while( *p == ' ' ) {
			p++;
		}
		if( !*p ) {
			break;
		}

		// mark start of bot name
		bot = p;

		// skip until space of null
		while( *p && *p != ' ' ) {
			p++;
		}
		if( *p ) {
			*p++ = '\0';
		}

		// we must add the bot this way, calling G_AddBot directly at this stage
		// does "Bad Things"
		trap_SendConsoleCommand( EXEC_INSERT, va("addbot %s %f free %i\n", bot, skill, delay) );

		delay += BOT_BEGIN_DELAY_INCREMENT;
	}
}


/*
===============
G_LoadBotsFromFile
===============
*/
static void G_LoadBotsFromFile( const char *filename ) {
	std::array<char, MAX_BOTS_TEXT + 1> buffer{};
	if ( !ReadTextFile( filename, buffer ) ) {
		return;
	}

	g_numBots += G_ParseInfos( buffer.data(), RemainingInfoSlots( g_botInfos, g_numBots ) );
}


/*
===============
G_LoadBots
===============
*/
static void G_LoadBots( void ) {
	vmCvar_t	botsFile;
	int			numdirs;
	std::array<char, 128> filename{};
	std::array<char, 1024> dirlist{};
	char*		dirptr;
	int			i;
	int			dirlen;

	if ( !trap_Cvar_VariableIntegerValue( "bot_enable" ) ) {
		return;
	}

	g_numBots = 0;

	trap_Cvar_Register( &botsFile, "g_botsFile", "", CVAR_ARCHIVE | CVAR_LATCH );

	if ( *botsFile.string && g_gametype.integer != GT_SINGLE_PLAYER ) {
		G_LoadBotsFromFile( botsFile.string );
	} else {
		G_LoadBotsFromFile( "scripts/bots.txt" );
	}

	// get all bots from .bot files
	numdirs = trap_FS_GetFileList( "scripts", ".bot", dirlist.data(), static_cast<int>( dirlist.size() ) );
	dirptr  = dirlist.data();
	for (i = 0; i < numdirs; i++, dirptr += dirlen+1) {
		dirlen = (int)strlen(dirptr);
		BuildScriptPath( filename, dirptr );
		G_LoadBotsFromFile( filename.data() );
	}
	trap_Print( va( "%i bots parsed\n", g_numBots ) );
}



/*
===============
G_GetBotInfoByNumber
===============
*/
char *G_GetBotInfoByNumber( int num ) {
	if( num < 0 || num >= g_numBots ) {
		trap_Print( va( S_COLOR_RED "Invalid bot number: %i\n", num ) );
		return nullptr;
	}
	return g_botInfos[num];
}


/*
===============
G_GetBotInfoByName
===============
*/
char *G_GetBotInfoByName( const char *name ) {
	int		n;
	char	*value;

	for ( n = 0; n < g_numBots ; n++ ) {
		value = Info_ValueForKey( g_botInfos[n], "name" );
		if ( !Q_stricmp( value, name ) ) {
			return g_botInfos[n];
		}
	}

	return nullptr;
}


/*
===============
G_InitBots
===============
*/
void G_InitBots( qboolean restart ) {
	int			fragLimit;
	int			timeLimit;
	const char	*arenainfo;
	char		*strValue;
	int			basedelay;

	G_LoadBots();
	G_LoadArenas();

	trap_Cvar_Register( &bot_minplayers, "bot_minplayers", "0", CVAR_SERVERINFO );

	if( g_gametype.integer == GT_SINGLE_PLAYER ) {
		arenainfo = G_GetArenaInfoByMap( mapname );
		if ( !arenainfo ) {
			return;
		}

		strValue = Info_ValueForKey( arenainfo, "fraglimit" );
		fragLimit = atoi( strValue );
		if ( fragLimit ) {
			trap_Cvar_Set( "fraglimit", strValue );
		}
		else {
			trap_Cvar_Set( "fraglimit", "0" );
		}

		strValue = Info_ValueForKey( arenainfo, "timelimit" );
		timeLimit = atoi( strValue );
		if ( timeLimit ) {
			trap_Cvar_Set( "timelimit", strValue );
		}
		else {
			trap_Cvar_Set( "timelimit", "0" );
		}

		if ( !fragLimit && !timeLimit ) {
			trap_Cvar_Set( "fraglimit", "10" );
			trap_Cvar_Set( "timelimit", "0" );
		}

		basedelay = BOT_BEGIN_DELAY_BASE;
		strValue = Info_ValueForKey( arenainfo, "special" );
		if( Q_stricmp( strValue, "training" ) == 0 ) {
			basedelay += 10000;
		}

		if( !restart ) {
			G_SpawnBots( Info_ValueForKey( arenainfo, "bots" ), basedelay );
		}
	}
}
