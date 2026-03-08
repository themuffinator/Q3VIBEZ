// Copyright (C) 1999-2000 Id Software, Inc.
//

#include <algorithm>
#include <array>

#include "g_local.h"


typedef struct teamgame_s {
	float			last_flag_capture;
	int				last_capture_team;
	flagStatus_t	redStatus;	// CTF
	flagStatus_t	blueStatus;	// CTF
	flagStatus_t	flagStatus;	// One Flag CTF
	int				redTakenTime;
	int				blueTakenTime;
	int				redObeliskAttackedTime;
	int				blueObeliskAttackedTime;
} teamgame_t;

teamgame_t teamgame;

gentity_t	*neutralObelisk;

static void Team_SetFlagStatus( team_t team, flagStatus_t status );

namespace {
constexpr std::array ctfFlagStatusRemap{ '0', '1', '*', '*', '2' };
constexpr std::array oneFlagStatusRemap{ '0', '1', '2', '3', '4' };

auto ServerCommandTarget( const gentity_t *ent ) -> int {
	return ent == nullptr ? -1 : static_cast<int>( ent - g_entities );
}

void SanitizeServerMessage( std::array<char, 1024> &message ) {
	std::replace( message.begin(), message.end(), '"', '\'' );
}

auto BuildFlagStatusString() -> std::array<char, 4> {
	std::array<char, 4> status{};
	if ( g_gametype.integer == GT_CTF ) {
		status[0] = ctfFlagStatusRemap[teamgame.redStatus];
		status[1] = ctfFlagStatusRemap[teamgame.blueStatus];
	} else {
		status[0] = oneFlagStatusRemap[teamgame.flagStatus];
	}
	return status;
}

auto FlagClassname( team_t team ) -> const char * {
	switch ( team ) {
	case TEAM_RED:
		return "team_CTF_redflag";
	case TEAM_BLUE:
		return "team_CTF_blueflag";
	case TEAM_FREE:
		return "team_CTF_neutralflag";
	default:
		return nullptr;
	}
}

auto EmitGlobalTeamSound( gentity_t *ent, int eventParm, const char *caller ) -> qboolean {
	if ( ent == nullptr ) {
		G_Printf( "Warning:  NULL passed to %s\n", caller );
		return qfalse;
	}

	gentity_t *te = G_TempEntity( ent->s.pos.trBase, EV_GLOBAL_TEAM_SOUND );
	te->s.eventParm = eventParm;
	te->r.svFlags |= SVF_BROADCAST;
	return qtrue;
}

auto CanPlayFlagTakenSound( team_t team ) -> qboolean {
	switch ( team ) {
	case TEAM_RED:
		if ( teamgame.blueStatus != FLAG_ATBASE && teamgame.blueTakenTime > level.time - 10000 ) {
			return qfalse;
		}
		teamgame.blueTakenTime = level.time;
		return qtrue;

	case TEAM_BLUE:
		if ( teamgame.redStatus != FLAG_ATBASE && teamgame.redTakenTime > level.time - 10000 ) {
			return qfalse;
		}
		teamgame.redTakenTime = level.time;
		return qtrue;

	default:
		return qfalse;
	}
}

constexpr float MaxLocationDistanceSquared = 3.0f * 8192.0f * 8192.0f;
constexpr int MaxTeamSpawnPoints = 32;

auto LocationDistanceSquared( const vec3_t origin, const gentity_t &location ) -> float {
	const float deltaX = origin[0] - location.r.currentOrigin[0];
	const float deltaY = origin[1] - location.r.currentOrigin[1];
	const float deltaZ = origin[2] - location.r.currentOrigin[2];
	return deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
}

auto ClampedLocationColorIndex( gentity_t &location ) -> int {
	location.count = std::clamp( location.count, 0, 7 );
	return location.count;
}

auto MatchesTeamSpawnCriteria( const gentity_t &spot, team_t team, int teamstate, qboolean checkState, qboolean checkTelefrag ) -> qboolean {
	if ( spot.fteam != team ) {
		return qfalse;
	}
	if ( checkTelefrag && SpotWouldTelefrag( const_cast<gentity_t *>( &spot ) ) ) {
		return qfalse;
	}
	if ( !checkState ) {
		return qtrue;
	}
	if ( teamstate == TEAM_BEGIN ) {
		return spot.count == 0;
	}
	return spot.count != 0;
}

auto CollectTeamSpawnPoints( std::array<gentity_t *, MaxTeamSpawnPoints> &spots, team_t team, int teamstate, qboolean checkState, qboolean checkTelefrag ) -> int {
	int numSpots = 0;
	for ( int index = 0; index < level.numSpawnSpots; ++index ) {
		gentity_t *spot = level.spawnSpots[index];
		if ( !MatchesTeamSpawnCriteria( *spot, team, teamstate, checkState, checkTelefrag ) ) {
			continue;
		}
		spots[numSpots++] = spot;
		if ( numSpots >= static_cast<int>( spots.size() ) ) {
			break;
		}
	}
	return numSpots;
}
}

void Team_InitGame( void ) {
	teamgame = {};

	switch( g_gametype.integer ) {
	case GT_CTF:
		teamgame.redStatus = static_cast<flagStatus_t>( -1 ); // Invalid to force update
		Team_SetFlagStatus( TEAM_RED, FLAG_ATBASE );
		teamgame.blueStatus = static_cast<flagStatus_t>( -1 ); // Invalid to force update
		Team_SetFlagStatus( TEAM_BLUE, FLAG_ATBASE );
		break;
#ifdef MISSIONPACK
	case GT_1FCTF:
		teamgame.flagStatus = static_cast<flagStatus_t>( -1 ); // Invalid to force update
		Team_SetFlagStatus( TEAM_FREE, FLAG_ATBASE );
		break;
#endif
	default:
		break;
	}
}


team_t OtherTeam( team_t team ) {
	if ( team == TEAM_RED )
		return TEAM_BLUE;
	else if ( team == TEAM_BLUE )
		return TEAM_RED;
	return team;
}


const char *TeamName( team_t team ) {
	if ( team == TEAM_RED )
		return "RED";
	else if ( team == TEAM_BLUE )
		return "BLUE";
	else if ( team == TEAM_SPECTATOR )
		return "SPECTATOR";
	return "FREE";
}


