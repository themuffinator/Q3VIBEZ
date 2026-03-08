// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_consolecmds.c -- text commands typed in at the local console, or
// executed by a key binding

#include "cg_local.h"

#include <algorithm>
#include <array>
#include <span>
#ifdef MISSIONPACK
#include "../ui/ui_shared.h"
extern menuDef_t *menuScoreboard;
#endif

namespace {

void AdjustViewSize( const int delta ) {
	trap_Cvar_Set( "cg_viewsize", va( "%i", static_cast<int>( cg_viewsize.integer + delta ) ) );
}

void SendDirectedClientCommand( const char *commandName, const int clientNum ) {
	std::array<char, 128> command{};
	std::array<char, 128> message{};

	trap_Args( message.data(), static_cast<int>( message.size() ) );
	Com_sprintf( command.data(), static_cast<int>( command.size() ), "%s %i %s", commandName, clientNum, message.data() );
	trap_SendClientCommand( command.data() );
}

void RegisterForwardedConsoleCommands() {
	static const auto forwardedCommands = std::to_array<const char *>( {
		"kill",
		"say",
		"say_team",
		"tell",
#ifdef MISSIONPACK
		"vsay",
		"vsay_team",
		"vtell",
		"vtaunt",
		"vosay",
		"vosay_team",
		"votell",
#endif
		"give",
		"god",
		"notarget",
		"noclip",
		"team",
		"follow",
		"levelshot",
		"addbot",
		"setviewpos",
		"callvote",
		"vote",
		"callteamvote",
		"teamvote",
		"stats",
		"teamtask",
		"loaddefered"
	} );

	for ( const char *command : forwardedCommands ) {
		trap_AddCommand( command );
	}
}

#ifdef MISSIONPACK
void ScrollScoreboardFeeder( const qboolean down ) {
	if ( menuScoreboard && cg.scoreBoardShowing ) {
		Menu_ScrollFeeder( menuScoreboard, FEEDER_SCOREBOARD, down );
		Menu_ScrollFeeder( menuScoreboard, FEEDER_REDTEAM_LIST, down );
		Menu_ScrollFeeder( menuScoreboard, FEEDER_BLUETEAM_LIST, down );
	}
}

void SendConsoleButtonPulse( const int button ) {
	trap_SendConsoleCommand( va( "+button%d; wait; -button%d", button, button ) );
}

void SendVoiceResponse( const int leader, const char *response ) {
	trap_SendConsoleCommand( va( "cmd vtell %d %s\n", leader, response ) );
}

void SendTeamVoiceCommand( const char *message ) {
	trap_SendConsoleCommand( va( "cmd vsay_team %s\n", message ) );
}

void SendTeamTaskCommand( const int teamTask ) {
	trap_SendClientCommand( va( "teamtask %d\n", teamTask ) );
}

void SendLiteralConsoleCommand( const char *command ) {
	trap_SendConsoleCommand( command );
}

void SendDirectedLiteralClientCommand( const char *commandName, const int clientNum, const char *message ) {
	std::array<char, 128> command{};
	Com_sprintf( command.data(), static_cast<int>( command.size() ), "%s %i %s", commandName, clientNum, message );
	trap_SendClientCommand( command.data() );
}

void ConfigureSinglePlayerVictoryCamera() {
	trap_Cvar_Set( "cg_cameraOrbit", "2" );
	trap_Cvar_Set( "cg_cameraOrbitDelay", "35" );
	trap_Cvar_Set( "cg_thirdPerson", "1" );
	trap_Cvar_Set( "cg_thirdPersonAngle", "0" );
	trap_Cvar_Set( "cg_thirdPersonRange", "100" );
}

void ShowSinglePlayerResult( sfxHandle_t sound, const char *message ) {
	ConfigureSinglePlayerVictoryCamera();
	CG_AddBufferedSound( sound );
	CG_CenterPrint( message, SCREEN_HEIGHT * .30f, 0 );
}

[[nodiscard]] bool CanSelectNextOrder() {
	const clientInfo_t *clientInfo = cgs.clientinfo + cg.snap->ps.clientNum;
	return clientInfo == nullptr || clientInfo->teamLeader || sortedTeamPlayers[cg_currentSelectedPlayer.integer] == cg.snap->ps.clientNum;
}

[[nodiscard]] int NextMissionpackOrder( const int currentOrder ) {
	int nextOrder = currentOrder < TEAMTASK_CAMP ? currentOrder + 1 : TEAMTASK_OFFENSE;

	if ( nextOrder == TEAMTASK_RETRIEVE && !CG_OtherTeamHasFlag() ) {
		++nextOrder;
	}
	if ( nextOrder == TEAMTASK_ESCORT && !CG_YourTeamHasFlag() ) {
		++nextOrder;
	}
	if ( nextOrder > TEAMTASK_CAMP ) {
		nextOrder = TEAMTASK_OFFENSE;
	}
	return nextOrder;
}
#endif

} // namespace


