// Copyright (C) 1999-2000 Id Software, Inc.
//
#include <algorithm>
#include <array>

#include "g_local.h"

// g_client.c -- client functions that don't happen every frame

const vec3_t	playerMins = {-15, -15, -24};
const vec3_t	playerMaxs = { 15,  15,  32};

static std::array<char, MAX_CVAR_VALUE_STRING> ban_reason{};
constexpr int MaxSpawnPoints = 64;

gentity_t *SelectSpawnPoint( gentity_t *ent, vec3_t avoidPoint, vec3_t origin, vec3_t angles );
gentity_t *SelectInitialSpawnPoint( gentity_t *ent, vec3_t origin, vec3_t angles );

/*QUAKED info_player_deathmatch (1 0 1) (-16 -16 -24) (16 16 32) initial
potential spawning position for deathmatch games.
The first time a player enters the game, they will be at an 'initial' spot.
Targets will be fired when someone spawns in on them.
"nobots" will prevent bots from using this spot.
"nohumans" will prevent non-bots from using this spot.
*/
void SP_info_player_deathmatch( gentity_t *ent ) {
	int		i;

	G_SpawnInt( "nobots", "0", &i);
	if ( i ) {
		ent->flags |= FL_NO_BOTS;
	}
	G_SpawnInt( "nohumans", "0", &i );
	if ( i ) {
		ent->flags |= FL_NO_HUMANS;
	}
}

/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 32)
equivelant to info_player_deathmatch
*/
void SP_info_player_start(gentity_t *ent) {
	ent->classname = "info_player_deathmatch";
	SP_info_player_deathmatch( ent );
}

/*QUAKED info_player_intermission (1 0 1) (-16 -16 -24) (16 16 32)
The intermission will be viewed from this point.  Target an info_notnull for the view direction.
*/
void SP_info_player_intermission( gentity_t *ent ) {

}



/*
=======================================================================

  SelectSpawnPoint

=======================================================================
*/

/*
================
SpotWouldTelefrag

================
*/
qboolean SpotWouldTelefrag( gentity_t *spot ) {
	int			num;
	std::array<int, MAX_GENTITIES> touch{};
	vec3_t		mins, maxs;

	VectorAdd( spot->s.origin, playerMins, mins );
	VectorAdd( spot->s.origin, playerMaxs, maxs );
	num = trap_EntitiesInBox( mins, maxs, touch.data(), touch.size() );

	for ( int index = 0 ; index < num ; ++index ) {
		gentity_t *hit = &g_entities[touch[index]];
		//if ( hit->client && hit->client->ps.stats[STAT_HEALTH] > 0 ) {
		if ( hit->client) {
			return qtrue;
		}

	}

	return qfalse;
}