const char *OtherTeamName( team_t team ) {
	if ( team == TEAM_RED )
		return "BLUE";
	else if ( team == TEAM_BLUE )
		return "RED";
	else if ( team == TEAM_SPECTATOR )
		return "SPECTATOR";
	return "FREE";
}


const char *TeamColorString( team_t team ) {
	if ( team == TEAM_RED )
		return S_COLOR_RED;
	else if ( team == TEAM_BLUE )
		return S_COLOR_BLUE;
	else if ( team == TEAM_SPECTATOR )
		return S_COLOR_YELLOW;
	return S_COLOR_WHITE;
}


// NULL for everyone
void QDECL PrintMsg( gentity_t *ent, const char *fmt, ... ) {
	std::array<char, 1024> msg{};
	va_list		argptr;
	
	va_start (argptr,fmt);
	if ( Q_vsprintf( msg.data(), fmt, argptr ) >= msg.size() ) {
		G_Error ( "PrintMsg overrun" );
	}
	va_end (argptr);

	// double quotes are bad
	SanitizeServerMessage( msg );

	trap_SendServerCommand ( ServerCommandTarget( ent ), va("print \"%s\"", msg.data() ));
}


/*
==============
AddTeamScore

 used for gametype > GT_TEAM
 for gametype GT_TEAM the level.teamScores is updated in AddScore in g_combat.c
==============
*/
void AddTeamScore( vec3_t origin, team_t team, int score ) {
	int			eventParm;
	int			otherTeam;
	gentity_t	*te;

	if ( score == 0 ) {
		return;
	}

	eventParm = -1;
	otherTeam = OtherTeam( team );

	if ( level.teamScores[ team ] + score == level.teamScores[ otherTeam ] ) {
		//teams are tied sound
		eventParm = GTS_TEAMS_ARE_TIED;
	} else if ( level.teamScores[ team ] >= level.teamScores[ otherTeam ] &&
				level.teamScores[ team ] + score < level.teamScores[ otherTeam ] ) {
		// other team took the lead sound (negative score)
		eventParm = ( otherTeam == TEAM_RED ) ? GTS_REDTEAM_TOOK_LEAD : GTS_BLUETEAM_TOOK_LEAD;
	} else if ( level.teamScores[ team ] <= level.teamScores[ otherTeam ] &&
				level.teamScores[ team ] + score > level.teamScores[ otherTeam ] ) {
		// this team took the lead sound
		eventParm = ( team == TEAM_RED ) ? GTS_REDTEAM_TOOK_LEAD : GTS_BLUETEAM_TOOK_LEAD;
	} else if ( score > 0 && g_gametype.integer != GT_TEAM ) {
		// team scored sound
		eventParm = ( team == TEAM_RED ) ? GTS_REDTEAM_SCORED : GTS_BLUETEAM_SCORED;
	}

	if ( eventParm != -1 ) {
		te = G_TempEntity(origin, EV_GLOBAL_TEAM_SOUND );
		te->r.svFlags |= SVF_BROADCAST;
		te->s.eventParm = eventParm;
	}

	level.teamScores[ team ] += score;
}

/*
==============
OnSameTeam
==============
*/
qboolean OnSameTeam( gentity_t *ent1, gentity_t *ent2 ) {
	if ( !ent1->client || !ent2->client ) {
		return qfalse;
	}

	if ( g_gametype.integer < GT_TEAM ) {
		return qfalse;
	}

	if ( ent1->client->sess.sessionTeam == ent2->client->sess.sessionTeam ) {
		return qtrue;
	}

	return qfalse;
}
static void Team_SetFlagStatus( team_t team, flagStatus_t status ) {
	qboolean modified = qfalse;

	switch( team ) {
	case TEAM_RED:	// CTF
		if ( teamgame.redStatus != status ) {
			teamgame.redStatus = status;
			modified = qtrue;
		}
		break;

	case TEAM_BLUE:	// CTF
		if ( teamgame.blueStatus != status ) {
			teamgame.blueStatus = status;
			modified = qtrue;
		}
		break;

	case TEAM_FREE:	// One Flag CTF
		if ( teamgame.flagStatus != status ) {
			teamgame.flagStatus = status;
			modified = qtrue;
		}
		break;

	default:
		return;
	}

	if ( modified ) {
		const auto statusString = BuildFlagStatusString();
		trap_SetConfigstring( CS_FLAGSTATUS, statusString.data() );
	}
}


void Team_CheckDroppedItem( gentity_t *dropped ) {
	if( dropped->item->giTag == PW_REDFLAG ) {
		Team_SetFlagStatus( TEAM_RED, FLAG_DROPPED );
	}
	else if( dropped->item->giTag == PW_BLUEFLAG ) {
		Team_SetFlagStatus( TEAM_BLUE, FLAG_DROPPED );
	}
	else if( dropped->item->giTag == PW_NEUTRALFLAG ) {
		Team_SetFlagStatus( TEAM_FREE, FLAG_DROPPED );
	}
}


/*
================
Team_ForceGesture
================
*/
static void Team_ForceGesture( team_t team ) {
	int i;
	gentity_t *ent;

	for ( i = 0; i < level.maxclients; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse )
			continue;
		if ( !ent->client )
			continue;
		if ( ent->client->sess.sessionTeam != team )
			continue;
		//
		ent->flags |= FL_FORCE_GESTURE;
	}
}