/*
=================
CG_TargetCommand_f
=================
*/
static void CG_TargetCommand_f( void ) {
	int		targetNum;
	std::array<char, 4> command{};

	targetNum = CG_CrosshairPlayer();
	if ( targetNum == -1 ) {
		return;
	}

	trap_Argv( 1, command.data(), command.size() );
	trap_SendConsoleCommand( va( "gc %i %i", targetNum, atoi( command.data() ) ) );
}



/*
=================
CG_SizeUp_f

Keybinding command
=================
*/
static void CG_SizeUp_f (void) {
	AdjustViewSize( 10 );
}


/*
=================
CG_SizeDown_f

Keybinding command
=================
*/
static void CG_SizeDown_f (void) {
	AdjustViewSize( -10 );
}


/*
=============
CG_Viewpos_f

Debugging command to print the current position
=============
*/
static void CG_Viewpos_f (void) {
	CG_Printf ("(%i %i %i) : %i\n", (int)cg.refdef.vieworg[0],
		(int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2], 
		(int)cg.refdefViewAngles[YAW]);
}


static void CG_ScoresDown_f( void ) {

#ifdef MISSIONPACK
	CG_BuildSpectatorString();
#endif
	if ( cg.scoresRequestTime + 2000 < cg.time && !cg.demoPlayback ) {
		// the scores are more than two seconds out of data,
		// so request new ones
		cg.scoresRequestTime = cg.time;
		trap_SendClientCommand( "score" );

		// leave the current scores up if they were already
		// displayed, but if this is the first hit, clear them out
		if ( !cg.showScores ) {
			cg.showScores = qtrue;
			cg.numScores = 0;
		}
	} else {
		// show the cached contents even if they just pressed if it
		// is within two seconds
		cg.showScores = qtrue;
	}

	CG_SetScoreCatcher( cg.showScores );
}


static void CG_ScoresUp_f( void ) {

	if ( cgs.filterKeyUpEvent ) {
		cgs.filterKeyUpEvent = qfalse;
		return;
	}

	if ( cg.showScores ) {
		cg.showScores = qfalse;
		cg.scoreFadeTime = cg.time;
	}

	CG_SetScoreCatcher( cg.showScores );
}


#ifdef MISSIONPACK
extern menuDef_t *menuScoreboard;

static void CG_LoadHud_f( void) {
	std::array<char, 1024> hudFileBuffer{};
	const char *hudSet;

	String_Init();
	Menu_Reset();
	
	trap_Cvar_VariableStringBuffer( "cg_hudFiles", hudFileBuffer.data(), hudFileBuffer.size() );
	hudSet = hudFileBuffer.data();
	if (hudSet[0] == '\0') {
		hudSet = "ui/hud.txt";
	}

	CG_LoadMenus(hudSet);
	menuScoreboard = nullptr;
}


static void CG_scrollScoresDown_f( void) {
	ScrollScoreboardFeeder( qtrue );
}


