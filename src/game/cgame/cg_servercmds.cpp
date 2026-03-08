// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_servercmds.c -- reliably sequenced text commands sent by the server
// these are processed at snapshot transition time, so there will definately
// be a valid snapshot this frame

#include "cg_local.h"

#include <algorithm>
#include <array>
#include <string_view>

#ifdef MISSIONPACK // bk001204
#include "../../ui/menudef.h" // bk001205 - for Q3_ui as well

typedef struct {
	const char *order;
	int taskNum;
} orderTask_t;

static const orderTask_t validOrders[] = {
	{ VOICECHAT_GETFLAG,						TEAMTASK_OFFENSE },
	{ VOICECHAT_OFFENSE,						TEAMTASK_OFFENSE },
	{ VOICECHAT_DEFEND,							TEAMTASK_DEFENSE },
	{ VOICECHAT_DEFENDFLAG,					TEAMTASK_DEFENSE },
	{ VOICECHAT_PATROL,							TEAMTASK_PATROL },
	{ VOICECHAT_CAMP,								TEAMTASK_CAMP },
	{ VOICECHAT_FOLLOWME,						TEAMTASK_FOLLOW },
	{ VOICECHAT_RETURNFLAG,					TEAMTASK_RETRIEVE },
	{ VOICECHAT_FOLLOWFLAGCARRIER,	TEAMTASK_ESCORT }
};

static const int numValidOrders = sizeof(validOrders) / sizeof(orderTask_t);

static int CG_ValidOrder(const char *p) {
	int i;
	for (i = 0; i < numValidOrders; i++) {
		if (Q_stricmp(p, validOrders[i].order) == 0) {
			return validOrders[i].taskNum;
		}
	}
	return -1;
}
#endif

static void CG_RemoveChatEscapeChar( char *text );
static void CG_AddToTeamChat( const char *str );

namespace {

[[nodiscard]] constexpr int ScoreArgIndex( const int scoreIndex, const int fieldOffset ) noexcept {
	return scoreIndex * 14 + 4 + fieldOffset;
}

[[nodiscard]] constexpr int TeamInfoArgIndex( const int playerIndex, const int fieldOffset ) noexcept {
	return playerIndex * 6 + 2 + fieldOffset;
}

[[nodiscard]] int ParseArgInt( const int index ) {
	return atoi( CG_Argv( index ) );
}

[[nodiscard]] int ParseConfigStringInt( const char *value ) {
	return atoi( value );
}

[[nodiscard]] bool CommandIs( const char *command, const std::string_view expected ) noexcept {
	return command == expected;
}

[[nodiscard]] bool CommandIsIgnoreCase( const char *command, const char *expected ) noexcept {
	return Q_stricmp( command, expected ) == 0;
}

[[nodiscard]] constexpr int TeamChatSlot( const int position, const int chatHeight ) noexcept {
	return position % chatHeight;
}

[[nodiscard]] bool CommandIdIsNumeric( const char *id ) noexcept {
	return *id >= '0' && *id <= '9';
}

void PrintChatMessage( const char *message, const char *id ) {
	if ( CommandIdIsNumeric( id ) ) {
		CG_Printf( "(%i) %s\n", atoi( id ), message );
		return;
	}

	CG_Printf( "%s\n", message );
}

void HandlePrintCommand() {
	const char *const message = CG_Argv( 1 );
	CG_Printf( "%s", message );
#ifdef MISSIONPACK
	// This is how vote pass/fail announcements arrive from the server.
	if ( !Q_stricmpn( message, "vote failed", 11 ) || !Q_stricmpn( message, "team vote failed", 16 ) ) {
		trap_S_StartLocalSound( cgs.media.voteFailed, CHAN_ANNOUNCER );
	} else if ( !Q_stricmpn( message, "vote passed", 11 ) || !Q_stricmpn( message, "team vote passed", 16 ) ) {
		trap_S_StartLocalSound( cgs.media.votePassed, CHAN_ANNOUNCER );
	}
#endif
}

void HandleChatCommand( const bool teamChat ) {
	if ( !teamChat && cg_teamChatsOnly.integer ) {
		return;
	}

	std::array<char, MAX_SAY_TEXT> text{};
	const char *const id = CG_Argv( 2 );

	trap_S_StartLocalSound( cgs.media.talkSound, CHAN_LOCAL_SOUND );
	Q_strncpyz( text.data(), CG_Argv( 1 ), text.size() );
	CG_RemoveChatEscapeChar( text.data() );

	if ( teamChat ) {
		CG_AddToTeamChat( text.data() );
	}

	PrintChatMessage( text.data(), id );
}

void HandleRemapShaderCommand() {
	if ( trap_Argc() != 4 ) {
		return;
	}

	std::array<char, MAX_QPATH> shader1{};
	std::array<char, MAX_QPATH> shader2{};
	std::array<char, MAX_QPATH> shader3{};

	Q_strncpyz( shader1.data(), CG_Argv( 1 ), shader1.size() );
	Q_strncpyz( shader2.data(), CG_Argv( 2 ), shader2.size() );
	Q_strncpyz( shader3.data(), CG_Argv( 3 ), shader3.size() );

	trap_R_RemapShader( shader1.data(), shader2.data(), shader3.data() );
}

[[nodiscard]] bool ShouldIgnoreDefragServerCommand( const char *command ) noexcept {
	return cgs.defrag && cg.demoPlayback && ( CommandIs( command, "aswitch" ) || CommandIs( command, "stats" ) );
}

void PlayVoteNowSound() {
#ifdef MISSIONPACK
	trap_S_StartLocalSound( cgs.media.voteNow, CHAN_ANNOUNCER );
#endif
}

void ApplyFlagStatusConfig( const char *flagStatus ) {
	if ( cgs.gametype == GT_CTF ) {
		cgs.redflag = flagStatus[0] - '0';
		cgs.blueflag = flagStatus[1] - '0';
		return;
	}
#ifdef MISSIONPACK
	if ( cgs.gametype == GT_1FCTF ) {
		cgs.flagStatus = flagStatus[0] - '0';
	}
#endif
}

template <std::size_t N>
bool TryUpdateTeamVoteValue(
	const int configString,
	const int baseConfigString,
	int (&values)[N],
	qboolean (&modified)[N],
	const char *value
) {
	if ( configString < baseConfigString || configString >= baseConfigString + static_cast<int>( N ) ) {
		return false;
	}

	const int slot = configString - baseConfigString;
	values[slot] = ParseConfigStringInt( value );
	modified[slot] = qtrue;
	return true;
}

template <std::size_t N, std::size_t M>
bool TryUpdateTeamVoteString(
	const int configString,
	const int baseConfigString,
	char (&values)[N][M],
	const char *value
) {
	if ( configString < baseConfigString || configString >= baseConfigString + static_cast<int>( N ) ) {
		return false;
	}

	Q_strncpyz( values[configString - baseConfigString], value, M );
	PlayVoteNowSound();
	return true;
}

bool TryHandleRegisteredAssetConfigString( const int configString, const char *value ) {
	if ( configString >= CS_MODELS && configString < CS_MODELS + MAX_MODELS ) {
		cgs.gameModels[configString - CS_MODELS] = trap_R_RegisterModel( value );
		return true;
	}

	if ( configString >= CS_SOUNDS && configString < CS_SOUNDS + MAX_SOUNDS ) {
		if ( value[0] != '*' ) {
			cgs.gameSounds[configString - CS_SOUNDS] = trap_S_RegisterSound( value, qfalse );
		}
		return true;
	}

	if ( configString >= CS_PLAYERS && configString < CS_PLAYERS + MAX_CLIENTS ) {
		CG_NewClientInfo( configString - CS_PLAYERS );
		CG_BuildSpectatorString();
		return true;
	}

	return false;
}

} // namespace

