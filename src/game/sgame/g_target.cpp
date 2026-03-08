// Copyright (C) 1999-2000 Id Software, Inc.
//
#include "g_local.h"

#include <algorithm>
#include <array>

namespace {

void ClearClientPowerups( gclient_t &client ) {
	std::fill_n( client.ps.powerups, MAX_POWERUPS, 0 );
}

void SendCenterPrint( const int client_num, const char *message ) {
	trap_SendServerCommand( client_num, va( "cp \"%s\"", message ) );
}

void SendTeamCenterPrint( const team_t team, const char *message ) {
	G_TeamCommand( team, va( "cp \"%s\"", message ) );
}

void BroadcastCenterPrint( const char *message ) {
	G_BroadcastServerCommand( -1, va( "cp \"%s\"", message ) );
}

void ToggleSpeakerLoopSound( gentity_t &entity ) {
	entity.s.loopSound = entity.s.loopSound ? 0 : entity.noise_index;
}

void PlayTargetSpeakerEvent( gentity_t &entity, gentity_t *activator ) {
	if ( entity.spawnflags & 8 && activator ) {
		G_AddEvent( activator, EV_GENERAL_SOUND, entity.noise_index );
		return;
	}

	if ( entity.spawnflags & 4 ) {
		G_AddEvent( &entity, EV_GLOBAL_SOUND, entity.noise_index );
		return;
	}

	G_AddEvent( &entity, EV_GENERAL_SOUND, entity.noise_index );
}

void BuildSpeakerPath( std::array<char, MAX_QPATH> &buffer, const char *sound_name ) {
	if ( strstr( sound_name, ".wav" ) ) {
		Q_strncpyz( buffer.data(), sound_name, buffer.size() );
		return;
	}

	Com_sprintf( buffer.data(), buffer.size(), "%s.wav", sound_name );
}

bool RelayAllowsActivator( const gentity_t &entity, const gentity_t *activator ) {
	if ( ( entity.spawnflags & 1 ) && activator && activator->client
		&& activator->client->sess.sessionTeam != TEAM_RED ) {
		return false;
	}

	if ( ( entity.spawnflags & 2 ) && activator && activator->client
		&& activator->client->sess.sessionTeam != TEAM_BLUE ) {
		return false;
	}

	return true;
}

void UseRandomRelayTarget( gentity_t *entity, gentity_t *activator ) {
	if ( gentity_t *target = G_PickTarget( entity->target ); target && target->use ) {
		target->use( target, entity, activator );
	}
}

} // namespace

//==========================================================

/*QUAKED target_give (1 0 0) (-8 -8 -8) (8 8 8)
Gives the activator all the items pointed to.
*/
void Use_Target_Give( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	trace_t trace{};

	if ( !activator->client ) {
		return;
	}

	if ( !ent->target ) {
		return;
	}

	for ( gentity_t *target = nullptr; ( target = G_Find( target, FOFS( targetname ), ent->target ) ) != nullptr; ) {
		if ( !target->item ) {
			continue;
		}
		Touch_Item( target, activator, &trace );

		// make sure it isn't going to respawn or show any events
		target->tag = TAG_DONTSPAWN;
		target->nextthink = 0;
		trap_UnlinkEntity( target );
	}
}

void SP_target_give( gentity_t *ent ) {
	ent->use = Use_Target_Give;
}


//==========================================================

/*QUAKED target_remove_powerups (1 0 0) (-8 -8 -8) (8 8 8)
takes away all the activators powerups.
Used to drop flight powerups into death puts.
*/
void Use_target_remove_powerups( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if( !activator->client ) {
		return;
	}

	if( activator->client->ps.powerups[PW_REDFLAG] ) {
		Team_ReturnFlag( TEAM_RED );
	} else if( activator->client->ps.powerups[PW_BLUEFLAG] ) {
		Team_ReturnFlag( TEAM_BLUE );
	} else if( activator->client->ps.powerups[PW_NEUTRALFLAG] ) {
		Team_ReturnFlag( TEAM_FREE );
	}

	ClearClientPowerups( *activator->client );
}