static void CG_scrollScoresUp_f( void) {
	ScrollScoreboardFeeder( qfalse );
}


static void CG_spWin_f( void) {
	ShowSinglePlayerResult( cgs.media.winnerSound, "YOU WIN!" );
}

static void CG_spLose_f( void) {
	ShowSinglePlayerResult( cgs.media.loserSound, "YOU LOSE..." );
}

#endif

/*
==================
CG_TellTarget_f
==================
*/
static void CG_TellTarget_f( void ) {
	const int clientNum = CG_CrosshairPlayer();
	if ( clientNum == -1 ) {
		return;
	}

	SendDirectedClientCommand( "tell", clientNum );
}


/*
==================
CG_TellAttacker_f
==================
*/
static void CG_TellAttacker_f( void ) {
	const int clientNum = CG_LastAttacker();
	if ( clientNum == -1 ) {
		return;
	}

	SendDirectedClientCommand( "tell", clientNum );
}


#ifdef MISSIONPACK
/*
==================
CG_VoiceTellTarget_f
==================
*/
static void CG_VoiceTellTarget_f( void ) {
	const int clientNum = CG_CrosshairPlayer();
	if ( clientNum == -1 ) {
		return;
	}

	SendDirectedClientCommand( "vtell", clientNum );
}


/*
==================
CG_VoiceTellAttacker_f
==================
*/
static void CG_VoiceTellAttacker_f( void ) {
	const int clientNum = CG_LastAttacker();
	if ( clientNum == -1 ) {
		return;
	}

	SendDirectedClientCommand( "vtell", clientNum );
}

static void CG_NextTeamMember_f( void ) {
  CG_SelectNextPlayer();
}

static void CG_PrevTeamMember_f( void ) {
  CG_SelectPrevPlayer();
}

// ASS U ME's enumeration order as far as task specific orders, OFFENSE is zero, CAMP is last
//
static void CG_NextOrder_f( void ) {
	if ( !CanSelectNextOrder() ) {
		return;
	}
	cgs.currentOrder = NextMissionpackOrder( cgs.currentOrder );
	cgs.orderPending = qtrue;
	cgs.orderTime = cg.time + 3000;
}


static void CG_ConfirmOrder_f (void ) {
	SendVoiceResponse( cgs.acceptLeader, VOICECHAT_YES );
	SendConsoleButtonPulse( 5 );
	if (cg.time < cgs.acceptOrderTime) {
		SendTeamTaskCommand( cgs.acceptTask );
		cgs.acceptOrderTime = 0;
	}
}

static void CG_DenyOrder_f (void ) {
	SendVoiceResponse( cgs.acceptLeader, VOICECHAT_NO );
	SendConsoleButtonPulse( 6 );
	if (cg.time < cgs.acceptOrderTime) {
		cgs.acceptOrderTime = 0;
	}
}

static void CG_TaskOffense_f (void ) {
	if (cgs.gametype == GT_CTF || cgs.gametype == GT_1FCTF) {
		SendTeamVoiceCommand( VOICECHAT_ONGETFLAG );
	} else {
		SendTeamVoiceCommand( VOICECHAT_ONOFFENSE );
	}
	SendTeamTaskCommand( TEAMTASK_OFFENSE );
}

static void CG_TaskDefense_f (void ) {
	SendTeamVoiceCommand( VOICECHAT_ONDEFENSE );
	SendTeamTaskCommand( TEAMTASK_DEFENSE );
}

static void CG_TaskPatrol_f (void ) {
	SendTeamVoiceCommand( VOICECHAT_ONPATROL );
	SendTeamTaskCommand( TEAMTASK_PATROL );
}

static void CG_TaskCamp_f (void ) {
	SendTeamVoiceCommand( VOICECHAT_ONCAMPING );
	SendTeamTaskCommand( TEAMTASK_CAMP );
}

static void CG_TaskFollow_f (void ) {
	SendTeamVoiceCommand( VOICECHAT_ONFOLLOW );
	SendTeamTaskCommand( TEAMTASK_FOLLOW );
}