/*
================
Team_FragBonuses

Calculate the bonuses for flag defense, flag carrier defense, etc.
Note that bonuses are not cumulative.  You get one, they are in importance
order.
================
*/
void Team_FragBonuses(gentity_t *targ, gentity_t *inflictor, gentity_t *attacker)
{
	int i;
	gentity_t *ent;
	int flag_pw, enemy_flag_pw;
	team_t otherteam;
	int tokens;
	gentity_t *flag, *carrier = NULL;
	char *c;
	vec3_t v1, v2;
	team_t team;

	// no bonus for fragging yourself or team mates
	if (!targ->client || !attacker->client || targ == attacker || OnSameTeam(targ, attacker))
		return;

	team = targ->client->sess.sessionTeam;
	otherteam = OtherTeam(targ->client->sess.sessionTeam);
	if (otherteam < 0)
		return; // whoever died isn't on a team

	// same team, if the flag at base, check to he has the enemy flag
	if (team == TEAM_RED) {
		flag_pw = PW_REDFLAG;
		enemy_flag_pw = PW_BLUEFLAG;
	} else {
		flag_pw = PW_BLUEFLAG;
		enemy_flag_pw = PW_REDFLAG;
	}

#ifdef MISSIONPACK
	if (g_gametype.integer == GT_1FCTF) {
		enemy_flag_pw = PW_NEUTRALFLAG;
	} 
#endif

	// did the attacker frag the flag carrier?
	tokens = 0;
#ifdef MISSIONPACK
	if( g_gametype.integer == GT_HARVESTER ) {
		tokens = targ->client->ps.generic1;
	}
#endif
	if (targ->client->ps.powerups[enemy_flag_pw]) {
		attacker->client->pers.teamState.lastfraggedcarrier = level.time;
		AddScore(attacker, targ->r.currentOrigin, CTF_FRAG_CARRIER_BONUS);
		attacker->client->pers.teamState.fragcarrier++;
		PrintMsg(NULL, "%s" S_COLOR_WHITE " fragged %s's flag carrier!\n",
			attacker->client->pers.netname, TeamName(team));

		// the target had the flag, clear the hurt carrier
		// field on the other team
		for (i = 0; i < level.maxclients; i++) {
			ent = g_entities + i;
			if (ent->inuse && ent->client->sess.sessionTeam == otherteam)
				ent->client->pers.teamState.lasthurtcarrier = 0;
		}
		return;
	}

	// did the attacker frag a head carrier? other->client->ps.generic1
	if (tokens) {
		attacker->client->pers.teamState.lastfraggedcarrier = level.time;
		AddScore(attacker, targ->r.currentOrigin, CTF_FRAG_CARRIER_BONUS * tokens * tokens);
		attacker->client->pers.teamState.fragcarrier++;
		PrintMsg(NULL, "%s" S_COLOR_WHITE " fragged %s's skull carrier!\n",
			attacker->client->pers.netname, TeamName(team));

		// the target had the flag, clear the hurt carrier
		// field on the other team
		for (i = 0; i < level.maxclients; i++) {
			ent = g_entities + i;
			if (ent->inuse && ent->client->sess.sessionTeam == otherteam)
				ent->client->pers.teamState.lasthurtcarrier = 0;
		}
		return;
	}

	if (targ->client->pers.teamState.lasthurtcarrier &&
		level.time - targ->client->pers.teamState.lasthurtcarrier < CTF_CARRIER_DANGER_PROTECT_TIMEOUT &&
		!attacker->client->ps.powerups[flag_pw]) {
		// attacker is on the same team as the flag carrier and
		// fragged a guy who hurt our flag carrier
		AddScore(attacker, targ->r.currentOrigin, CTF_CARRIER_DANGER_PROTECT_BONUS);

		attacker->client->pers.teamState.carrierdefense++;
		targ->client->pers.teamState.lasthurtcarrier = 0;

		attacker->client->ps.persistant[PERS_DEFEND_COUNT]++;
		team = attacker->client->sess.sessionTeam;
		// add the sprite over the player's head
		attacker->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
		attacker->client->ps.eFlags |= EF_AWARD_DEFEND;
		attacker->client->rewardTime = level.time + REWARD_SPRITE_TIME;

		return;
	}

	if (targ->client->pers.teamState.lasthurtcarrier &&
		level.time - targ->client->pers.teamState.lasthurtcarrier < CTF_CARRIER_DANGER_PROTECT_TIMEOUT) {
		// attacker is on the same team as the skull carrier and
		AddScore(attacker, targ->r.currentOrigin, CTF_CARRIER_DANGER_PROTECT_BONUS);

		attacker->client->pers.teamState.carrierdefense++;
		targ->client->pers.teamState.lasthurtcarrier = 0;

		attacker->client->ps.persistant[PERS_DEFEND_COUNT]++;
		team = attacker->client->sess.sessionTeam;
		// add the sprite over the player's head
		attacker->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
		attacker->client->ps.eFlags |= EF_AWARD_DEFEND;
		attacker->client->rewardTime = level.time + REWARD_SPRITE_TIME;

		return;
	}

	// flag and flag carrier area defense bonuses

	// we have to find the flag and carrier entities

#ifdef MISSIONPACK	
	if( g_gametype.integer == GT_OBELISK ) {
		// find the team obelisk
		switch (attacker->client->sess.sessionTeam) {
		case TEAM_RED:
			c = "team_redobelisk";
			break;
		case TEAM_BLUE:
			c = "team_blueobelisk";
			break;		
		default:
			return;
		}
		
	} else if (g_gametype.integer == GT_HARVESTER ) {
		// find the center obelisk
		c = "team_neutralobelisk";
	} else {
#endif
	// find the flag
	switch (attacker->client->sess.sessionTeam) {
	case TEAM_RED:
		c = "team_CTF_redflag";
		break;
	case TEAM_BLUE:
		c = "team_CTF_blueflag";
		break;		
	default:
		return;
	}
	// find attacker's team's flag carrier
	for (i = 0; i < level.maxclients; i++) {
		carrier = g_entities + i;
		if (carrier->inuse && carrier->client->ps.powerups[flag_pw])
			break;
		carrier = NULL;
	}
#ifdef MISSIONPACK
	}
#endif
	flag = NULL;
	while ((flag = G_Find (flag, FOFS(classname), c)) != NULL) {
		if (!(flag->flags & FL_DROPPED_ITEM))
			break;
	}

	if (!flag)
		return; // can't find attacker's flag

	// ok we have the attackers flag and a pointer to the carrier

	// check to see if we are defending the base's flag
	VectorSubtract(targ->r.currentOrigin, flag->r.currentOrigin, v1);
	VectorSubtract(attacker->r.currentOrigin, flag->r.currentOrigin, v2);

	if ( ( ( VectorLength(v1) < CTF_TARGET_PROTECT_RADIUS &&
		trap_InPVS(flag->r.currentOrigin, targ->r.currentOrigin ) ) ||
		( VectorLength(v2) < CTF_TARGET_PROTECT_RADIUS &&
		trap_InPVS(flag->r.currentOrigin, attacker->r.currentOrigin ) ) ) &&
		attacker->client->sess.sessionTeam != targ->client->sess.sessionTeam) {

		// we defended the base flag
		AddScore(attacker, targ->r.currentOrigin, CTF_FLAG_DEFENSE_BONUS);
		attacker->client->pers.teamState.basedefense++;

		attacker->client->ps.persistant[PERS_DEFEND_COUNT]++;
		// add the sprite over the player's head
		attacker->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
		attacker->client->ps.eFlags |= EF_AWARD_DEFEND;
		attacker->client->rewardTime = level.time + REWARD_SPRITE_TIME;

		return;
	}

	if (carrier && carrier != attacker) {
		VectorSubtract(targ->r.currentOrigin, carrier->r.currentOrigin, v1);
		VectorSubtract(attacker->r.currentOrigin, carrier->r.currentOrigin, v1);

		if ( ( ( VectorLength(v1) < CTF_ATTACKER_PROTECT_RADIUS &&
			trap_InPVS(carrier->r.currentOrigin, targ->r.currentOrigin ) ) ||
			( VectorLength(v2) < CTF_ATTACKER_PROTECT_RADIUS &&
				trap_InPVS(carrier->r.currentOrigin, attacker->r.currentOrigin ) ) ) &&
			attacker->client->sess.sessionTeam != targ->client->sess.sessionTeam) {
			AddScore(attacker, targ->r.currentOrigin, CTF_CARRIER_PROTECT_BONUS);
			attacker->client->pers.teamState.carrierdefense++;

			attacker->client->ps.persistant[PERS_DEFEND_COUNT]++;
			// add the sprite over the player's head
			attacker->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
			attacker->client->ps.eFlags |= EF_AWARD_DEFEND;
			attacker->client->rewardTime = level.time + REWARD_SPRITE_TIME;

			return;
		}
	}
}