void SP_target_remove_powerups( gentity_t *ent ) {
	ent->use = Use_target_remove_powerups;
}


//==========================================================

/*QUAKED target_delay (1 0 0) (-8 -8 -8) (8 8 8)
"wait" seconds to pause before firing targets.
"random" delay variance, total delay = delay +/- random seconds
*/
void Think_Target_Delay( gentity_t *ent ) {
	G_UseTargets( ent, ent->activator );
}

void Use_Target_Delay( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	ent->nextthink = level.time + ( ent->wait + ent->random * crandom() ) * 1000;
	ent->think = Think_Target_Delay;
	ent->activator = activator;
}

void SP_target_delay( gentity_t *ent ) {
	// check delay for backwards compatability
	if ( !G_SpawnFloat( "delay", "0", &ent->wait ) ) {
		G_SpawnFloat( "wait", "1", &ent->wait );
	}

	if ( !ent->wait ) {
		ent->wait = 1;
	}
	ent->use = Use_Target_Delay;
}


//==========================================================

/*QUAKED target_score (1 0 0) (-8 -8 -8) (8 8 8)
"count" number of points to add, default 1

The activator is given this many points.
*/
void Use_Target_Score (gentity_t *ent, gentity_t *other, gentity_t *activator) {
	if ( !activator )
		return;
	AddScore( activator, ent->r.currentOrigin, ent->count );
}

void SP_target_score( gentity_t *ent ) {
	if ( !ent->count ) {
		ent->count = 1;
	}
	ent->use = Use_Target_Score;
}


//==========================================================

/*QUAKED target_print (1 0 0) (-8 -8 -8) (8 8 8) redteam blueteam private
"message"	text to print
If "private", only the activator gets the message.  If no checks, all clients get the message.
*/
void Use_Target_Print (gentity_t *ent, gentity_t *other, gentity_t *activator) {
	if ( activator && activator->client && ( ent->spawnflags & 4 ) ) {
		SendCenterPrint( activator - g_entities, ent->message );
		return;
	}

	if ( ent->spawnflags & 3 ) {
		if ( ent->spawnflags & 1 ) {
			SendTeamCenterPrint( TEAM_RED, ent->message );
		}
		if ( ent->spawnflags & 2 ) {
			SendTeamCenterPrint( TEAM_BLUE, ent->message );
		}
		return;
	}

	BroadcastCenterPrint( ent->message );
}

void SP_target_print( gentity_t *ent ) {
	ent->use = Use_Target_Print;
}


//==========================================================


/*QUAKED target_speaker (1 0 0) (-8 -8 -8) (8 8 8) looped-on looped-off global activator
"noise"		wav file to play

A global sound will play full volume throughout the level.
Activator sounds will play on the player that activated the target.
Global and activator sounds can't be combined with looping.
Normal sounds play each time the target is used.
Looped sounds will be toggled by use functions.
Multiple identical looping sounds will just increase volume without any speed cost.
"wait" : Seconds between auto triggerings, 0 = don't auto trigger
"random"	wait variance, default is 0
*/
void Use_Target_Speaker (gentity_t *ent, gentity_t *other, gentity_t *activator) {
	if ( ent->spawnflags & 3 ) {	// looping sound toggles
		ToggleSpeakerLoopSound( *ent );
	} else {	// normal sound
		PlayTargetSpeakerEvent( *ent, activator );
	}
}