static void CG_TaskRetrieve_f (void ) {
	SendTeamVoiceCommand( VOICECHAT_ONRETURNFLAG );
	SendTeamTaskCommand( TEAMTASK_RETRIEVE );
}

static void CG_TaskEscort_f (void ) {
	SendTeamVoiceCommand( VOICECHAT_ONFOLLOWCARRIER );
	SendTeamTaskCommand( TEAMTASK_ESCORT );
}

static void CG_TaskOwnFlag_f (void ) {
	SendTeamVoiceCommand( VOICECHAT_IHAVEFLAG );
}

static void CG_TauntKillInsult_f (void ) {
	SendLiteralConsoleCommand( "cmd vsay kill_insult\n" );
}

static void CG_TauntPraise_f (void ) {
	SendLiteralConsoleCommand( "cmd vsay praise\n" );
}

static void CG_TauntTaunt_f (void ) {
	SendLiteralConsoleCommand( "cmd vtaunt\n" );
}

static void CG_TauntDeathInsult_f (void ) {
	SendLiteralConsoleCommand( "cmd vsay death_insult\n" );
}

static void CG_TauntGauntlet_f (void ) {
	SendLiteralConsoleCommand( "cmd vsay kill_gauntlet\n" );
}

static void CG_TaskSuicide_f (void ) {
	const int clientNum = CG_CrosshairPlayer();
	if ( clientNum == -1 ) {
		return;
	}

	SendDirectedLiteralClientCommand( "tell", clientNum, "suicide" );
}



/*
==================
CG_TeamMenu_f
==================
*/
/*
static void CG_TeamMenu_f( void ) {
  if (trap_Key_GetCatcher() & KEYCATCH_CGAME) {
    CG_EventHandling(CGAME_EVENT_NONE);
    trap_Key_SetCatcher(0);
  } else {
    CG_EventHandling(CGAME_EVENT_TEAMMENU);
    //trap_Key_SetCatcher(KEYCATCH_CGAME);
  }
}
*/

/*
==================
CG_EditHud_f
==================
*/
/*
static void CG_EditHud_f( void ) {
  //cls.keyCatchers ^= KEYCATCH_CGAME;
  //VM_Call (cgvm, CG_EVENT_HANDLING, (cls.keyCatchers & KEYCATCH_CGAME) ? CGAME_EVENT_EDITHUD : CGAME_EVENT_NONE);
}
*/

#endif

/*
==================
CG_StartOrbit_f
==================
*/

static void CG_StartOrbit_f( void ) {
	std::array<char, MAX_TOKEN_CHARS> developerValue{};

	trap_Cvar_VariableStringBuffer( "developer", developerValue.data(), developerValue.size() );
	if ( !atoi( developerValue.data() ) ) {
		return;
	}
	if (cg_cameraOrbit.value != 0) {
		trap_Cvar_Set ("cg_cameraOrbit", "0");
		trap_Cvar_Set("cg_thirdPerson", "0");
	} else {
		trap_Cvar_Set("cg_cameraOrbit", "5");
		trap_Cvar_Set("cg_thirdPerson", "1");
		trap_Cvar_Set("cg_thirdPersonAngle", "0");
		trap_Cvar_Set("cg_thirdPersonRange", "100");
	}
}

/*
static void CG_Camera_f( void ) {
	char name[1024];
	trap_Argv( 1, name, sizeof(name));
	if (trap_loadCamera(name)) {
		cg.cameraMode = qtrue;
		trap_startCamera(cg.time);
	} else {
		CG_Printf ("Unable to load camera %s\n",name);
	}
}
*/


typedef struct {
	const char *cmd;
	void	(*function)(void);
} consoleCommand_t;