namespace {
auto IsBotSpawnerClient( const gentity_t *ent ) -> qboolean {
	return Q_FromBool( ent && ( ent->r.svFlags & SVF_BOT ) == SVF_BOT );
}

auto IsEligibleFreeSpawnSpot( const gentity_t &spot, qboolean checkTelefrag, qboolean checkType, qboolean isBot ) -> qboolean {
	if ( spot.fteam != TEAM_FREE && level.numSpawnSpotsFFA > 0 ) {
		return qfalse;
	}
	if ( checkTelefrag && SpotWouldTelefrag( const_cast<gentity_t *>( &spot ) ) ) {
		return qfalse;
	}
	if ( !checkType ) {
		return qtrue;
	}
	if ( ( spot.flags & FL_NO_BOTS ) && isBot ) {
		return qfalse;
	}
	if ( ( spot.flags & FL_NO_HUMANS ) && !isBot ) {
		return qfalse;
	}
	return qtrue;
}

void InsertSpawnCandidate( std::array<float, MaxSpawnPoints> &distances, std::array<gentity_t *, MaxSpawnPoints> &spots, int &numSpots, float distance, gentity_t *spot ) {
	int insertIndex = 0;
	while ( insertIndex < numSpots && distance <= distances[insertIndex] ) {
		++insertIndex;
	}
	if ( insertIndex >= static_cast<int>( spots.size() ) ) {
		return;
	}

	const int lastIndex = std::min( numSpots, static_cast<int>( spots.size() ) - 1 );
	for ( int index = lastIndex; index > insertIndex; --index ) {
		distances[index] = distances[index - 1];
		spots[index] = spots[index - 1];
	}

	distances[insertIndex] = distance;
	spots[insertIndex] = spot;
	if ( numSpots < static_cast<int>( spots.size() ) ) {
		++numSpots;
	}
}

auto CollectFreeSpawnCandidates( std::array<float, MaxSpawnPoints> &distances, std::array<gentity_t *, MaxSpawnPoints> &spots, const vec3_t avoidPoint, qboolean checkTelefrag, qboolean checkType, qboolean isBot ) -> int {
	int numSpots = 0;
	vec3_t delta;

	for ( int index = 0; index < level.numSpawnSpots; ++index ) {
		gentity_t *spot = level.spawnSpots[index];
		if ( !IsEligibleFreeSpawnSpot( *spot, checkTelefrag, checkType, isBot ) ) {
			continue;
		}

		VectorSubtract( spot->s.origin, avoidPoint, delta );
		InsertSpawnCandidate( distances, spots, numSpots, VectorLength( delta ), spot );
	}

	return numSpots;
}

void ApplySpawnPoint( const gentity_t &spot, vec3_t origin, vec3_t angles ) {
	VectorCopy( spot.s.angles, angles );
	VectorCopy( spot.s.origin, origin );
	origin[2] += 9.0f;
}

auto FindInitialFreeSpawnPoint() -> gentity_t * {
	for ( int index = 0; index < level.numSpawnSpotsFFA; ++index ) {
		gentity_t *spot = level.spawnSpots[index];
		if ( spot->fteam == TEAM_FREE && ( spot->spawnflags & 1 ) ) {
			return spot;
		}
	}
	return nullptr;
}

using InfoStringBuffer = std::array<char, MAX_INFO_STRING>;

auto GetClientUserinfoBuffer( const int client_num ) -> InfoStringBuffer {
	InfoStringBuffer userinfo{};
	trap_GetUserinfo( client_num, userinfo.data(), userinfo.size() );
	return userinfo;
}

auto UserinfoValue( const InfoStringBuffer &userinfo, const char *key ) -> const char * {
	return Info_ValueForKey( userinfo.data(), key );
}

auto UserinfoIntValue( const InfoStringBuffer &userinfo, const char *key ) -> int {
	return atoi( UserinfoValue( userinfo, key ) );
}

auto SuggestedBalancedTeam( const int ignore_client_num ) -> team_t {
	const std::array<int, TEAM_NUM_TEAMS> counts = {
		0,
		TeamCount( ignore_client_num, TEAM_RED ),
		TeamCount( ignore_client_num, TEAM_BLUE ),
		0
	};

	if ( counts[TEAM_BLUE] > counts[TEAM_RED] ) {
		return TEAM_RED;
	}
	if ( counts[TEAM_RED] > counts[TEAM_BLUE] ) {
		return TEAM_BLUE;
	}
	return level.teamScores[TEAM_BLUE] > level.teamScores[TEAM_RED] ? TEAM_RED : TEAM_BLUE;
}

auto IsLocalClientUserinfo( const InfoStringBuffer &userinfo ) -> qboolean {
	return Q_FromBool( !strcmp( UserinfoValue( userinfo, "ip" ), "localhost" ) );
}

auto TeamOverlayEnabled( const InfoStringBuffer &userinfo ) -> qboolean {
	const char *value = UserinfoValue( userinfo, "teamoverlay" );
	return Q_FromBool( !*value || atoi( value ) != 0 );
}

auto ItemPredictionEnabled( const InfoStringBuffer &userinfo ) -> qboolean {
	return Q_FromBool( UserinfoIntValue( userinfo, "cg_predictItems" ) != 0 );
}

template <std::size_t N>
void CopyUserinfoValue( std::array<char, N> &destination, const InfoStringBuffer &userinfo, const char *key ) {
	Q_strncpyz( destination.data(), UserinfoValue( userinfo, key ), destination.size() );
}

auto HandicapMaxHealth( const InfoStringBuffer &userinfo ) -> int {
	const int handicap = UserinfoIntValue( userinfo, "handicap" );
	if ( handicap < 1 || handicap > HEALTH_SOFT_LIMIT ) {
		return HEALTH_SOFT_LIMIT;
	}
	return handicap;
}

#ifdef MISSIONPACK
auto ConfiguredMaxHealth( const gclient_t &client, const InfoStringBuffer &userinfo ) -> int {
	if ( client.ps.powerups[PW_GUARD] ) {
		return HEALTH_SOFT_LIMIT * 2;
	}
	return HandicapMaxHealth( userinfo );
}
#else
auto ConfiguredMaxHealth( const gclient_t &, const InfoStringBuffer &userinfo ) -> int {
	return HandicapMaxHealth( userinfo );
}
#endif

auto SpawnPointAllowsClient( const gentity_t &spawn_point, const gentity_t &entity ) -> bool {
	if ( ( spawn_point.flags & FL_NO_BOTS ) && ( entity.r.svFlags & SVF_BOT ) ) {
		return false;
	}
	if ( ( spawn_point.flags & FL_NO_HUMANS ) && !( entity.r.svFlags & SVF_BOT ) ) {
		return false;
	}
	return true;
}

auto SelectFreeForAllSpawnPoint( gentity_t *ent, gclient_t &client, vec3_t spawn_origin, vec3_t spawn_angles ) -> gentity_t * {
	for ( ;; ) {
		gentity_t *spawn_point = nullptr;
		if ( !client.pers.initialSpawn && client.pers.localClient ) {
			client.pers.initialSpawn = qtrue;
			spawn_point = SelectInitialSpawnPoint( ent, spawn_origin, spawn_angles );
		} else {
			spawn_point = SelectSpawnPoint( ent, client.ps.origin, spawn_origin, spawn_angles );
		}

		if ( SpawnPointAllowsClient( *spawn_point, *ent ) ) {
			return spawn_point;
		}
	}
}

void ResetConnectingEntity( gentity_t &ent, const int client_num ) {
	trap_UnlinkEntity( &ent );
	ent.r.contents = 0;
	ent.s.eType = ET_INVISIBLE;
	ent.s.eFlags = 0;
	ent.s.modelindex = 0;
	ent.s.clientNum = client_num;
	ent.s.number = client_num;
	ent.takedamage = qfalse;
}

auto IsConnectionAdmin( const char *ip, const qboolean is_bot ) -> qboolean {
	return Q_FromBool( !is_bot && !strcmp( ip, "localhost" ) );
}
}


