// Copyright (C) 1999-2000 Id Software, Inc.
//
#include <array>
#include <span>

#include "g_local.h"

namespace {
auto SessionCvarName( const gclient_t *client ) -> const char * {
	return va( "session%i", client - level.clients );
}

auto IsSpectatorRequest( const char *team ) -> qboolean {
	return Q_FromBool( team[0] == 's' || team[0] == 'S' );
}

auto RequestedBotTeam( const char *team ) -> team_t {
	if ( team[0] == 'r' || team[0] == 'R' ) {
		return TEAM_RED;
	}
	if ( team[0] == 'b' || team[0] == 'B' ) {
		return TEAM_BLUE;
	}
	return PickTeam( -1 );
}

auto ShouldAutoJoinFreeTeam( qboolean isBot ) -> qboolean {
	return Q_FromBool( ( g_autoJoin.integer & 1 ) != 0 || isBot || g_gametype.integer == GT_SINGLE_PLAYER );
}

auto ChooseTeamGameSessionTeam( const char *team, qboolean isBot ) -> team_t {
	if ( IsSpectatorRequest( team ) ) {
		return TEAM_SPECTATOR;
	}
	if ( g_autoJoin.integer & 2 ) {
		return PickTeam( -1 );
	}
	if ( isBot == qfalse ) {
		return TEAM_SPECTATOR;
	}
	return RequestedBotTeam( team );
}

auto ChooseNonTeamSessionTeam( const char *team, qboolean isBot ) -> team_t {
	if ( IsSpectatorRequest( team ) ) {
		return TEAM_SPECTATOR;
	}

	switch ( g_gametype.integer ) {
	default:
	case GT_FFA:
	case GT_SINGLE_PLAYER:
		if ( g_maxGameClients.integer > 0 && level.numNonSpectatorClients >= g_maxGameClients.integer ) {
			return TEAM_SPECTATOR;
		}
		return ShouldAutoJoinFreeTeam( isBot ) ? TEAM_FREE : TEAM_SPECTATOR;

	case GT_TOURNAMENT:
		if ( level.numNonSpectatorClients >= 2 ) {
			return TEAM_SPECTATOR;
		}
		return ( ( g_autoJoin.integer & 1 ) != 0 || isBot ) ? TEAM_FREE : TEAM_SPECTATOR;
	}
}

auto ChooseInitialSessionTeam( const char *team, qboolean isBot ) -> team_t {
	if ( g_gametype.integer >= GT_TEAM ) {
		return ChooseTeamGameSessionTeam( team, isBot );
	}
	return ChooseNonTeamSessionTeam( team, isBot );
}

auto ActiveClients() -> std::span<gclient_t> {
	return { level.clients, static_cast<std::size_t>( level.maxclients ) };
}
}


/*
=======================================================================

  SESSION DATA

Session data is the only data that stays persistant across level loads
and tournament restarts.
=======================================================================
*/

/*
================
G_WriteClientSessionData

Called on game shutdown
================
*/
void G_WriteClientSessionData( gclient_t *client ) {
	const char	*s;

	s = va("%i %i %i %i %i %i %i", 
		client->sess.sessionTeam,
		client->sess.spectatorTime,
		client->sess.spectatorState,
		client->sess.spectatorClient,
		client->sess.wins,
		client->sess.losses,
		client->sess.teamLeader
		);

	trap_Cvar_Set( SessionCvarName( client ), s );
}


/*
================
G_ReadSessionData

Called on a reconnect
================
*/
void G_ReadClientSessionData( gclient_t *client ) {
	std::array<char, MAX_STRING_CHARS> sessionString{};
	int teamLeader;
	int spectatorState;
	int sessionTeam;

	trap_Cvar_VariableStringBuffer( SessionCvarName( client ), sessionString.data(), sessionString.size() );

	Q_sscanf( sessionString.data(), "%i %i %i %i %i %i %i",
		&sessionTeam,
		&client->sess.spectatorTime,
		&spectatorState,
		&client->sess.spectatorClient,
		&client->sess.wins,
		&client->sess.losses,
		&teamLeader
		);

	client->sess.sessionTeam = (team_t)sessionTeam;
	client->sess.spectatorState = (spectatorState_t)spectatorState;
	client->sess.teamLeader = Q_FromBool( teamLeader != 0 );

	if ( (unsigned)client->sess.sessionTeam >= TEAM_NUM_TEAMS ) {
		client->sess.sessionTeam = TEAM_SPECTATOR;
	}
}


/*
================
G_ClearClientSessionData
================
*/
void G_ClearClientSessionData( gclient_t *client )
{
	trap_Cvar_Set( SessionCvarName( client ), "" );
}


/*
================
G_InitSessionData

Called on a first-time connect
================
*/
void G_InitSessionData( gclient_t *client, const char *team, qboolean isBot ) {
	clientSession_t	*sess;
	
	sess = &client->sess;

	sess->sessionTeam = ChooseInitialSessionTeam( team, isBot );

	sess->spectatorState = SPECTATOR_FREE;
	sess->spectatorTime = 0;
}


/*
==================
G_InitWorldSession

==================
*/
void G_InitWorldSession( void ) {
	std::array<char, MAX_STRING_CHARS> sessionString{};
	int			gt;

	trap_Cvar_VariableStringBuffer( "session", sessionString.data(), sessionString.size() );
	gt = atoi( sessionString.data() );
	
	// if the gametype changed since the last session, don't use any
	// client sessions
	if ( !sessionString[0] || g_gametype.integer != gt ) {
		level.newSession = qtrue;
		G_Printf( "Gametype changed, clearing session data.\n" );
	}
}


/*
==================
G_WriteSessionData

==================
*/
void G_WriteSessionData( void ) {
	trap_Cvar_Set( "session", va("%i", g_gametype.integer) );

	for ( auto &client : ActiveClients() ) {
		if ( client.pers.connected != CON_DISCONNECTED ) {
			G_WriteClientSessionData( &client );
		}
	}
}