/*
=================
CG_ParseScores

=================
*/
static void CG_ParseScores( void ) {
	int		i, powerups;

	cg.numScores = ParseArgInt( 1 );
	if ( cg.numScores > MAX_CLIENTS ) {
		cg.numScores = MAX_CLIENTS;
	}

	cg.teamScores[0] = ParseArgInt( 2 );
	cg.teamScores[1] = ParseArgInt( 3 );

	cg.scores.fill( score_t{} );
	for ( i = 0 ; i < cg.numScores ; i++ ) {
		score_t &score = cg.scores[i];

		//
		score.client = ParseArgInt( ScoreArgIndex( i, 0 ) );
		score.score = ParseArgInt( ScoreArgIndex( i, 1 ) );
		score.ping = ParseArgInt( ScoreArgIndex( i, 2 ) );
		score.time = ParseArgInt( ScoreArgIndex( i, 3 ) );
		score.scoreFlags = ParseArgInt( ScoreArgIndex( i, 4 ) );
		powerups = ParseArgInt( ScoreArgIndex( i, 5 ) );
		score.accuracy = ParseArgInt( ScoreArgIndex( i, 6 ) );
		score.impressiveCount = ParseArgInt( ScoreArgIndex( i, 7 ) );
		score.excellentCount = ParseArgInt( ScoreArgIndex( i, 8 ) );
		score.gauntletCount = ParseArgInt( ScoreArgIndex( i, 9 ) );
		score.defendCount = ParseArgInt( ScoreArgIndex( i, 10 ) );
		score.assistCount = ParseArgInt( ScoreArgIndex( i, 11 ) );
		score.perfect = ParseArgInt( ScoreArgIndex( i, 12 ) );
		score.captures = ParseArgInt( ScoreArgIndex( i, 13 ) );

		if ( score.client < 0 || score.client >= MAX_CLIENTS ) {
			score.client = 0;
		}
		cgs.clientinfo[ score.client ].score = score.score;
		cgs.clientinfo[ score.client ].powerups = powerups;

		score.team = cgs.clientinfo[score.client].team;
	}
#ifdef MISSIONPACK
	CG_SetScoreSelection(nullptr);
#endif
}


/*
=================
CG_ParseTeamInfo
=================
*/
static void CG_ParseTeamInfo( void ) {
	int		i;
	int		client;

	numSortedTeamPlayers = ParseArgInt( 1 );
	if( (unsigned) numSortedTeamPlayers > TEAM_MAXOVERLAY )
		numSortedTeamPlayers = TEAM_MAXOVERLAY;

	for ( i = 0 ; i < numSortedTeamPlayers ; i++ ) {
		client = ParseArgInt( TeamInfoArgIndex( i, 0 ) );
		if ( (unsigned) client >= MAX_CLIENTS )
			continue;

		sortedTeamPlayers[i] = client;

		cgs.clientinfo[ client ].location = ParseArgInt( TeamInfoArgIndex( i, 1 ) );
		cgs.clientinfo[ client ].health = ParseArgInt( TeamInfoArgIndex( i, 2 ) );
		cgs.clientinfo[ client ].armor = ParseArgInt( TeamInfoArgIndex( i, 3 ) );
		cgs.clientinfo[ client ].curWeapon = ParseArgInt( TeamInfoArgIndex( i, 4 ) );
		cgs.clientinfo[ client ].powerups = ParseArgInt( TeamInfoArgIndex( i, 5 ) );
	}
}