/*
===========
SelectRandomFurthestSpawnPoint

Chooses a player start, deathmatch start, etc
============
*/
static gentity_t *SelectRandomFurthestSpawnPoint( const gentity_t *ent, vec3_t avoidPoint, vec3_t origin, vec3_t angles ) {
	std::array<float, MaxSpawnPoints> listDist{};
	std::array<gentity_t *, MaxSpawnPoints> listSpot{};
	const qboolean isBot = IsBotSpawnerClient( ent );

	for ( int checkMask = 3; checkMask >= 0; --checkMask ) {
		const qboolean checkTelefrag = Q_FromBool( ( checkMask & 1 ) != 0 );
		const qboolean checkType = Q_FromBool( ( checkMask & 2 ) != 0 );
		const int numSpots = CollectFreeSpawnCandidates( listDist, listSpot, avoidPoint, checkTelefrag, checkType, isBot );

		if ( numSpots <= 0 ) {
			continue;
		}

		// select a random spot from the spawn points furthest away
		gentity_t *spot = listSpot[ static_cast<int>( random() * ( numSpots / 2 ) ) ];
		ApplySpawnPoint( *spot, origin, angles );
		return spot;
	}

	G_Error( "Couldn't find a spawn point" );
	return nullptr;
}


/*
===========
SelectSpawnPoint

Chooses a player start, deathmatch start, etc
============
*/
gentity_t *SelectSpawnPoint( gentity_t *ent, vec3_t avoidPoint, vec3_t origin, vec3_t angles ) {
	return SelectRandomFurthestSpawnPoint( ent, avoidPoint, origin, angles );
}


/*
===========
SelectInitialSpawnPoint

Try to find a spawn point marked 'initial', otherwise
use normal spawn selection.
============
*/
gentity_t *SelectInitialSpawnPoint( gentity_t *ent, vec3_t origin, vec3_t angles ) {
	gentity_t *spot = FindInitialFreeSpawnPoint();

	if ( !spot || SpotWouldTelefrag( spot ) ) {
		return SelectSpawnPoint( ent, vec3_origin, origin, angles );
	}

	ApplySpawnPoint( *spot, origin, angles );
	return spot;
}


/*
===========
SelectSpectatorSpawnPoint

============
*/
gentity_t *SelectSpectatorSpawnPoint( vec3_t origin, vec3_t angles ) {
	FindIntermissionPoint();

	VectorCopy( level.intermission_origin, origin );
	VectorCopy( level.intermission_angle, angles );

	return level.spawnSpots[ SPAWN_SPOT_INTERMISSION ]; // was NULL
}


/*
=======================================================================

BODYQUE

=======================================================================
*/

/*
===============
InitBodyQue
===============
*/
void InitBodyQue (void) {
	int		i;
	gentity_t	*ent;

	level.bodyQueIndex = 0;
	for (i=0; i<BODY_QUEUE_SIZE ; i++) {
		ent = G_Spawn();
		ent->classname = "bodyque";
		ent->neverFree = qtrue;
		level.bodyQue[i] = ent;
	}
}

/*
=============
BodySink

After sitting around for five seconds, fall into the ground and dissapear
=============
*/
void BodySink( gentity_t *ent ) {
	if ( level.time - ent->timestamp > 6500 ) {
		// the body ques are never actually freed, they are just unlinked
		trap_UnlinkEntity( ent );
		ent->physicsObject = qfalse;
		return;	
	}
	ent->nextthink = level.time + FRAMETIME;
	ent->s.pos.trBase[2] -= 1;
}