void SP_target_speaker( gentity_t *ent ) {
	std::array<char, MAX_QPATH> buffer{};
	char	*s;

	G_SpawnFloat( "wait", "0", &ent->wait );
	G_SpawnFloat( "random", "0", &ent->random );

	if ( !G_SpawnString( "noise", "NOSOUND", &s ) ) {
		G_Error( "target_speaker without a noise key at %s", vtos( ent->s.origin ) );
	}

	// force all client relative sounds to be "activator" speakers that
	// play on the entity that activates it
	if ( s[0] == '*' ) {
		ent->spawnflags |= 8;
	}

	BuildSpeakerPath( buffer, s );
	ent->noise_index = G_SoundIndex( buffer.data() );

	// a repeating speaker can be done completely client side
	ent->s.eType = ET_SPEAKER;
	ent->s.eventParm = ent->noise_index;
	ent->s.frame = ent->wait * 10;
	ent->s.clientNum = ent->random * 10;


	// check for prestarted looping sound
	if ( ent->spawnflags & 1 ) {
		ent->s.loopSound = ent->noise_index;
	} else {
		ent->s.loopSound = 0;
	}

	ent->use = Use_Target_Speaker;

	if (ent->spawnflags & 4) {
		ent->r.svFlags |= SVF_BROADCAST;
	}

	VectorCopy( ent->s.origin, ent->s.pos.trBase );

	// must link the entity so we get areas and clusters so
	// the server can determine who to send updates to
	trap_LinkEntity( ent );
}

/*QUAKED q3vibe_env_sound (0 .7 .9) (-8 -8 -8) (8 8 8)
"radius"			Radius in units used for automatic preset selection. Default is 250.
"reverb_preset"	Named reverb preset or numeric preset id.
"reverb_effect_id"	Numeric reverb preset id alias.

Editor-only helper entity for OpenAL environmental reverb zones. The server
does not simulate it; the client reads it from the BSP entity string when
automatic OpenAL reverb selection is enabled.
*/
void SP_q3vibe_env_sound( gentity_t *ent ) {
	G_FreeEntity( ent );
}



//==========================================================

/*QUAKED target_laser (0 .5 .8) (-8 -8 -8) (8 8 8) START_ON
When triggered, fires a laser.  You can either set a target or a direction.
*/
void target_laser_think (gentity_t *self) {
	vec3_t	end;
	trace_t	tr;
	vec3_t	point;

	// if pointed at another entity, set movedir to point at it
	if ( self->enemy ) {
		VectorMA (self->enemy->s.origin, 0.5, self->enemy->r.mins, point);
		VectorMA (point, 0.5, self->enemy->r.maxs, point);
		VectorSubtract (point, self->s.origin, self->movedir);
		VectorNormalize (self->movedir);
	}

	// fire forward and see what we hit
	VectorMA (self->s.origin, 2048, self->movedir, end);

	trap_Trace( &tr, self->s.origin, NULL, NULL, end, self->s.number, CONTENTS_SOLID|CONTENTS_BODY|CONTENTS_CORPSE);

	if ( tr.entityNum ) {
		// hurt it if we can
		G_Damage ( &g_entities[tr.entityNum], self, self->activator, self->movedir, 
			tr.endpos, self->damage, DAMAGE_NO_KNOCKBACK, MOD_TARGET_LASER);
	}

	VectorCopy (tr.endpos, self->s.origin2);

	trap_LinkEntity( self );
	self->nextthink = level.time + FRAMETIME;
}

void target_laser_on (gentity_t *self)
{
	if (!self->activator)
		self->activator = self;
	target_laser_think (self);
}

void target_laser_off (gentity_t *self)
{
	trap_UnlinkEntity( self );
	self->nextthink = 0;
}

void target_laser_use (gentity_t *self, gentity_t *other, gentity_t *activator)
{
	self->activator = activator;
	if ( self->nextthink > 0 )
		target_laser_off (self);
	else
		target_laser_on (self);
}