/*
================
CG_ParseServerinfo

This is called explicitly when the gamestate is first received,
and whenever the server updates any serverinfo flagged cvars
================
*/
void CG_ParseServerinfo( void ) {
	const char	*info;
	const char	*mapname;

	info = CG_ConfigString( CS_SERVERINFO );
	cgs.gametype = static_cast<gametype_t>( atoi( Info_ValueForKey( info, "g_gametype" ) ) );
	trap_Cvar_Set( "ui_gametype", va( "%i", cgs.gametype ) );
	cgs.dmflags = atoi( Info_ValueForKey( info, "dmflags" ) );
	cgs.teamflags = atoi( Info_ValueForKey( info, "teamflags" ) );
	cgs.fraglimit = atoi( Info_ValueForKey( info, "fraglimit" ) );
	cgs.capturelimit = atoi( Info_ValueForKey( info, "capturelimit" ) );
	cgs.timelimit = atoi( Info_ValueForKey( info, "timelimit" ) );
	cgs.maxclients = atoi( Info_ValueForKey( info, "sv_maxclients" ) );
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cgs.mapname, sizeof( cgs.mapname ), "maps/%s.bsp", mapname );
	Q_strncpyz( cgs.redTeam, Info_ValueForKey( info, "g_redTeam" ), sizeof(cgs.redTeam) );
	Q_strncpyz( cgs.blueTeam, Info_ValueForKey( info, "g_blueTeam" ), sizeof(cgs.blueTeam) );
}


void CG_ParseSysteminfo( void ) {
	const char	*info;

	info = CG_ConfigString( CS_SYSTEMINFO );

	cgs.pmove_fixed = ( atoi( Info_ValueForKey( info, "pmove_fixed" ) ) ) ? qtrue : qfalse;
	cgs.pmove_msec = atoi( Info_ValueForKey( info, "pmove_msec" ) );
	if ( cgs.pmove_msec < 8 ) {
		cgs.pmove_msec = 8;
	} else if ( cgs.pmove_msec > 33 ) {
		cgs.pmove_msec = 33;
	}

	cgs.synchronousClients = ( atoi( Info_ValueForKey( info, "g_synchronousClients" ) ) ) ? qtrue : qfalse;
}


/*
==================
CG_ParseWarmup
==================
*/
static void CG_ParseWarmup( void ) {
	const char	*info;
	int			warmup;

	info = CG_ConfigString( CS_WARMUP );

	warmup = atoi( info );
	cg.warmupCount = -1;

	if ( warmup ) {
		cg.timelimitWarnings |= 1 | 2 | 4;
		cg.fraglimitWarnings |= 1 | 2 | 4;
	}

	if ( cg.clientFrame == 0 ) {
		if ( warmup == 0 && cgs.gametype != GT_SINGLE_PLAYER ) {
			if ( cg.snap && ( cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR || cg.snap->ps.pm_flags & PMF_FOLLOW ) ) {
				// force sound playback in CG_WarmupEvents()
				cg.warmup = cg.time;
				cg.warmupCount = -2; // special value to silent FIGHT sound for demo playback
			}
			return;
		}
	}

	if ( warmup == 0 && cg.warmup ) {

	} else if ( warmup > 0 && cg.warmup <= 0 ) {
#ifdef MISSIONPACK
		if (cgs.gametype >= GT_CTF && cgs.gametype <= GT_HARVESTER) {
			trap_S_StartLocalSound( cgs.media.countPrepareTeamSound, CHAN_ANNOUNCER );
		} else
#endif
		{
			if ( cg.soundPlaying != cgs.media.countPrepareSound ) {
				CG_AddBufferedSound( -1 );
				CG_AddBufferedSound( cgs.media.countPrepareSound );
				cg.soundTime = cg.time + 1; // play in next frame
			}
		}
	}

	cg.warmup = warmup;
}


/*
================
CG_SetConfigValues

Called on load to set the initial values from configure strings
================
*/
void CG_SetConfigValues( void ) {
	cgs.scores1 = ParseConfigStringInt( CG_ConfigString( CS_SCORES1 ) );
	cgs.scores2 = ParseConfigStringInt( CG_ConfigString( CS_SCORES2 ) );
	cgs.levelStartTime = ParseConfigStringInt( CG_ConfigString( CS_LEVEL_START_TIME ) );
	ApplyFlagStatusConfig( CG_ConfigString( CS_FLAGSTATUS ) );
	CG_ParseWarmup();
}


/*
=====================
CG_ShaderStateChanged
=====================
*/
void CG_ShaderStateChanged(void) {
	std::array<char, MAX_QPATH> originalShader{};
	std::array<char, MAX_QPATH> newShader{};
	std::array<char, 16> timeOffset{};
	const char *o;
	const char *n, *t;

	o = CG_ConfigString( CS_SHADERSTATE );
	while (o && *o) {
		n = strchr(o, '=');
		if (n) {
			Com_sprintf( originalShader.data(), originalShader.size(), "%.*s", static_cast<int>( n - o ), o );
			n++;
			t = strchr(n, ':');
			if (t) {
				Com_sprintf( newShader.data(), newShader.size(), "%.*s", static_cast<int>( t - n ), n );
			} else {
				break;
			}
			t++;
			o = strchr(t, '@');
			if (o) {
				Com_sprintf( timeOffset.data(), timeOffset.size(), "%.*s", static_cast<int>( o - t ), t );
				o++;
				trap_R_RemapShader( originalShader.data(), newShader.data(), timeOffset.data() );
			}
		} else {
			break;
		}
	}
}