/*
=============
CopyToBodyQue

A player is respawning, so make an entity that looks
just like the existing corpse to leave behind.
=============
*/
void CopyToBodyQue( gentity_t *ent ) {
#ifdef MISSIONPACK
	gentity_t	*e;
	int i;
#endif
	gentity_t		*body;
	int			contents;

	trap_UnlinkEntity (ent);

	// don't leave a corpse if already gibbed
	if ( ent->s.eType == ET_INVISIBLE && ent->health <= GIB_HEALTH ) {
		return;
	}

	// if client is in a nodrop area, don't leave the body
	contents = trap_PointContents( ent->s.origin, -1 );
	if ( contents & CONTENTS_NODROP ) {
		return;
	}

	// grab a body que and cycle to the next one
	body = level.bodyQue[ level.bodyQueIndex ];
	level.bodyQueIndex = (level.bodyQueIndex + 1) % BODY_QUEUE_SIZE;

	trap_UnlinkEntity (body);

	body->s = ent->s;
	body->s.eFlags = EF_DEAD;		// clear EF_TALK, etc
#ifdef MISSIONPACK
	if ( ent->s.eFlags & EF_KAMIKAZE ) {
		body->s.eFlags |= EF_KAMIKAZE;

		// check if there is a kamikaze timer around for this owner
		for (i = 0; i < level.num_entities; i++) {
			e = &g_entities[i];
			if (!e->inuse)
				continue;
			if (e->activator != ent)
				continue;
			if (strcmp(e->classname, "kamikaze timer"))
				continue;
			e->activator = body;
			break;
		}
	}
#endif
	body->s.powerups = 0;	// clear powerups
	body->s.loopSound = 0;	// clear lava burning
	body->s.number = body - g_entities;
	body->timestamp = level.time;
	body->physicsObject = qtrue;
	body->physicsBounce = 0;		// don't bounce
	if ( body->s.groundEntityNum == ENTITYNUM_NONE ) {
		body->s.pos.trType = TR_GRAVITY;
		body->s.pos.trTime = level.time;
		VectorCopy( ent->client->ps.velocity, body->s.pos.trDelta );
	} else {
		body->s.pos.trType = TR_STATIONARY;
	}
	body->s.event = 0;

	// change the animation to the last-frame only, so the sequence
	// doesn't repeat anew for the body
	switch ( body->s.legsAnim & ~ANIM_TOGGLEBIT ) {
	case BOTH_DEATH1:
	case BOTH_DEAD1:
		body->s.torsoAnim = body->s.legsAnim = BOTH_DEAD1;
		break;
	case BOTH_DEATH2:
	case BOTH_DEAD2:
		body->s.torsoAnim = body->s.legsAnim = BOTH_DEAD2;
		break;
	case BOTH_DEATH3:
	case BOTH_DEAD3:
	default:
		body->s.torsoAnim = body->s.legsAnim = BOTH_DEAD3;
		break;
	}

	body->r.svFlags = ent->r.svFlags;
	VectorCopy (ent->r.mins, body->r.mins);
	VectorCopy (ent->r.maxs, body->r.maxs);
	VectorCopy (ent->r.absmin, body->r.absmin);
	VectorCopy (ent->r.absmax, body->r.absmax);

	body->clipmask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP;
	body->r.contents = CONTENTS_CORPSE;
	body->r.ownerNum = ent->s.number;

	body->nextthink = level.time + 5000;
	body->think = BodySink;

	body->die = body_die;

	body->takedamage = ent->takedamage;

	VectorCopy ( body->s.pos.trBase, body->r.currentOrigin );
	trap_LinkEntity( body );
}


//======================================================================

/*
==================
SetClientViewAngle
==================
*/
void SetClientViewAngle( gentity_t *ent, vec3_t angle ) {
	int	i, cmdAngle;
	gclient_t	*client;

	client = ent->client;

	// set the delta angle
	for (i = 0 ; i < 3 ; i++) {
		cmdAngle = ANGLE2SHORT(angle[i]);
		client->ps.delta_angles[i] = cmdAngle - client->pers.cmd.angles[i];
	}
	VectorCopy( angle, ent->s.angles );
	VectorCopy( ent->s.angles, client->ps.viewangles );
}


/*
================
respawn
================
*/
void respawn( gentity_t *ent ) {
	gentity_t	*tent;

	if ( ent->health <= 0 )
		CopyToBodyQue( ent );

	ClientSpawn( ent );

	// bots doesn't need to see any effects
	if ( level.intermissiontime && ent->r.svFlags & SVF_BOT )
		return;

	// add a teleportation effect
	tent = G_TempEntity( ent->client->ps.origin, EV_PLAYER_TELEPORT_IN );
	tent->s.clientNum = ent->s.clientNum;

	// optimize bandwidth
	if ( level.intermissiontime ) {
		tent->r.svFlags = SVF_SINGLECLIENT;
		tent->r.singleClient = ent->s.clientNum;
	}
}


/*
================
TeamCount

Returns number of players on a team
================
*/
int TeamCount( int ignoreClientNum, team_t team ) {
	int		i;
	int		count = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i == ignoreClientNum ) {
			continue;
		}
		if ( level.clients[i].pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( level.clients[i].sess.sessionTeam == team ) {
			count++;
		}
	}

	return count;
}


/*
================
TeamConnectedCount

Returns number of active players on a team
================
*/
int TeamConnectedCount( int ignoreClientNum, team_t team ) {
	int		i;
	int		count = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i == ignoreClientNum ) {
			continue;
		}
		if ( level.clients[i].pers.connected != CON_CONNECTED ) {
			continue;
		}
		if ( level.clients[i].sess.sessionTeam == team ) {
			count++;
		}
	}

	return count;
}


/*
================
TeamLeader

Returns the client number of the team leader
================
*/
int TeamLeader( team_t team ) {
	int		i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( level.clients[i].sess.sessionTeam == team ) {
			if ( level.clients[i].sess.teamLeader )
				return i;
		}
	}

	return -1;
}


/*
================
PickTeam
================
*/
team_t PickTeam( int ignoreClientNum ) {
	return SuggestedBalancedTeam( ignoreClientNum );
}