/*
================
Team_CheckHurtCarrier

Check to see if attacker hurt the flag carrier.  Needed when handing out bonuses for assistance to flag
carrier defense.
================
*/
void Team_CheckHurtCarrier(gentity_t *targ, gentity_t *attacker)
{
	int flag_pw;

	if (!targ->client || !attacker->client)
		return;

	if (targ->client->sess.sessionTeam == TEAM_RED)
		flag_pw = PW_BLUEFLAG;
	else
		flag_pw = PW_REDFLAG;

	// flags
	if (targ->client->ps.powerups[flag_pw] &&
		targ->client->sess.sessionTeam != attacker->client->sess.sessionTeam)
		attacker->client->pers.teamState.lasthurtcarrier = level.time;

	// skulls
	if (targ->client->ps.generic1 &&
		targ->client->sess.sessionTeam != attacker->client->sess.sessionTeam)
		attacker->client->pers.teamState.lasthurtcarrier = level.time;
}


static gentity_t *Team_ResetFlag( team_t team ) {
	const char *classname = FlagClassname( team );
	gentity_t *ent = nullptr;
	gentity_t *rent = nullptr;

	if ( classname == nullptr ) {
		return nullptr;
	}

	while ( ( ent = G_Find( ent, FOFS(classname), classname ) ) != nullptr ) {
		if (ent->flags & FL_DROPPED_ITEM)
			G_FreeEntity(ent);
		else {
			rent = ent;
			RespawnItem(ent);
		}
	}

	Team_SetFlagStatus( team, FLAG_ATBASE );

	return rent;
}


void Team_ResetFlags( void ) {
	if( g_gametype.integer == GT_CTF ) {
		Team_ResetFlag( TEAM_RED );
		Team_ResetFlag( TEAM_BLUE );
	}
#ifdef MISSIONPACK
	else if( g_gametype.integer == GT_1FCTF ) {
		Team_ResetFlag( TEAM_FREE );
	}
#endif
}


static void Team_ReturnFlagSound( gentity_t *ent, team_t team ) {
	EmitGlobalTeamSound( ent, team == TEAM_BLUE ? GTS_RED_RETURN : GTS_BLUE_RETURN, "Team_ReturnFlagSound" );
}


static void Team_TakeFlagSound( gentity_t *ent, team_t team ) {
	// only play sound when the flag was at the base
	// or not picked up the last 10 seconds
	if ( !CanPlayFlagTakenSound( team ) ) {
		return;
	}

	EmitGlobalTeamSound( ent, team == TEAM_BLUE ? GTS_RED_TAKEN : GTS_BLUE_TAKEN, "Team_TakeFlagSound" );
}


static void Team_CaptureFlagSound( gentity_t *ent, team_t team ) {
	EmitGlobalTeamSound( ent, team == TEAM_BLUE ? GTS_BLUE_CAPTURE : GTS_RED_CAPTURE, "Team_CaptureFlagSound" );
}


void Team_ReturnFlag( team_t team ) {
	Team_ReturnFlagSound( Team_ResetFlag( team ), team );
	if( team == TEAM_FREE ) {
		PrintMsg( nullptr, "The flag has returned!\n" );
	}
	else {
		PrintMsg( nullptr, "The %s flag has returned!\n", TeamName(team) );
	}
}


void Team_FreeEntity( gentity_t *ent ) {
	if( ent->item->giTag == PW_REDFLAG ) {
		Team_ReturnFlag( TEAM_RED );
	}
	else if( ent->item->giTag == PW_BLUEFLAG ) {
		Team_ReturnFlag( TEAM_BLUE );
	}
	else if( ent->item->giTag == PW_NEUTRALFLAG ) {
		Team_ReturnFlag( TEAM_FREE );
	}
}


/*
==============
Team_DroppedFlagThink

Automatically set in Launch_Item if the item is one of the flags

Flags are unique in that if they are dropped, the base flag must be respawned when they time out
==============
*/
void Team_DroppedFlagThink(gentity_t *ent) {
	team_t	team = TEAM_FREE;

	if( ent->item->giTag == PW_REDFLAG ) {
		team = TEAM_RED;
	}
	else if( ent->item->giTag == PW_BLUEFLAG ) {
		team = TEAM_BLUE;
	}
	else if( ent->item->giTag == PW_NEUTRALFLAG ) {
		team = TEAM_FREE;
	}

	Team_ReturnFlagSound( Team_ResetFlag( team ), team );
	// Reset Flag will delete this entity
}