/*
================
CG_ConfigStringModified

================
*/
static void CG_ConfigStringModified( void ) {
	const char	*str;
	const int	num = ParseArgInt( 1 );

	// get the gamestate from the client system, which will have the
	// new configstring already integrated
	trap_GetGameState( &cgs.gameState );

	// look up the individual string that was modified
	str = CG_ConfigString( num );

	if ( TryUpdateTeamVoteValue( num, CS_TEAMVOTE_TIME, cgs.teamVoteTime, cgs.teamVoteModified, str )
		|| TryUpdateTeamVoteValue( num, CS_TEAMVOTE_YES, cgs.teamVoteYes, cgs.teamVoteModified, str )
		|| TryUpdateTeamVoteValue( num, CS_TEAMVOTE_NO, cgs.teamVoteNo, cgs.teamVoteModified, str )
		|| TryUpdateTeamVoteString( num, CS_TEAMVOTE_STRING, cgs.teamVoteString, str )
		|| TryHandleRegisteredAssetConfigString( num, str ) ) {
		return;
	}

	switch ( num ) {
	case CS_MUSIC:
		CG_StartMusic();
		return;
	case CS_SYSTEMINFO:
		CG_ParseSysteminfo();
		return;
	case CS_SERVERINFO:
		CG_ParseServerinfo();
		return;
	case CS_WARMUP:
		CG_ParseWarmup();
		return;
	case CS_SCORES1:
		cgs.scores1 = ParseConfigStringInt( str );
		return;
	case CS_SCORES2:
		cgs.scores2 = ParseConfigStringInt( str );
		return;
	case CS_LEVEL_START_TIME:
		cgs.levelStartTime = ParseConfigStringInt( str );
		return;
	case CS_VOTE_TIME:
		cgs.voteTime = ParseConfigStringInt( str );
		cgs.voteModified = qtrue;
		return;
	case CS_VOTE_YES:
		cgs.voteYes = ParseConfigStringInt( str );
		cgs.voteModified = qtrue;
		return;
	case CS_VOTE_NO:
		cgs.voteNo = ParseConfigStringInt( str );
		cgs.voteModified = qtrue;
		return;
	case CS_VOTE_STRING:
		Q_strncpyz( cgs.voteString, str, sizeof( cgs.voteString ) );
		PlayVoteNowSound();
		return;
	case CS_INTERMISSION:
		cg.intermissionStarted = ParseConfigStringInt( str );
		return;
	case CS_FLAGSTATUS:
		ApplyFlagStatusConfig( str );
		return;
	case CS_SHADERSTATE:
		CG_ShaderStateChanged();
		return;
	default:
		return;
	}
}


/*
=======================
CG_AddToTeamChat

=======================
*/
static void CG_AddToTeamChat( const char *str ) {
	int len;
	char *p, *ls;
	int lastcolor;
	int chatHeight;

	if (cg_teamChatHeight.integer < TEAMCHAT_HEIGHT) {
		chatHeight = cg_teamChatHeight.integer;
	} else {
		chatHeight = TEAMCHAT_HEIGHT;
	}

	if (chatHeight <= 0 || cg_teamChatTime.integer <= 0) {
		// team chat disabled, dump into normal chat
		cgs.teamChatPos = cgs.teamLastChatPos = 0;
		return;
	}

	len = 0;

	p = cgs.teamChatMsgs[TeamChatSlot( cgs.teamChatPos, chatHeight )].data();
	*p = 0;

	lastcolor = '7';

	ls = nullptr;
	while (*str) {
		if (len > TEAMCHAT_WIDTH - 1) {
			if (ls) {
				str -= (p - ls);
				str++;
				p -= (p - ls);
			}
			*p = 0;

			cgs.teamChatMsgTimes[TeamChatSlot( cgs.teamChatPos, chatHeight )] = cg.time;

			cgs.teamChatPos++;
			p = cgs.teamChatMsgs[TeamChatSlot( cgs.teamChatPos, chatHeight )].data();
			*p = 0;
			*p++ = Q_COLOR_ESCAPE;
			*p++ = lastcolor;
			len = 0;
			ls = nullptr;
		}

		if ( Q_IsColorString( str ) ) {
			*p++ = *str++;
			lastcolor = *str;
			*p++ = *str++;
			continue;
		}
		if (*str == ' ') {
			ls = p;
		}
		*p++ = *str++;
		len++;
	}
	*p = 0;

	cgs.teamChatMsgTimes[TeamChatSlot( cgs.teamChatPos, chatHeight )] = cg.time;
	cgs.teamChatPos++;

	if (cgs.teamChatPos - cgs.teamLastChatPos > chatHeight)
		cgs.teamLastChatPos = cgs.teamChatPos - chatHeight;
}

/*
===============
CG_MapRestart

The server has issued a map_restart, so the next snapshot
is completely new and should not be interpolated to.

A tournement restart will clear everything, but doesn't
require a reload of all the media
===============
*/
static void CG_MapRestart( void ) {
	if ( cg_showmiss.integer ) {
		CG_Printf( "CG_MapRestart\n" );
	}

	CG_InitLocalEntities();
	CG_InitMarkPolys();
	CG_ClearParticles ();

	// make sure the "3 frags left" warnings play again
	cg.fraglimitWarnings = 0;
	cg.timelimitWarnings = 0;

	cg.rewardTime = 0;
	cg.rewardStack = 0;
	cg.intermissionStarted = qfalse;
	cg.levelShot = qfalse;

	cgs.voteTime = 0;

	cg.mapRestart = qtrue;

	CG_StartMusic();

	trap_S_ClearLoopingSounds( qtrue );

	cg.allowPickupPrediction = cg.time + PICKUP_PREDICTION_DELAY;

	// we really should clear more parts of cg here and stop sounds

	// play the "fight" sound if this is a restart without warmup
	if ( cg.warmup == 0 /* && cgs.gametype == GT_TOURNAMENT */ ) {
		// force sound playback in CG_WarmupEvents()
		cg.warmup = cg.time;
		cg.warmupCount = -1;
	}

#ifdef MISSIONPACK
	if (cg_singlePlayerActive.integer) {
		trap_Cvar_Set("ui_matchStartTime", va("%i", cg.time));
		if (cg_recordSPDemo.integer && *cg_recordSPDemoName.string) {
			trap_SendConsoleCommand(va("set g_synchronousclients 1 ; record %s \n", cg_recordSPDemoName.string));
		}
	}
#endif

	trap_Cvar_Set( "cg_thirdPerson", "0" );
}