static const auto commands = std::to_array<consoleCommand_t>( {
	{ "testgun", CG_TestGun_f },
	{ "testmodel", CG_TestModel_f },
	{ "nextframe", CG_TestModelNextFrame_f },
	{ "prevframe", CG_TestModelPrevFrame_f },
	{ "nextskin", CG_TestModelNextSkin_f },
	{ "prevskin", CG_TestModelPrevSkin_f },
	{ "viewpos", CG_Viewpos_f },
	{ "+scores", CG_ScoresDown_f },
	{ "-scores", CG_ScoresUp_f },
	{ "+zoom", CG_ZoomDown_f },
	{ "-zoom", CG_ZoomUp_f },
	{ "sizeup", CG_SizeUp_f },
	{ "sizedown", CG_SizeDown_f },
	{ "weapnext", CG_NextWeapon_f },
	{ "weapprev", CG_PrevWeapon_f },
	{ "weapon", CG_Weapon_f },
	{ "tcmd", CG_TargetCommand_f },
	{ "tell_target", CG_TellTarget_f },
	{ "tell_attacker", CG_TellAttacker_f },
#ifdef MISSIONPACK
	{ "vtell_target", CG_VoiceTellTarget_f },
	{ "vtell_attacker", CG_VoiceTellAttacker_f },
	{ "loadhud", CG_LoadHud_f },
	{ "nextTeamMember", CG_NextTeamMember_f },
	{ "prevTeamMember", CG_PrevTeamMember_f },
	{ "nextOrder", CG_NextOrder_f },
	{ "confirmOrder", CG_ConfirmOrder_f },
	{ "denyOrder", CG_DenyOrder_f },
	{ "taskOffense", CG_TaskOffense_f },
	{ "taskDefense", CG_TaskDefense_f },
	{ "taskPatrol", CG_TaskPatrol_f },
	{ "taskCamp", CG_TaskCamp_f },
	{ "taskFollow", CG_TaskFollow_f },
	{ "taskRetrieve", CG_TaskRetrieve_f },
	{ "taskEscort", CG_TaskEscort_f },
	{ "taskSuicide", CG_TaskSuicide_f },
	{ "taskOwnFlag", CG_TaskOwnFlag_f },
	{ "tauntKillInsult", CG_TauntKillInsult_f },
	{ "tauntPraise", CG_TauntPraise_f },
	{ "tauntTaunt", CG_TauntTaunt_f },
	{ "tauntDeathInsult", CG_TauntDeathInsult_f },
	{ "tauntGauntlet", CG_TauntGauntlet_f },
	{ "spWin", CG_spWin_f },
	{ "spLose", CG_spLose_f },
	{ "scoresDown", CG_scrollScoresDown_f },
	{ "scoresUp", CG_scrollScoresUp_f },
#endif
	{ "startOrbit", CG_StartOrbit_f },
	//{ "camera", CG_Camera_f },
	{ "loaddeferred", CG_LoadDeferredPlayers }	
} );

namespace {

[[nodiscard]] auto CommandEntries() noexcept -> std::span<const consoleCommand_t> {
	return commands;
}

[[nodiscard]] const consoleCommand_t *FindConsoleCommand( const char *cmd ) {
	const auto commandIt = std::find_if( commands.begin(), commands.end(), [cmd]( const consoleCommand_t &command ) {
		return !Q_stricmp( cmd, command.cmd );
	} );

	return commandIt != commands.end() ? &*commandIt : nullptr;
}

} // namespace


/*
=================
CG_ConsoleCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
qboolean CG_ConsoleCommand( void ) {
	if ( const auto *command = FindConsoleCommand( CG_Argv( 0 ) ); command != nullptr ) {
		command->function();
		return qtrue;
	}

	return qfalse;
}


/*
=================
CG_InitConsoleCommands

Let the client system know about all of our commands
so it can perform tab completion
=================
*/
void CG_InitConsoleCommands( void ) {
	for ( const consoleCommand_t &command : CommandEntries() ) {
		trap_AddCommand( command.cmd );
	}

	//
	// the game server will interpret these commands, which will be automatically
	// forwarded to the server after they are not recognized locally
	//
	RegisterForwardedConsoleCommands();
}