/*
==============
Team_DroppedFlagThink
==============
*/
static int Team_TouchOurFlag( gentity_t *ent, gentity_t *other, team_t team ) {
	int			i;
	gentity_t	*player;
	gclient_t	*cl = other->client;
	int			enemy_flag;

#ifdef MISSIONPACK
	if( g_gametype.integer == GT_1FCTF ) {
		enemy_flag = PW_NEUTRALFLAG;
	}
	else {
#endif
	if (cl->sess.sessionTeam == TEAM_RED) {
		enemy_flag = PW_BLUEFLAG;
	} else {
		enemy_flag = PW_REDFLAG;
	}

	if ( ent->flags & FL_DROPPED_ITEM ) {
		// hey, its not home.  return it by teleporting it back
		PrintMsg( NULL, "%s" S_COLOR_WHITE " returned the %s flag!\n", 
			cl->pers.netname, TeamName(team));
		AddScore(other, ent->r.currentOrigin, CTF_RECOVERY_BONUS);
		other->client->pers.teamState.flagrecovery++;
		other->client->pers.teamState.lastreturnedflag = level.time;
		//ResetFlag will remove this entity!  We must return zero
		Team_ReturnFlagSound(Team_ResetFlag(team), team);
		return 0;
	}
#ifdef MISSIONPACK
	}
#endif

	// the flag is at home base.  if the player has the enemy
	// flag, he's just won!
	if (!cl->ps.powerups[enemy_flag])
		return 0; // We don't have the flag
#ifdef MISSIONPACK
	if( g_gametype.integer == GT_1FCTF ) {
		PrintMsg( NULL, "%s" S_COLOR_WHITE " captured the flag!\n", cl->pers.netname );
	}
	else {
#endif
	PrintMsg( NULL, "%s" S_COLOR_WHITE " captured the %s flag!\n", cl->pers.netname, TeamName(OtherTeam(team)));
#ifdef MISSIONPACK
	}
#endif

	cl->ps.powerups[enemy_flag] = 0;

	teamgame.last_flag_capture = level.time;
	teamgame.last_capture_team = team;

	// Increase the team's score
	AddTeamScore(ent->s.pos.trBase, other->client->sess.sessionTeam, 1);
	Team_ForceGesture(other->client->sess.sessionTeam);

	other->client->pers.teamState.captures++;
	// add the sprite over the player's head
	other->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
	other->client->ps.eFlags |= EF_AWARD_CAP;
	other->client->rewardTime = level.time + REWARD_SPRITE_TIME;
	other->client->ps.persistant[PERS_CAPTURES]++;

	// other gets another 10 frag bonus
	AddScore(other, ent->r.currentOrigin, CTF_CAPTURE_BONUS);

	Team_CaptureFlagSound( ent, team );

	// Ok, let's do the player loop, hand out the bonuses
	for (i = 0; i < level.maxclients; i++) {
		player = &g_entities[i];
		if (!player->inuse || player == other)
			continue;

		if (player->client->sess.sessionTeam !=
			cl->sess.sessionTeam) {
			player->client->pers.teamState.lasthurtcarrier = -5;
		} else {
#ifdef MISSIONPACK
				AddScore(player, ent->r.currentOrigin, CTF_TEAM_BONUS);
#endif
			// award extra points for capture assists
			if (player->client->pers.teamState.lastreturnedflag + 
				CTF_RETURN_FLAG_ASSIST_TIMEOUT > level.time) {
				AddScore (player, ent->r.currentOrigin, CTF_RETURN_FLAG_ASSIST_BONUS);
				other->client->pers.teamState.assists++;

				player->client->ps.persistant[PERS_ASSIST_COUNT]++;
				// add the sprite over the player's head
				player->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
				player->client->ps.eFlags |= EF_AWARD_ASSIST;
				player->client->rewardTime = level.time + REWARD_SPRITE_TIME;

			} 
			if (player->client->pers.teamState.lastfraggedcarrier + 
				CTF_FRAG_CARRIER_ASSIST_TIMEOUT > level.time) {
				AddScore(player, ent->r.currentOrigin, CTF_FRAG_CARRIER_ASSIST_BONUS);
				other->client->pers.teamState.assists++;
				player->client->ps.persistant[PERS_ASSIST_COUNT]++;
				// add the sprite over the player's head
				player->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
				player->client->ps.eFlags |= EF_AWARD_ASSIST;
				player->client->rewardTime = level.time + REWARD_SPRITE_TIME;
			}
		}
	}
	Team_ResetFlags();

	CalculateRanks();

	return 0; // Do not respawn this automatically
}


static int Team_TouchEnemyFlag( gentity_t *ent, gentity_t *other, team_t team ) {
	gclient_t *cl = other->client;

#ifdef MISSIONPACK
	if( g_gametype.integer == GT_1FCTF ) {
		PrintMsg (NULL, "%s" S_COLOR_WHITE " got the flag!\n", other->client->pers.netname );

		cl->ps.powerups[PW_NEUTRALFLAG] = INT_MAX; // flags never expire

		if( team == TEAM_RED ) {
			Team_SetFlagStatus( TEAM_FREE, FLAG_TAKEN_RED );
		}
		else {
			Team_SetFlagStatus( TEAM_FREE, FLAG_TAKEN_BLUE );
		}
	}
	else{
#endif
		PrintMsg (NULL, "%s" S_COLOR_WHITE " got the %s flag!\n",
			other->client->pers.netname, TeamName(team));

		if (team == TEAM_RED)
			cl->ps.powerups[PW_REDFLAG] = INT_MAX; // flags never expire
		else
			cl->ps.powerups[PW_BLUEFLAG] = INT_MAX; // flags never expire

		Team_SetFlagStatus( team, FLAG_TAKEN );
#ifdef MISSIONPACK
	}

	AddScore(other, ent->r.currentOrigin, CTF_FLAG_BONUS);
#endif
	cl->pers.teamState.flagsince = level.time;
	Team_TakeFlagSound( ent, team );

	return -1; // Do not respawn this automatically, but do delete it if it was FL_DROPPED
}