#ifdef MISSIONPACK

#define MAX_VOICEFILESIZE	16384
#define MAX_VOICEFILES		8
#define MAX_VOICECHATS		64
#define MAX_VOICESOUNDS		64
#define MAX_CHATSIZE		64
#define MAX_HEADMODELS		64

typedef struct voiceChat_s
{
	std::array<char, 64> id{};
	int numSounds = 0;
	std::array<sfxHandle_t, MAX_VOICESOUNDS> sounds{};
	std::array<std::array<char, MAX_CHATSIZE>, MAX_VOICESOUNDS> chats{};
} voiceChat_t;

typedef struct voiceChatList_s
{
	std::array<char, 64> name{};
	int gender = 0;
	int numVoiceChats = 0;
	std::array<voiceChat_t, MAX_VOICECHATS> voiceChats{};
} voiceChatList_t;

typedef struct headModelVoiceChat_s
{
	std::array<char, 64> headmodel{};
	int voiceChatNum = 0;
} headModelVoiceChat_t;

std::array<voiceChatList_t, MAX_VOICEFILES> voiceChatLists;
std::array<headModelVoiceChat_t, MAX_HEADMODELS> headModelVoiceChat;

namespace {

constexpr std::array<const char *, MAX_VOICEFILES> kDefaultVoiceChatFiles = {
	"scripts/female1.voice",
	"scripts/female2.voice",
	"scripts/female3.voice",
	"scripts/male1.voice",
	"scripts/male2.voice",
	"scripts/male3.voice",
	"scripts/male4.voice",
	"scripts/male5.voice",
};

template <std::size_t N>
[[nodiscard]] bool IsEmptyCString( const std::array<char, N> &value ) noexcept {
	return value.front() == '\0';
}

[[nodiscard]] bool IsEmptyToken( const char *token ) noexcept {
	return token[0] == '\0';
}

[[nodiscard]] auto ParseVoiceChatGender( const char *token ) noexcept -> int {
	if ( !Q_stricmp( token, "female" ) ) {
		return GENDER_FEMALE;
	}
	if ( !Q_stricmp( token, "male" ) ) {
		return GENDER_MALE;
	}
	if ( !Q_stricmp( token, "neuter" ) ) {
		return GENDER_NEUTER;
	}

	return -1;
}

[[nodiscard]] auto ReadVoiceChatFile(
	const char *filename,
	std::array<char, MAX_VOICEFILESIZE + 1> &buffer,
	const bool reportMissingFile
) -> int {
	fileHandle_t fileHandle;
	const int len = trap_FS_FOpenFile( filename, &fileHandle, FS_READ );

	if ( fileHandle == FS_INVALID_HANDLE ) {
		if ( reportMissingFile ) {
			trap_Print( va( S_COLOR_RED "voice chat file not found: %s\n", filename ) );
		}
		return -1;
	}

	if ( len >= MAX_VOICEFILESIZE ) {
		trap_Print( va( S_COLOR_RED "voice chat file too large: %s is %i, max allowed is %i", filename, len, MAX_VOICEFILESIZE ) );
		trap_FS_FCloseFile( fileHandle );
		return -1;
	}

	trap_FS_Read( buffer.data(), len, fileHandle );
	buffer[len] = '\0';
	trap_FS_FCloseFile( fileHandle );
	return len;
}

void BuildHeadModelName( std::array<char, MAX_QPATH> &headModelName, const clientInfo_t &clientInfo, const bool includeSkin ) {
	const char *const baseHeadModelName = clientInfo.headModelName[0] == '*' ? clientInfo.headModelName + 1 : clientInfo.headModelName;

	if ( includeSkin ) {
		Com_sprintf( headModelName.data(), headModelName.size(), "%s/%s", baseHeadModelName, clientInfo.headSkinName );
		return;
	}

	Com_sprintf( headModelName.data(), headModelName.size(), "%s", baseHeadModelName );
}

[[nodiscard]] auto FindCachedHeadModelVoiceChat( const char *headModelName ) noexcept -> int {
	for ( int i = 0; i < MAX_HEADMODELS; ++i ) {
		if ( !Q_stricmp( headModelVoiceChat[i].headmodel.data(), headModelName ) ) {
			return i;
		}
	}

	return -1;
}

[[nodiscard]] auto FindOpenHeadModelVoiceChatSlot() noexcept -> int {
	for ( int i = 0; i < MAX_HEADMODELS; ++i ) {
		if ( IsEmptyCString( headModelVoiceChat[i].headmodel ) ) {
			return i;
		}
	}

	return -1;
}

void CacheHeadModelVoiceChat( const int slot, const char *headModelName, const int voiceChatNum ) {
	Com_sprintf( headModelVoiceChat[slot].headmodel.data(), headModelVoiceChat[slot].headmodel.size(), "%s", headModelName );
	headModelVoiceChat[slot].voiceChatNum = voiceChatNum;
}

[[nodiscard]] auto FindVoiceChatListByGender( const int gender ) noexcept -> int {
	for ( int i = 0; i < MAX_VOICEFILES; ++i ) {
		if ( !IsEmptyCString( voiceChatLists[i].name ) && voiceChatLists[i].gender == gender ) {
			return i;
		}
	}

	return -1;
}

} // namespace