void target_laser_start (gentity_t *self)
{
	self->s.eType = ET_BEAM;

	if (self->target) {
		gentity_t *target = G_Find( nullptr, FOFS( targetname ), self->target );
		if ( !target ) {
			G_Printf ("%s at %s: %s is a bad target\n", self->classname, vtos(self->s.origin), self->target);
		}
		self->enemy = target;
	} else {
		G_SetMovedir (self->s.angles, self->movedir);
	}

	self->use = target_laser_use;
	self->think = target_laser_think;

	if ( !self->damage ) {
		self->damage = 1;
	}

	if (self->spawnflags & 1)
		target_laser_on (self);
	else
		target_laser_off (self);
}

void SP_target_laser (gentity_t *self)
{
	// let everything else get spawned before we start firing
	self->think = target_laser_start;
	self->nextthink = level.time + FRAMETIME;
}


//==========================================================

void target_teleporter_use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	gentity_t	*dest;

	if ( !activator || !activator->client )
		return;
	dest = 	G_PickTarget( self->target );
	if (!dest) {
		G_Printf ("Couldn't find teleporter destination\n");
		return;
	}

	TeleportPlayer( activator, dest->s.origin, dest->s.angles );
}

/*QUAKED target_teleporter (1 0 0) (-8 -8 -8) (8 8 8)
The activator will be teleported away.
*/
void SP_target_teleporter( gentity_t *self ) {
	if (!self->targetname)
		G_Printf("untargeted %s at %s\n", self->classname, vtos(self->s.origin));

	self->use = target_teleporter_use;
}

//==========================================================


/*QUAKED target_relay (.5 .5 .5) (-8 -8 -8) (8 8 8) RED_ONLY BLUE_ONLY RANDOM
This doesn't perform any actions except fire its targets.
The activator can be forced to be from a certain team.
if RANDOM is checked, only one of the targets will be fired, not all of them
*/
void target_relay_use (gentity_t *self, gentity_t *other, gentity_t *activator) {
	if ( !RelayAllowsActivator( *self, activator ) ) {
		return;
	}

	if ( self->spawnflags & 4 ) {
		UseRandomRelayTarget( self, activator );
		return;
	}

	G_UseTargets (self, activator);
}

void SP_target_relay (gentity_t *self) {
	self->use = target_relay_use;
}


//==========================================================

/*QUAKED target_kill (.5 .5 .5) (-8 -8 -8) (8 8 8)
Kills the activator.
*/
void target_kill_use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	if ( !activator )
		return;
	G_Damage ( activator, NULL, NULL, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
}

void SP_target_kill( gentity_t *self ) {
	self->use = target_kill_use;
}

/*QUAKED target_position (0 0.5 0) (-4 -4 -4) (4 4 4)
Used as a positional target for in-game calculation, like jumppad targets.
*/
void SP_target_position( gentity_t *self ){
	G_SetOrigin( self, self->s.origin );
}

static void target_location_linkup(gentity_t *ent)
{
	int i;
	int n;

	if (level.locationLinked) 
		return;

	level.locationLinked = qtrue;

	level.locationHead = NULL;

	trap_SetConfigstring( CS_LOCATIONS, "unknown" );

	for (i = 0, ent = g_entities, n = 1;
			i < level.num_entities;
			i++, ent++) {
		if (ent->classname && !Q_stricmp(ent->classname, "target_location")) {
			// lets overload some variables!
			ent->health = n; // use for location marking
			trap_SetConfigstring( CS_LOCATIONS + n, ent->message );
			n++;
			ent->nextTrain = level.locationHead;
			level.locationHead = ent;
		}
	}

	// All linked together now
}

/*QUAKED target_location (0 0.5 0) (-8 -8 -8) (8 8 8)
Set "message" to the name of this location.
Set "count" to 0-7 for color.
0:white 1:red 2:green 3:yellow 4:blue 5:cyan 6:magenta 7:white

Closest target_location in sight used for the location, if none
in site, closest in distance
*/
void SP_target_location( gentity_t *self ){
	self->think = target_location_linkup;
	self->nextthink = level.time + 200;  // Let them all spawn first

	G_SetOrigin( self, self->s.origin );
}