int Pickup_Team( gentity_t *ent, gentity_t *other ) {
	team_t team = TEAM_FREE;
	gclient_t *cl = other->client;

#ifdef MISSIONPACK
	if( g_gametype.integer == GT_OBELISK ) {
		// there are no team items that can be picked up in obelisk
		G_FreeEntity( ent );
		return 0;
	}

	if( g_gametype.integer == GT_HARVESTER ) {
		// the only team items that can be picked up in harvester are the cubes
		if( ent->spawnflags != cl->sess.sessionTeam ) {
			cl->ps.generic1 += 1;
		}
		G_FreeEntity( ent );
		return 0;
	}
#endif
	// figure out what team this flag is
	if( strcmp(ent->classname, "team_CTF_redflag") == 0 ) {
		team = TEAM_RED;
	}
	else if( strcmp(ent->classname, "team_CTF_blueflag") == 0 ) {
		team = TEAM_BLUE;
	}
#ifdef MISSIONPACK
	else if( strcmp(ent->classname, "team_CTF_neutralflag") == 0  ) {
		team = TEAM_FREE;
	}
#endif
	else {
		PrintMsg ( other, "Don't know what team the flag is on.\n");
		return 0;
	}
#ifdef MISSIONPACK
	if( g_gametype.integer == GT_1FCTF ) {
		if( team == TEAM_FREE ) {
			return Team_TouchEnemyFlag( ent, other, cl->sess.sessionTeam );
		}
		if( team != cl->sess.sessionTeam) {
			return Team_TouchOurFlag( ent, other, cl->sess.sessionTeam );
		}
		return 0;
	}
#endif
	// GT_CTF
	if( team == cl->sess.sessionTeam) {
		return Team_TouchOurFlag( ent, other, team );
	}
	return Team_TouchEnemyFlag( ent, other, team );
}


/*
===========
Team_GetLocation

Report a location for the player. Uses placed nearby target_location entities
============
*/
gentity_t *Team_GetLocation(gentity_t *ent)
{
	gentity_t		*best;
	float			bestlen;
	vec3_t			origin;

	best = nullptr;
	bestlen = MaxLocationDistanceSquared;

	VectorCopy( ent->r.currentOrigin, origin );

	for ( gentity_t *location = level.locationHead; location; location = location->nextTrain ) {
		const float locationDistance = LocationDistanceSquared( origin, *location );

		if ( locationDistance > bestlen ) {
			continue;
		}

		if ( !trap_InPVS( origin, location->r.currentOrigin ) ) {
			continue;
		}

		bestlen = locationDistance;
		best = location;
	}

	return best;
}


/*
===========
Team_GetLocation

Report a location for the player. Uses placed nearby target_location entities
============
*/
qboolean Team_GetLocationMsg(gentity_t *ent, char *loc, int loclen)
{
	gentity_t *best;

	best = Team_GetLocation( ent );
	
	if (!best)
		return qfalse;

	if (best->count) {
		const int locationColorIndex = ClampedLocationColorIndex( *best );
		Com_sprintf(loc, loclen, "%c%c%s" S_COLOR_WHITE, Q_COLOR_ESCAPE, locationColorIndex + '0', best->message );
	} else
		Com_sprintf(loc, loclen, "%s", best->message);

	return qtrue;
}


/*---------------------------------------------------------------------------*/

/*
================
SelectRandomTeamSpawnPoint

go to a random point that doesn't telefrag
================
*/
gentity_t *SelectRandomTeamSpawnPoint( gentity_t *ent, int teamstate, team_t team ) {
	std::array<gentity_t *, MaxTeamSpawnPoints> spots{};

	if ( team != TEAM_RED && team != TEAM_BLUE )
		return nullptr;

	for ( int checkMask = 3; checkMask >= 0; --checkMask ) {
		const qboolean checkTelefrag = Q_FromBool( ( checkMask & 1 ) != 0 );
		const qboolean checkState = Q_FromBool( ( checkMask & 2 ) != 0 );
		const int numSpots = CollectTeamSpawnPoints( spots, team, teamstate, checkState, checkTelefrag );
		if ( numSpots > 0 ) {
			return spots[ rand() % numSpots ];
		}
	}

	return nullptr;
}


/*
===========
SelectCTFSpawnPoint
============
*/
gentity_t *SelectCTFSpawnPoint( gentity_t *ent, team_t team, int teamstate, vec3_t origin, vec3_t angles ) {
	gentity_t	*spot;

	spot = SelectRandomTeamSpawnPoint( ent, teamstate, team );

	if ( !spot ) {
		return SelectSpawnPoint( ent, vec3_origin, origin, angles );
	}

	VectorCopy( spot->s.origin, origin );
	VectorCopy( spot->s.angles, angles );
	origin[2] += 9.0f;

	return spot;
}

/*
==================
TeamplayLocationsMessage

Format:
	clientNum location health armor weapon powerups

==================
*/
static qboolean IsActiveTeamOverlayPlayer( const gentity_t &entity ) {
	if ( !entity.inuse || !entity.client ) {
		return qfalse;
	}
	if ( entity.client->pers.connected != CON_CONNECTED ) {
		return qfalse;
	}
	return Q_FromBool( entity.client->sess.sessionTeam == TEAM_RED || entity.client->sess.sessionTeam == TEAM_BLUE );
}

static int NonNegativePlayerStat( int value ) {
	return std::max( value, 0 );
}

static qboolean AppendTeamplayEntry( std::array<char, 128> &entry, std::array<char, MAX_STRING_CHARS - 9> &payload, int &payloadLength, int clientNum, const gentity_t &player ) {
	const int health = NonNegativePlayerStat( player.client->ps.stats[STAT_HEALTH] );
	const int armor = NonNegativePlayerStat( player.client->ps.stats[STAT_ARMOR] );
	const int written = BG_sprintf( entry.data(), " %i %i %i %i %i %i",
		clientNum,
		player.client->pers.teamState.location,
		health,
		armor,
		player.client->ps.weapon,
		player.s.powerups );

	if ( payloadLength + written >= static_cast<int>( payload.size() ) ) {
		return qfalse;
	}

	std::copy_n( entry.data(), written + 1, payload.data() + payloadLength );
	payloadLength += written;
	return qtrue;
}

static void UpdateTeamOverlayLocation( gentity_t &entity ) {
	gentity_t *location = Team_GetLocation( &entity );
	entity.client->pers.teamState.location = location ? location->health : 0;
}