/*
=================
CG_ParseVoiceChats
=================
*/
int CG_ParseVoiceChats( const char *filename, voiceChatList_t *voiceChatList, int maxVoiceChats ) {
	std::array<char, MAX_VOICEFILESIZE + 1> buffer{};
	char *bufferCursor = buffer.data();
	char **p = &bufferCursor;
	char *token;
	const qboolean compress = cg_buildScript.integer ? qfalse : qtrue;

	if ( ReadVoiceChatFile( filename, buffer, true ) < 0 ) {
		return qfalse;
	}

	*voiceChatList = voiceChatList_t{};
	Com_sprintf( voiceChatList->name.data(), voiceChatList->name.size(), "%s", filename );

	token = COM_ParseExt( p, qtrue );
	if ( IsEmptyToken( token ) ) {
		return qtrue;
	}

	voiceChatList->gender = ParseVoiceChatGender( token );
	if ( voiceChatList->gender < 0 ) {
		trap_Print( va( S_COLOR_RED "expected gender not found in voice chat file: %s\n", filename ) );
		return qfalse;
	}

	while ( 1 ) {
		token = COM_ParseExt( p, qtrue );
		if ( IsEmptyToken( token ) ) {
			return qtrue;
		}

		voiceChat_t &voiceChat = voiceChatList->voiceChats[voiceChatList->numVoiceChats];
		Com_sprintf( voiceChat.id.data(), voiceChat.id.size(), "%s", token );

		token = COM_ParseExt( p, qtrue );
		if ( Q_stricmp( token, "{" ) ) {
			trap_Print( va( S_COLOR_RED "expected { found %s in voice chat file: %s\n", token, filename ) );
			return qfalse;
		}

		while ( 1 ) {
			token = COM_ParseExt( p, qtrue );
			if ( IsEmptyToken( token ) ) {
				return qtrue;
			}
			if ( !Q_stricmp( token, "}" ) ) {
				break;
			}

			const sfxHandle_t sound = trap_S_RegisterSound( token, compress );
			voiceChat.sounds[voiceChat.numSounds] = sound;

			token = COM_ParseExt( p, qtrue );
			if ( IsEmptyToken( token ) ) {
				return qtrue;
			}

			Com_sprintf( voiceChat.chats[voiceChat.numSounds].data(), voiceChat.chats[voiceChat.numSounds].size(), "%s", token );
			if ( sound ) {
				++voiceChat.numSounds;
			}
			if ( voiceChat.numSounds >= MAX_VOICESOUNDS ) {
				break;
			}
		}

		++voiceChatList->numVoiceChats;
		if ( voiceChatList->numVoiceChats >= maxVoiceChats ) {
			return qtrue;
		}
	}

	return qtrue;
}

/*
=================
CG_LoadVoiceChats
=================
*/
void CG_LoadVoiceChats( void ) {
	const int size = trap_MemoryRemaining();

	for ( int i = 0; i < MAX_VOICEFILES; ++i ) {
		CG_ParseVoiceChats( kDefaultVoiceChatFiles[i], &voiceChatLists[i], MAX_VOICECHATS );
	}

	CG_Printf( "voice chat memory size = %d\n", size - trap_MemoryRemaining() );
}

/*
=================
CG_HeadModelVoiceChats
=================
*/
int CG_HeadModelVoiceChats( const char *filename ) {
	int i;
	std::array<char, MAX_VOICEFILESIZE + 1> buffer{};
	char *bufferCursor = buffer.data();
	char **p = &bufferCursor;
	char *token;

	if ( ReadVoiceChatFile( filename, buffer, false ) < 0 ) {
		return -1;
	}

	token = COM_ParseExt( p, qtrue );
	if ( IsEmptyToken( token ) ) {
		return -1;
	}

	for ( i = 0; i < MAX_VOICEFILES; i++ ) {
		if ( !Q_stricmp( token, voiceChatLists[i].name.data() ) ) {
			return i;
		}
	}

	//FIXME: maybe try to load the .voice file which name is stored in token?

	return -1;
}


/*
=================
CG_GetVoiceChat
=================
*/
int CG_GetVoiceChat( voiceChatList_t *voiceChatList, const char *id, sfxHandle_t *snd, char **chat) {
	int i, rnd;

	for ( i = 0; i < voiceChatList->numVoiceChats; i++ ) {
		voiceChat_t &voiceChat = voiceChatList->voiceChats[i];

		if ( !Q_stricmp( id, voiceChat.id.data() ) ) {
			if ( voiceChat.numSounds == 0 ) {
				return qfalse;
			}

			rnd = random() * voiceChat.numSounds;
			*snd = voiceChat.sounds[rnd];
			*chat = voiceChat.chats[rnd].data();
			return qtrue;
		}
	}
	return qfalse;
}