/*
===========
ClientUserInfoChanged

Called from ClientConnect when the player first connects and
directly by the server system when the player updates a userinfo variable.

The game can override any of the settings and call trap_SetUserinfo
if desired.

returns qfalse in case of invalid userinfo
============
*/
qboolean ClientUserinfoChanged( int clientNum ) {
	gentity_t *ent;
	int		teamTask, teamLeader, health;
	const char	*s;
	std::array<char, MAX_QPATH> model{};
	std::array<char, MAX_QPATH> headModel{};
	std::array<char, MAX_NETNAME> oldname{};
	gclient_t	*client;
	std::array<char, 8> c1{};
	std::array<char, 8> c2{};
	const InfoStringBuffer userinfo = GetClientUserinfoBuffer( clientNum );

	ent = g_entities + clientNum;
	client = ent->client;

	// check for malformed or illegal info strings
	if ( !Info_Validate( userinfo.data() ) ) {
		Q_strcpy( ban_reason.data(), "bad userinfo" );
		if ( client && client->pers.connected != CON_DISCONNECTED )
			trap_DropClient( clientNum, ban_reason.data() );
		return qfalse;
	}

	if ( client->pers.connected == CON_DISCONNECTED ) {
		// we just checked if connecting player can join server
		// so quit now as some important data like player team is still not set
		return qtrue;
	}

	// check for local client
	client->pers.localClient = IsLocalClientUserinfo( userinfo );

	// check the item prediction
	client->pers.predictItemPickup = ItemPredictionEnabled( userinfo );

	// set name
	Q_strncpyz( oldname.data(), client->pers.netname, oldname.size() );
	s = UserinfoValue( userinfo, "name" );
	BG_CleanName( s, client->pers.netname, sizeof( client->pers.netname ), "UnnamedPlayer" );

	if ( client->sess.sessionTeam == TEAM_SPECTATOR ) {
		if ( client->sess.spectatorState == SPECTATOR_SCOREBOARD ) {
			Q_strncpyz( client->pers.netname, "scoreboard", sizeof(client->pers.netname) );
		}
	}

	if ( client->pers.connected == CON_CONNECTED ) {
		if ( strcmp( oldname.data(), client->pers.netname ) ) {
			G_BroadcastServerCommand( -1, va("print \"%s" S_COLOR_WHITE " renamed to %s\n\"", oldname.data(), client->pers.netname) );
		}
	}

	// set max health
	health = ConfiguredMaxHealth( *client, userinfo );
	client->pers.maxHealth = health;
	client->ps.stats[STAT_MAX_HEALTH] = client->pers.maxHealth;

#ifdef MISSIONPACK
	if (g_gametype.integer >= GT_TEAM) {
		client->pers.teamInfo = qtrue;
	} else {
		client->pers.teamInfo = TeamOverlayEnabled( userinfo );
	}
#else
	// teamInfo
	client->pers.teamInfo = TeamOverlayEnabled( userinfo );
#endif

	// set model
	CopyUserinfoValue( model, userinfo, "model" );
	CopyUserinfoValue( headModel, userinfo, "headmodel" );

	// team task (0 = none, 1 = offence, 2 = defence)
	teamTask = UserinfoIntValue( userinfo, "teamtask" );
	// team Leader (1 = leader, 0 is normal player)
	teamLeader = client->sess.teamLeader;

	// colors
	CopyUserinfoValue( c1, userinfo, "color1" );
	CopyUserinfoValue( c2, userinfo, "color2" );

	// send over a subset of the userinfo keys so other clients can
	// print scoreboards, display models, and play custom sounds
	if ( ent->r.svFlags & SVF_BOT ) {
		s = va("n\\%s\\t\\%i\\model\\%s\\hmodel\\%s\\c1\\%s\\c2\\%s\\hc\\%i\\w\\%i\\l\\%i\\skill\\%s\\tt\\%d\\tl\\%d",
			client->pers.netname, client->sess.sessionTeam, model.data(), headModel.data(), c1.data(), c2.data(),
			client->pers.maxHealth, client->sess.wins, client->sess.losses,
			UserinfoValue( userinfo, "skill" ), teamTask, teamLeader );
	} else {
		s = va("n\\%s\\t\\%i\\model\\%s\\hmodel\\%s\\c1\\%s\\c2\\%s\\hc\\%i\\w\\%i\\l\\%i\\tt\\%d\\tl\\%d",
			client->pers.netname, client->sess.sessionTeam, model.data(), headModel.data(), c1.data(), c2.data(), 
			client->pers.maxHealth, client->sess.wins, client->sess.losses, teamTask, teamLeader );
	}

	trap_SetConfigstring( CS_PLAYERS+clientNum, s );

	// this is not the userinfo, more like the configstring actually
	G_LogPrintf( "ClientUserinfoChanged: %i %s\n", clientNum, s );

	return qtrue;
}