void TeamplayInfoMessage( gentity_t *ent ) {
	std::array<char, 128> entry{}; // to fit 6 decimal numbers with spaces
	std::array<char, MAX_STRING_CHARS - 9> payload{}; // -strlen("tinfo nn ")
	int			payloadLength = 0;
	int			count = 0;

	if ( !ent->client->pers.teamInfo )
		return;

	for ( int clientNum = 0; clientNum < level.maxclients && count < TEAM_MAXOVERLAY; ++clientNum ) {
		gentity_t &player = g_entities[clientNum];
		if ( !player.inuse || player.client->sess.sessionTeam != ent->client->sess.sessionTeam ) {
			continue;
		}
		if ( !AppendTeamplayEntry( entry, payload, payloadLength, clientNum, player ) ) {
			break;
		}
		count++;
	}

	trap_SendServerCommand( ServerCommandTarget( ent ), va( "tinfo %i %s", count, payload.data() ) );
}


void CheckTeamStatus( void ) {
	if ( level.time - level.lastTeamLocationTime <= TEAM_LOCATION_UPDATE_TIME ) {
		return;
	}

	level.lastTeamLocationTime = level.time;

	for ( int clientNum = 0; clientNum < level.maxclients; ++clientNum ) {
		gentity_t &entity = g_entities[clientNum];
		if ( !IsActiveTeamOverlayPlayer( entity ) ) {
			continue;
		}
		UpdateTeamOverlayLocation( entity );
	}

	for ( int clientNum = 0; clientNum < level.maxclients; ++clientNum ) {
		gentity_t &entity = g_entities[clientNum];
		if ( !IsActiveTeamOverlayPlayer( entity ) ) {
			continue;
		}
		TeamplayInfoMessage( &entity );
	}
}

/*-----------------------------------------------------------------*/

/*QUAKED team_CTF_redplayer (1 0 0) (-16 -16 -16) (16 16 32)
Only in CTF games.  Red players spawn here at game start.
*/
void SP_team_CTF_redplayer( gentity_t *ent ) {
}


/*QUAKED team_CTF_blueplayer (0 0 1) (-16 -16 -16) (16 16 32)
Only in CTF games.  Blue players spawn here at game start.
*/
void SP_team_CTF_blueplayer( gentity_t *ent ) {
}


/*QUAKED team_CTF_redspawn (1 0 0) (-16 -16 -24) (16 16 32)
potential spawning position for red team in CTF games.
Targets will be fired when someone spawns in on them.
*/
void SP_team_CTF_redspawn(gentity_t *ent) {
}

/*QUAKED team_CTF_bluespawn (0 0 1) (-16 -16 -24) (16 16 32)
potential spawning position for blue team in CTF games.
Targets will be fired when someone spawns in on them.
*/
void SP_team_CTF_bluespawn(gentity_t *ent) {
}


#ifdef MISSIONPACK
/*
================
Obelisks
================
*/

static void ObeliskRegen( gentity_t *self ) {
	self->nextthink = level.time + g_obeliskRegenPeriod.integer * 1000;
	if( self->health >= g_obeliskHealth.integer ) {
		return;
	}

	G_AddEvent( self, EV_POWERUP_REGEN, 0 );
	self->health += g_obeliskRegenAmount.integer;
	if ( self->health > g_obeliskHealth.integer ) {
		self->health = g_obeliskHealth.integer;
	}

	self->activator->s.modelindex2 = self->health * 0xff / g_obeliskHealth.integer;
	self->activator->s.frame = 0;
}


static void ObeliskRespawn( gentity_t *self ) {
	self->takedamage = qtrue;
	self->health = g_obeliskHealth.integer;

	self->think = ObeliskRegen;
	self->nextthink = level.time + g_obeliskRegenPeriod.integer * 1000;

	self->activator->s.frame = 0;
}


static void ObeliskDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	int			otherTeam;

	otherTeam = OtherTeam( self->spawnflags );
	AddTeamScore(self->s.pos.trBase, otherTeam, 1);
	Team_ForceGesture(otherTeam);

	CalculateRanks();

	self->takedamage = qfalse;
	self->think = ObeliskRespawn;
	self->nextthink = level.time + g_obeliskRespawnDelay.integer * 1000;

	self->activator->s.modelindex2 = 0xff;
	self->activator->s.frame = 2;

	G_AddEvent( self->activator, EV_OBELISKEXPLODE, 0 );

	AddScore(attacker, self->r.currentOrigin, CTF_CAPTURE_BONUS);

	// add the sprite over the player's head
	attacker->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
	attacker->client->ps.eFlags |= EF_AWARD_CAP;
	attacker->client->rewardTime = level.time + REWARD_SPRITE_TIME;
	attacker->client->ps.persistant[PERS_CAPTURES]++;

	teamgame.redObeliskAttackedTime = 0;
	teamgame.blueObeliskAttackedTime = 0;
}


static void ObeliskTouch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	int			tokens;

	if ( !other->client ) {
		return;
	}

	if ( OtherTeam(other->client->sess.sessionTeam) != self->spawnflags ) {
		return;
	}

	tokens = other->client->ps.generic1;
	if( tokens <= 0 ) {
		return;
	}

	PrintMsg(NULL, "%s" S_COLOR_WHITE " brought in %i skull%s.\n",
					other->client->pers.netname, tokens, tokens ? "s" : "" );

	AddTeamScore(self->s.pos.trBase, other->client->sess.sessionTeam, tokens);
	Team_ForceGesture(other->client->sess.sessionTeam);

	AddScore(other, self->r.currentOrigin, CTF_CAPTURE_BONUS*tokens);

	// add the sprite over the player's head
	other->client->ps.eFlags &= ~(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP );
	other->client->ps.eFlags |= EF_AWARD_CAP;
	other->client->rewardTime = level.time + REWARD_SPRITE_TIME;
	other->client->ps.persistant[PERS_CAPTURES] += tokens;
	
	other->client->ps.generic1 = 0;
	CalculateRanks();

	Team_CaptureFlagSound( self, self->spawnflags );
}

static void ObeliskPain( gentity_t *self, gentity_t *attacker, int damage ) {
	int actualDamage = damage / 10;
	if (actualDamage <= 0) {
		actualDamage = 1;
	}
	self->activator->s.modelindex2 = self->health * 0xff / g_obeliskHealth.integer;
	if (!self->activator->s.frame) {
		G_AddEvent(self, EV_OBELISKPAIN, 0);
	}
	self->activator->s.frame = 1;
	AddScore(attacker, self->r.currentOrigin, actualDamage);
}