/*
=================
CG_VoiceChatListForClient
=================
*/
voiceChatList_t *CG_VoiceChatListForClient( int clientNum ) {
	clientInfo_t *ci;
	int voiceChatNum, cachedVoiceChatSlot, openVoiceChatSlot, gender;
	std::array<char, MAX_QPATH> filename{};
	std::array<char, MAX_QPATH> headModelName{};

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		clientNum = 0;
	}
	ci = &cgs.clientinfo[ clientNum ];

	for ( int k = 0; k < 2; ++k ) {
		BuildHeadModelName( headModelName, *ci, k == 0 );

		// find the voice file for the head model the client uses
		cachedVoiceChatSlot = FindCachedHeadModelVoiceChat( headModelName.data() );
		if ( cachedVoiceChatSlot >= 0 ) {
			return &voiceChatLists[headModelVoiceChat[cachedVoiceChatSlot].voiceChatNum];
		}

		// find a <headmodelname>.vc file
		openVoiceChatSlot = FindOpenHeadModelVoiceChatSlot();
		if ( openVoiceChatSlot < 0 ) {
			continue;
		}

		Com_sprintf( filename.data(), filename.size(), "scripts/%s.vc", headModelName.data() );
		voiceChatNum = CG_HeadModelVoiceChats( filename.data() );
		if ( voiceChatNum == -1 ) {
			continue;
		}

		CacheHeadModelVoiceChat( openVoiceChatSlot, headModelName.data(), voiceChatNum );
		return &voiceChatLists[headModelVoiceChat[openVoiceChatSlot].voiceChatNum];
	}

	gender = ci->gender;
	for ( int k = 0; k < 2; ++k ) {
		// just pick the first with the right gender
		voiceChatNum = FindVoiceChatListByGender( gender );
		if ( voiceChatNum >= 0 ) {
			openVoiceChatSlot = FindOpenHeadModelVoiceChatSlot();
			if ( openVoiceChatSlot >= 0 ) {
				CacheHeadModelVoiceChat( openVoiceChatSlot, headModelName.data(), voiceChatNum );
			}
			return &voiceChatLists[voiceChatNum];
		}

		// fall back to male gender because we don't have neuter in the mission pack
		if ( gender == GENDER_MALE ) {
			break;
		}
		gender = GENDER_MALE;
	}

	// store this head model with voice chat for future reference
	openVoiceChatSlot = FindOpenHeadModelVoiceChatSlot();
	if ( openVoiceChatSlot >= 0 ) {
		CacheHeadModelVoiceChat( openVoiceChatSlot, headModelName.data(), 0 );
	}

	// just return the first voice chat list
	return &voiceChatLists[0];
}

#define MAX_VOICECHATBUFFER		32

namespace {

[[nodiscard]] constexpr int NextVoiceChatBufferIndex( const int index ) noexcept {
	return ( index + 1 ) % MAX_VOICECHATBUFFER;
}

} // namespace

typedef struct bufferedVoiceChat_s
{
	int clientNum;
	sfxHandle_t snd;
	int voiceOnly;
	char cmd[MAX_SAY_TEXT];
	char message[MAX_SAY_TEXT];
} bufferedVoiceChat_t;

std::array<bufferedVoiceChat_t, MAX_VOICECHATBUFFER> voiceChatBuffer;

/*
=================
CG_PlayVoiceChat
=================
*/
void CG_PlayVoiceChat( bufferedVoiceChat_t *vchat ) {

	// if we are going into the intermission, don't start any voices
	if ( cg.intermissionStarted ) {
		return;
	}

	if ( !cg_noVoiceChats.integer ) {
		trap_S_StartLocalSound( vchat->snd, CHAN_VOICE);
		if (vchat->clientNum != cg.snap->ps.clientNum) {
			int orderTask = CG_ValidOrder(vchat->cmd);
			if (orderTask > 0) {
				cgs.acceptOrderTime = cg.time + 5000;
				Q_strncpyz(cgs.acceptVoice, vchat->cmd, sizeof(cgs.acceptVoice));
				cgs.acceptTask = orderTask;
				cgs.acceptLeader = vchat->clientNum;
			}
			// see if this was an order
			CG_ShowResponseHead();
		}
	}
	if (!vchat->voiceOnly && !cg_noVoiceText.integer) {
		CG_AddToTeamChat( vchat->message );
		CG_Printf( "%s\n", vchat->message );
	}
	voiceChatBuffer[cg.voiceChatBufferOut].snd = 0;
}


/*
=====================
CG_PlayBufferedVoieChats
=====================
*/
void CG_PlayBufferedVoiceChats( void ) {
	if ( cg.voiceChatTime < cg.time ) {
		if ( cg.voiceChatBufferOut != cg.voiceChatBufferIn && voiceChatBuffer[cg.voiceChatBufferOut].snd ) {
			//
			CG_PlayVoiceChat( &voiceChatBuffer[cg.voiceChatBufferOut] );
			//
			cg.voiceChatBufferOut = NextVoiceChatBufferIndex( cg.voiceChatBufferOut );
			cg.voiceChatTime = cg.time + 1000;
		}
	}
}


/*
=====================
CG_AddBufferedVoiceChat
=====================
*/
void CG_AddBufferedVoiceChat( bufferedVoiceChat_t *vchat ) {

	// if we are going into the intermission, don't start any voices
	if ( cg.intermissionStarted ) {
		return;
	}

	voiceChatBuffer[cg.voiceChatBufferIn] = *vchat;
	cg.voiceChatBufferIn = NextVoiceChatBufferIndex( cg.voiceChatBufferIn );
	if ( cg.voiceChatBufferIn == cg.voiceChatBufferOut ) {
		CG_PlayVoiceChat( &voiceChatBuffer[cg.voiceChatBufferOut] );
		cg.voiceChatBufferOut = NextVoiceChatBufferIndex( cg.voiceChatBufferOut );
	}
}