/*
===========
ClientConnect

Called when a player begins connecting to the server.
Called again for every map change or tournement restart.

The session information will be valid after exit.

Return NULL if the client should be allowed, otherwise return
a string with the reason for denial.

Otherwise, the client will be sent the current gamestate
and will eventually get to ClientBegin.

firstTime will be qtrue the very first time a client connects
to the server machine, but qfalse on map changes and tournement
restarts.
============
*/
const char *ClientConnect( int clientNum, qboolean firstTime, qboolean isBot ) {
	const char	*value;
//	char		*areabits;
	gclient_t	*client;
	const InfoStringBuffer userinfo = GetClientUserinfoBuffer( clientNum );
	gentity_t	*ent;
	qboolean	isAdmin;

	if ( clientNum >= level.maxclients ) {
		return "Bad connection slot.";
	}

	ent = &g_entities[ clientNum ];
	ent->client = level.clients + clientNum;

	if ( firstTime ) {
		// cleanup previous data manually
		// because client may silently (re)connect without ClientDisconnect in case of crash for example
		if ( level.clients[ clientNum ].pers.connected != CON_DISCONNECTED )
			ClientDisconnect( clientNum );

		// remove old entity from the world
		ResetConnectingEntity( *ent, clientNum );
	}

	ent->r.svFlags &= ~SVF_BOT;
	ent->inuse = qfalse;

 	// IP filtering
 	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=500
 	// recommanding PB based IP / GUID banning, the builtin system is pretty limited
 	// check to see if they are on the banned IP list
	value = UserinfoValue( userinfo, "ip" );
	isAdmin = IsConnectionAdmin( value, isBot );

	if ( !isAdmin && G_FilterPacket( const_cast<char *>( value ) ) ) {
		return "You are banned from this server.";
	}

	// we don't check password for bots and local client
	// NOTE: local client <-> "ip" "localhost"
	// this means this client is not running in our current process
	if ( !isBot && !isAdmin ) {
		// check for a password
		if ( g_password.string[0] && Q_stricmp( g_password.string, "none" ) ) {
			value = UserinfoValue( userinfo, "password" );
			if ( strcmp( g_password.string, value ) )
				return "Invalid password";
		}
	}

	// they can connect
	ent->client = level.clients + clientNum;
	client = ent->client;

//	areabits = client->areabits;
	*client = {};

	client->ps.clientNum = clientNum;

	if ( !ClientUserinfoChanged( clientNum ) ) {
		return ban_reason.data();
	}

	// read or initialize the session data
	if ( firstTime || level.newSession ) {
		value = UserinfoValue( userinfo, "team" );
		G_InitSessionData( client, value, isBot );
		G_WriteClientSessionData( client );
	}

	G_ReadClientSessionData( client );

	if( isBot ) {
		if( !G_BotConnect( clientNum, !firstTime ) ) {
			return "BotConnectfailed";
		}
		ent->r.svFlags |= SVF_BOT;
		client->sess.spectatorClient = clientNum;
	}
	ent->inuse = qtrue;

	// get and distribute relevant paramters
	G_LogPrintf( "ClientConnect: %i\n", clientNum );

	client->pers.connected = CON_CONNECTING;

	ClientUserinfoChanged( clientNum );

	// don't do the "xxx connected" messages if they were caried over from previous level
	if ( firstTime ) {
		G_BroadcastServerCommand( -1, va( "print \"%s" S_COLOR_WHITE " connected\n\"", client->pers.netname ) );

		// mute all prints until completely in game
		client->pers.inGame = qfalse;
	} else {
		client->pers.inGame = qtrue; // FIXME: read from session data?
	}

	// count current clients and rank for scoreboard
	CalculateRanks();

	// for statistics
//	client->areabits = areabits;
//	if ( !client->areabits )
//		client->areabits = G_Alloc( (trap_AAS_PointReachabilityAreaIndex( NULL ) + 7) / 8 );

	return nullptr;
}


/*
===========
ClientBegin

called when a client has finished connecting, and is ready
to be placed into the level.  This will happen every level load,
and on transition between teams, but doesn't happen on respawns
============
*/
void ClientBegin( int clientNum ) {
	gentity_t	*ent;
	gclient_t	*client;
	gentity_t	*tent;
	int			flags;
	int			spawns;

	ent = g_entities + clientNum;

	client = level.clients + clientNum;

	if ( ent->r.linked ) {
		trap_UnlinkEntity( ent );
	}

	G_InitGentity( ent );
	ent->touch = 0;
	ent->pain = 0;
	ent->client = client;

	if ( client->pers.connected == CON_DISCONNECTED )
		return;

	client->pers.connected = CON_CONNECTED;
	client->pers.enterTime = level.time;
	client->pers.teamState.state = TEAM_BEGIN;
	spawns = client->ps.persistant[PERS_SPAWN_COUNT];

	// save eflags around this, because changing teams will
	// cause this to happen with a valid entity, and we
	// want to make sure the teleport bit is set right
	// so the viewpoint doesn't interpolate through the
	// world to the new position
	flags = client->ps.eFlags;
	client->ps = {};
	client->ps.eFlags = flags;
	client->ps.persistant[PERS_SPAWN_COUNT] = spawns;

	// locate ent at a spawn point
	ClientSpawn( ent );

	if ( !client->pers.inGame ) {
		BroadcastTeamChange( client, -1 );
		if ( client->sess.sessionTeam == TEAM_RED || client->sess.sessionTeam == TEAM_BLUE )
			CheckTeamLeader( client->sess.sessionTeam );
	}

	if ( client->sess.sessionTeam != TEAM_SPECTATOR ) {
		// send event
		tent = G_TempEntity( client->ps.origin, EV_PLAYER_TELEPORT_IN );
		tent->s.clientNum = ent->s.clientNum;

		client->sess.spectatorTime = 0;

		if ( g_gametype.integer != GT_TOURNAMENT && !client->pers.inGame ) {
			G_BroadcastServerCommand( -1, va("print \"%s" S_COLOR_WHITE " entered the game\n\"", client->pers.netname) );
		}
	}
	
	client->pers.inGame = qtrue;

	G_LogPrintf( "ClientBegin: %i\n", clientNum );

	// count current clients and rank for scoreboard
	CalculateRanks();
}