gentity_t *SpawnObelisk( vec3_t origin, int team, int spawnflags) {
	trace_t		tr;
	vec3_t		dest;
	gentity_t	*ent;

	ent = G_Spawn();

	VectorCopy( origin, ent->s.origin );
	VectorCopy( origin, ent->s.pos.trBase );
	VectorCopy( origin, ent->r.currentOrigin );

	VectorSet( ent->r.mins, -15, -15, 0 );
	VectorSet( ent->r.maxs, 15, 15, 87 );

	ent->s.eType = ET_GENERAL;
	ent->flags = FL_NO_KNOCKBACK;

	if( g_gametype.integer == GT_OBELISK ) {
		ent->r.contents = CONTENTS_SOLID;
		ent->takedamage = qtrue;
		ent->health = g_obeliskHealth.integer;
		ent->die = ObeliskDie;
		ent->pain = ObeliskPain;
		ent->think = ObeliskRegen;
		ent->nextthink = level.time + g_obeliskRegenPeriod.integer * 1000;
	}
	if( g_gametype.integer == GT_HARVESTER ) {
		ent->r.contents = CONTENTS_TRIGGER;
		ent->touch = ObeliskTouch;
	}

	if ( spawnflags & 1 ) {
		// suspended
		G_SetOrigin( ent, ent->s.origin );
	} else {
		// mappers like to put them exactly on the floor, but being coplanar
		// will sometimes show up as starting in solid, so lif it up one pixel
		ent->s.origin[2] += 1;

		// drop to floor
		VectorSet( dest, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] - 4096 );
		trap_Trace( &tr, ent->s.origin, ent->r.mins, ent->r.maxs, dest, ent->s.number, MASK_SOLID );
		if ( tr.startsolid ) {
			ent->s.origin[2] -= 1;
			G_Printf( "SpawnObelisk: %s startsolid at %s\n", ent->classname, vtos(ent->s.origin) );

			ent->s.groundEntityNum = ENTITYNUM_NONE;
			G_SetOrigin( ent, ent->s.origin );
		}
		else {
			// allow to ride movers
			ent->s.groundEntityNum = tr.entityNum;
			G_SetOrigin( ent, tr.endpos );
		}
	}

	ent->spawnflags = team;

	trap_LinkEntity( ent );

	return ent;
}

/*QUAKED team_redobelisk (1 0 0) (-16 -16 0) (16 16 8)
*/
void SP_team_redobelisk( gentity_t *ent ) {
	gentity_t *obelisk;

	if ( g_gametype.integer <= GT_TEAM ) {
		G_FreeEntity(ent);
		return;
	}
	ent->s.eType = ET_TEAM;
	if ( g_gametype.integer == GT_OBELISK ) {
		obelisk = SpawnObelisk( ent->s.origin, TEAM_RED, ent->spawnflags );
		obelisk->activator = ent;
		// initial obelisk health value
		ent->s.modelindex2 = 0xff;
		ent->s.frame = 0;
	}
	if ( g_gametype.integer == GT_HARVESTER ) {
		obelisk = SpawnObelisk( ent->s.origin, TEAM_RED, ent->spawnflags );
		obelisk->activator = ent;
	}
	ent->s.modelindex = TEAM_RED;
	trap_LinkEntity(ent);
}

/*QUAKED team_blueobelisk (0 0 1) (-16 -16 0) (16 16 88)
*/
void SP_team_blueobelisk( gentity_t *ent ) {
	gentity_t *obelisk;

	if ( g_gametype.integer <= GT_TEAM ) {
		G_FreeEntity(ent);
		return;
	}
	ent->s.eType = ET_TEAM;
	if ( g_gametype.integer == GT_OBELISK ) {
		obelisk = SpawnObelisk( ent->s.origin, TEAM_BLUE, ent->spawnflags );
		obelisk->activator = ent;
		// initial obelisk health value
		ent->s.modelindex2 = 0xff;
		ent->s.frame = 0;
	}
	if ( g_gametype.integer == GT_HARVESTER ) {
		obelisk = SpawnObelisk( ent->s.origin, TEAM_BLUE, ent->spawnflags );
		obelisk->activator = ent;
	}
	ent->s.modelindex = TEAM_BLUE;
	trap_LinkEntity(ent);
}

/*QUAKED team_neutralobelisk (0 0 1) (-16 -16 0) (16 16 88)
*/
void SP_team_neutralobelisk( gentity_t *ent ) {
	if ( g_gametype.integer != GT_1FCTF && g_gametype.integer != GT_HARVESTER ) {
		G_FreeEntity(ent);
		return;
	}
	ent->s.eType = ET_TEAM;
	if ( g_gametype.integer == GT_HARVESTER) {
		neutralObelisk = SpawnObelisk( ent->s.origin, TEAM_FREE, ent->spawnflags);
		neutralObelisk->spawnflags = TEAM_FREE;
	}
	ent->s.modelindex = TEAM_FREE;
	trap_LinkEntity(ent);
}


/*
================
CheckObeliskAttack
================
*/
qboolean CheckObeliskAttack( gentity_t *obelisk, gentity_t *attacker ) {
	gentity_t	*te;

	// if this really is an obelisk
	if( obelisk->die != ObeliskDie ) {
		return qfalse;
	}

	// if the attacker is a client
	if( !attacker->client ) {
		return qfalse;
	}

	// if the obelisk is on the same team as the attacker then don't hurt it
	if( obelisk->spawnflags == attacker->client->sess.sessionTeam ) {
		return qtrue;
	}

	// obelisk may be hurt

	// if not played any sounds recently
	if ((obelisk->spawnflags == TEAM_RED &&
		teamgame.redObeliskAttackedTime < level.time - OVERLOAD_ATTACK_BASE_SOUND_TIME) ||
		(obelisk->spawnflags == TEAM_BLUE &&
		teamgame.blueObeliskAttackedTime < level.time - OVERLOAD_ATTACK_BASE_SOUND_TIME) ) {

		// tell which obelisk is under attack
		te = G_TempEntity( obelisk->s.pos.trBase, EV_GLOBAL_TEAM_SOUND );
		if( obelisk->spawnflags == TEAM_RED ) {
			te->s.eventParm = GTS_REDOBELISK_ATTACKED;
			teamgame.redObeliskAttackedTime = level.time;
		}
		else {
			te->s.eventParm = GTS_BLUEOBELISK_ATTACKED;
			teamgame.blueObeliskAttackedTime = level.time;
		}
		te->r.svFlags |= SVF_BROADCAST;
	}

	return qfalse;
}
#endif