/*
=================
CG_VoiceChatLocal
=================
*/
void CG_VoiceChatLocal( int mode, qboolean voiceOnly, int clientNum, int color, const char *cmd ) {

	char *chat;
	voiceChatList_t *voiceChatList;
	clientInfo_t *ci;
	sfxHandle_t snd;
	bufferedVoiceChat_t vchat{};

	// if we are going into the intermission, don't start any voices
	if ( cg.intermissionStarted ) {
		return;
	}

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		clientNum = 0;
	}
	ci = &cgs.clientinfo[ clientNum ];

	cgs.currentVoiceClient = clientNum;

	voiceChatList = CG_VoiceChatListForClient( clientNum );

	if ( CG_GetVoiceChat( voiceChatList, cmd, &snd, &chat ) ) {
		//
		if ( mode == SAY_TEAM || !cg_teamChatsOnly.integer ) {
			vchat.clientNum = clientNum;
			vchat.snd = snd;
			vchat.voiceOnly = voiceOnly;
			Q_strncpyz(vchat.cmd, cmd, sizeof(vchat.cmd));
			if ( mode == SAY_TELL ) {
				Com_sprintf(vchat.message, sizeof(vchat.message), "[%s]: %c%c%s", ci->name, Q_COLOR_ESCAPE, color, chat);
			}
			else if ( mode == SAY_TEAM ) {
				Com_sprintf(vchat.message, sizeof(vchat.message), "(%s): %c%c%s", ci->name, Q_COLOR_ESCAPE, color, chat);
			}
			else {
				Com_sprintf(vchat.message, sizeof(vchat.message), "%s: %c%c%s", ci->name, Q_COLOR_ESCAPE, color, chat);
			}
			CG_AddBufferedVoiceChat(&vchat);
		}
	}
}


/*
=================
CG_VoiceChat
=================
*/
void CG_VoiceChat( int mode ) {
	const char *cmd;
	int clientNum, color;
	qboolean voiceOnly;

	voiceOnly = atoi(CG_Argv(1));
	clientNum = atoi(CG_Argv(2));
	color = atoi(CG_Argv(3));
	cmd = CG_Argv(4);

	if (cg_noTaunt.integer != 0) {
		static constexpr std::array<std::string_view, 5> blockedTaunts = {
			VOICECHAT_KILLINSULT,
			VOICECHAT_TAUNT,
			VOICECHAT_DEATHINSULT,
			VOICECHAT_KILLGAUNTLET,
			VOICECHAT_PRAISE,
		};

		if ( std::ranges::any_of( blockedTaunts, [cmd]( const std::string_view blockedCommand ) {
			return CommandIs( cmd, blockedCommand );
		} ) ) {
			return;
		}
	}

	CG_VoiceChatLocal( mode, voiceOnly, clientNum, color, cmd );
}
#endif // MISSIONPACK


/*
=================
CG_RemoveChatEscapeChar
=================
*/
static void CG_RemoveChatEscapeChar( char *text ) {
	char *write = text;

	for ( char *read = text; *read != '\0'; ++read ) {
		if ( *read == '\x19' ) {
			continue;
		}

		*write++ = *read;
	}

	*write = '\0';
}


/*
=================
CG_ServerCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
static void CG_ServerCommand( void ) {
	const char *const cmd = CG_Argv( 0 );

	if ( cmd[0] == '\0' ) {
		// server claimed the command
		return;
	}

	if ( CommandIs( cmd, "cp" ) ) {
		CG_CenterPrint( CG_Argv( 1 ), SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH );
		return;
	}

	if ( CommandIs( cmd, "cs" ) ) {
		CG_ConfigStringModified();
		return;
	}

	if ( CommandIs( cmd, "print" ) ) {
		HandlePrintCommand();
		return;
	}

	if ( CommandIs( cmd, "chat" ) ) {
		HandleChatCommand( false );
		return;
	}

	if ( CommandIs( cmd, "tchat" ) ) {
		HandleChatCommand( true );
		return;
	}

#ifdef MISSIONPACK
	if ( CommandIs( cmd, "vchat" ) ) {
		CG_VoiceChat( SAY_ALL );
		return;
	}

	if ( CommandIs( cmd, "vtchat" ) ) {
		CG_VoiceChat( SAY_TEAM );
		return;
	}

	if ( CommandIs( cmd, "vtell" ) ) {
		CG_VoiceChat( SAY_TELL );
		return;
	}
#endif

	if ( CommandIs( cmd, "scores" ) ) {
		CG_ParseScores();
		return;
	}

	if ( CommandIs( cmd, "tinfo" ) ) {
		CG_ParseTeamInfo();
		return;
	}

	if ( CommandIs( cmd, "map_restart" ) ) {
		CG_MapRestart();
		return;
	}

	if ( CommandIsIgnoreCase( cmd, "remapShader" ) ) {
		HandleRemapShaderCommand();
		return;
	}

	// loaddeferred can be both a servercmd and a consolecmd
	if ( CommandIs( cmd, "loaddeferred" ) ) {	// FIXME: spelled wrong, but not changing for demo
		CG_LoadDeferredPlayers();
		return;
	}

	// clientLevelShot is sent before taking a special screenshot for
	// the menu system during development
	if ( CommandIs( cmd, "clientLevelShot" ) ) {
		cg.levelShot = qtrue;
		return;
	}

	if ( ShouldIgnoreDefragServerCommand( cmd ) ) {
		return;
	}

	CG_Printf( "Unknown client game command: %s\n", cmd );
}


/*
====================
CG_ExecuteNewServerCommands

Execute all of the server commands that were received along
with this this snapshot.
====================
*/
void CG_ExecuteNewServerCommands( int latestSequence ) {
	while ( cgs.serverCommandSequence < latestSequence ) {
		if ( trap_GetServerCommand( ++cgs.serverCommandSequence ) ) {
			CG_ServerCommand();
		}
	}
}