/*
===========
ClientSpawn

Called every time a client is placed fresh in the world:
after the first ClientBegin, and after each respawn
Initializes all non-persistant parts of playerState
============
*/
void ClientSpawn(gentity_t *ent) {
	int		index;
	vec3_t	spawn_origin, spawn_angles;
	gclient_t	*client;
	int		i;
	clientPersistant_t	saved;
	clientSession_t		savedSess;
	int		persistant[MAX_PERSISTANT];
	gentity_t	*spawnPoint;
	int		flags;
	int		savedPing;
//	char	*savedAreaBits;
	int		accuracy_hits, accuracy_shots;
	int		eventSequence;
	InfoStringBuffer userinfo{};
	qboolean isSpectator;

	index = ent - g_entities;
	client = ent->client;

	trap_UnlinkEntity( ent );

	isSpectator = client->sess.sessionTeam == TEAM_SPECTATOR;
	// find a spawn point
	// do it before setting health back up, so farthest
	// ranging doesn't count this client
	if ( isSpectator ) {
		spawnPoint = SelectSpectatorSpawnPoint( spawn_origin, spawn_angles );
	} else if (g_gametype.integer >= GT_CTF ) {
		// all base oriented team games use the CTF spawn points
		spawnPoint = SelectCTFSpawnPoint( ent, client->sess.sessionTeam, client->pers.teamState.state, spawn_origin, spawn_angles );
	} else {
		spawnPoint = SelectFreeForAllSpawnPoint( ent, *client, spawn_origin, spawn_angles );
	}
	client->pers.teamState.state = TEAM_ACTIVE;

#ifdef MISSIONPACK
	// always clear the kamikaze flag
	ent->s.eFlags &= ~EF_KAMIKAZE;
#endif

	// toggle the teleport bit so the client knows to not lerp
	// and never clear the voted flag
	flags = client->ps.eFlags & (EF_TELEPORT_BIT | EF_VOTED | EF_TEAMVOTED);
	flags ^= EF_TELEPORT_BIT;

	// unlagged
	G_ResetHistory( ent );
	client->saved.leveltime = 0;

	// clear everything but the persistant data

	saved = client->pers;
	savedSess = client->sess;
	savedPing = client->ps.ping;
//	savedAreaBits = client->areabits;
	accuracy_hits = client->accuracy_hits;
	accuracy_shots = client->accuracy_shots;
	for ( i = 0 ; i < MAX_PERSISTANT ; i++ ) {
		persistant[i] = client->ps.persistant[i];
	}
	eventSequence = client->ps.eventSequence;

	*client = {};

	client->pers = saved;
	client->sess = savedSess;
	client->ps.ping = savedPing;
//	client->areabits = savedAreaBits;
	client->accuracy_hits = accuracy_hits;
	client->accuracy_shots = accuracy_shots;
	client->lastkilled_client = -1;

	for ( i = 0 ; i < MAX_PERSISTANT ; i++ ) {
		client->ps.persistant[i] = persistant[i];
	}
	client->ps.eventSequence = eventSequence;
	// increment the spawncount so the client will detect the respawn
	client->ps.persistant[PERS_SPAWN_COUNT]++;
	client->ps.persistant[PERS_TEAM] = client->sess.sessionTeam;

	client->airOutTime = level.time + 12000;

	userinfo = GetClientUserinfoBuffer( index );
	// set max health
	client->pers.maxHealth = HandicapMaxHealth( userinfo );
	// clear entity values
	client->ps.stats[STAT_MAX_HEALTH] = client->pers.maxHealth;
	client->ps.eFlags = flags;

	ent->s.groundEntityNum = ENTITYNUM_NONE;
	ent->client = &level.clients[index];
	ent->inuse = qtrue;
	ent->classname = "player";
	if ( isSpectator ) {
		ent->takedamage = qfalse;
		ent->r.contents = 0;
		ent->clipmask = MASK_PLAYERSOLID & ~CONTENTS_BODY;
		client->ps.pm_type = PM_SPECTATOR;
	} else {
		ent->takedamage = qtrue;
		ent->r.contents = CONTENTS_BODY;
		ent->clipmask = MASK_PLAYERSOLID;
	}
	ent->die = player_die;
	ent->waterlevel = 0;
	ent->watertype = 0;
	ent->flags = 0;
	
	VectorCopy (playerMins, ent->r.mins);
	VectorCopy (playerMaxs, ent->r.maxs);

	client->ps.clientNum = index;

	client->ps.stats[STAT_WEAPONS] = ( 1 << WP_MACHINEGUN );
	if ( g_gametype.integer == GT_TEAM ) {
		client->ps.ammo[WP_MACHINEGUN] = 50;
	} else {
		client->ps.ammo[WP_MACHINEGUN] = 100;
	}

	client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_GAUNTLET );
	client->ps.ammo[WP_GAUNTLET] = -1;
	client->ps.ammo[WP_GRAPPLING_HOOK] = -1;

	// health will count down towards max_health
	ent->health = client->ps.stats[STAT_HEALTH] = client->ps.stats[STAT_MAX_HEALTH] + 25;

	G_SetOrigin( ent, spawn_origin );
	VectorCopy( spawn_origin, client->ps.origin );

	// the respawned flag will be cleared after the attack and jump keys come up
	client->ps.pm_flags |= PMF_RESPAWNED;

	trap_GetUsercmd( client - level.clients, &ent->client->pers.cmd );
	SetClientViewAngle( ent, spawn_angles );

	// entity should be unlinked before calling G_KillBox()	
	if ( !isSpectator )
		G_KillBox( ent );

	// force the base weapon up
	client->ps.weapon = WP_MACHINEGUN;
	client->ps.weaponstate = WEAPON_READY;

	// don't allow full run speed for a bit
	client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	client->ps.pm_time = 100;

	client->respawnTime = level.time;
	client->inactivityTime = level.time + g_inactivity.integer * 1000;
	client->latched_buttons = 0;

	// set default animations
	client->ps.torsoAnim = TORSO_STAND;
	client->ps.legsAnim = LEGS_IDLE;

	if ( level.intermissiontime ) {
		MoveClientToIntermission( ent );
	} else {
		if ( !isSpectator )
			trap_LinkEntity( ent );
		// fire the targets of the spawn point
		G_UseTargets( spawnPoint, ent );

		// select the highest weapon number available, after any
		// spawn given items have fired
		client->ps.weapon = 1;
		for ( i = WP_NUM_WEAPONS - 1 ; i > 0 ; i-- ) {
			if ( client->ps.stats[STAT_WEAPONS] & ( 1 << i ) ) {
				client->ps.weapon = i;
				break;
			}
		}
	}

	// run a client frame to drop exactly to the floor,
	// initialize animations and other things
	client->ps.commandTime = level.time - 100;
	client->pers.cmd.serverTime = level.time;
	ClientThink( ent-g_entities );

	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
	VectorCopy( client->ps.origin, ent->r.currentOrigin );

	// run the presend to set anything else
	ClientEndFrame( ent );

	// clear entity state values
	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
}


/*
===========
ClientDisconnect

Called when a player drops from the server.
Will not be called between levels.

This should NOT be called directly by any game logic,
call trap_DropClient(), which will call this and do
server system housekeeping.
============
*/
void ClientDisconnect( int clientNum ) {
	gentity_t	*ent;
	gentity_t	*tent;
	int			i;

	// cleanup if we are kicking a bot that
	// hasn't spawned yet
	G_RemoveQueuedBotBegin( clientNum );

	ent = g_entities + clientNum;
	if (!ent->client || ent->client->pers.connected == CON_DISCONNECTED) {
		return;
	}

	// stop any following clients
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].sess.sessionTeam == TEAM_SPECTATOR
			&& level.clients[i].sess.spectatorState == SPECTATOR_FOLLOW
			&& level.clients[i].sess.spectatorClient == clientNum ) {
			StopFollowing( &g_entities[i], qtrue );
		}
	}

	// send effect if they were completely connected
	if ( ent->client->pers.connected == CON_CONNECTED 
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		tent = G_TempEntity( ent->client->ps.origin, EV_PLAYER_TELEPORT_OUT );
		tent->s.clientNum = ent->s.clientNum;

		// They don't get to take powerups with them!
		// Especially important for stuff like CTF flags
		TossClientItems( ent );
#ifdef MISSIONPACK
		TossClientPersistantPowerups( ent );
		if( g_gametype.integer == GT_HARVESTER ) {
			TossClientCubes( ent );
		}
#endif

	}

	G_RevertVote( ent->client );

	G_LogPrintf( "ClientDisconnect: %i\n", clientNum );

	// if we are playing in tourney mode and losing, give a win to the other player
	if ( (g_gametype.integer == GT_TOURNAMENT )
		&& !level.intermissiontime
		&& !level.warmupTime && level.sortedClients[1] == clientNum ) {
		level.clients[ level.sortedClients[0] ].sess.wins++;
		ClientUserinfoChanged( level.sortedClients[0] );
	}

	trap_UnlinkEntity( ent );
	ent->s.modelindex = 0;
	ent->inuse = qfalse;
	ent->classname = "disconnected";
	ent->client->pers.connected = CON_DISCONNECTED;
	ent->client->ps.persistant[PERS_TEAM] = TEAM_FREE;
	ent->client->sess.sessionTeam = TEAM_FREE;

	trap_SetConfigstring( CS_PLAYERS + clientNum, "" );

	G_ClearClientSessionData( ent->client );

	CalculateRanks();

	if ( ent->r.svFlags & SVF_BOT ) {
		BotAIShutdownClient( clientNum, qfalse );
	}
}
