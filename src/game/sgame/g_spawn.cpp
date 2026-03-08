// Copyright (C) 1999-2000 Id Software, Inc.
//

#include <algorithm>
#include <array>

#include "g_local.h"

qboolean	G_SpawnString( const char *key, const char *defaultString, char **out ) {
	int		i;

	if ( !level.spawning ) {
		*out = (char *)defaultString;
//		G_Error( "G_SpawnString() called while not spawning" );
	}

	for ( i = 0 ; i < level.numSpawnVars ; i++ ) {
		if ( !Q_stricmp( key, level.spawnVars[i][0] ) ) {
			*out = level.spawnVars[i][1];
			return qtrue;
		}
	}

	*out = (char *)defaultString;
	return qfalse;
}

qboolean	G_SpawnFloat( const char *key, const char *defaultString, float *out ) {
	char		*s;
	qboolean	present;

	present = G_SpawnString( key, defaultString, &s );
	*out = atof( s );
	return present;
}

qboolean	G_SpawnInt( const char *key, const char *defaultString, int *out ) {
	char		*s;
	qboolean	present;

	present = G_SpawnString( key, defaultString, &s );
	*out = atoi( s );
	return present;
}

qboolean	G_SpawnVector( const char *key, const char *defaultString, float *out ) {
	char		*s;
	qboolean	present;

	present = G_SpawnString( key, defaultString, &s );
	Q_sscanf( s, "%f %f %f", &out[0], &out[1], &out[2] );
	return present;
}



//
// fields are needed for spawning from the entity string
//
typedef enum {
	F_INT, 
	F_FLOAT,
	F_LSTRING,			// string on disk, pointer in memory, TAG_LEVEL
	F_GSTRING,			// string on disk, pointer in memory, TAG_GAME
	F_VECTOR,
	F_ANGLEHACK,
	F_ENTITY,			// index on disk, pointer in memory
	F_ITEM,				// index on disk, pointer in memory
	F_CLIENT,			// index on disk, pointer in memory
	F_IGNORE
} fieldtype_t;

struct field_t {
	const char *name;
	intptr_t	ofs;
	fieldtype_t	type;
	//int		flags;
};

struct spawn_t {
	const char *name;
	void	(*spawn)(gentity_t *ent);
};

void SP_info_player_start (gentity_t *ent);
void SP_info_player_deathmatch (gentity_t *ent);
void SP_info_player_intermission (gentity_t *ent);
void SP_info_firstplace(gentity_t *ent);
void SP_info_secondplace(gentity_t *ent);
void SP_info_thirdplace(gentity_t *ent);
void SP_info_podium(gentity_t *ent);

void SP_func_plat (gentity_t *ent);
void SP_func_static (gentity_t *ent);
void SP_func_rotating (gentity_t *ent);
void SP_func_bobbing (gentity_t *ent);
void SP_func_pendulum( gentity_t *ent );
void SP_func_button (gentity_t *ent);
void SP_func_door (gentity_t *ent);
void SP_func_train (gentity_t *ent);
void SP_func_timer (gentity_t *self);

void SP_trigger_always (gentity_t *ent);
void SP_trigger_multiple (gentity_t *ent);
void SP_trigger_push (gentity_t *ent);
void SP_trigger_teleport (gentity_t *ent);
void SP_trigger_hurt (gentity_t *ent);

void SP_target_remove_powerups( gentity_t *ent );
void SP_target_give (gentity_t *ent);
void SP_target_delay (gentity_t *ent);
void SP_target_speaker (gentity_t *ent);
void SP_target_print (gentity_t *ent);
void SP_target_laser (gentity_t *self);
void SP_target_character (gentity_t *ent);
void SP_target_score( gentity_t *ent );
void SP_target_teleporter( gentity_t *ent );
void SP_target_relay (gentity_t *ent);
void SP_target_kill (gentity_t *ent);
void SP_target_position (gentity_t *ent);
void SP_target_location (gentity_t *ent);
void SP_target_push (gentity_t *ent);

void SP_light (gentity_t *self);
void SP_info_null (gentity_t *self);
void SP_info_notnull (gentity_t *self);
void SP_info_camp (gentity_t *self);
void SP_path_corner (gentity_t *self);

void SP_misc_teleporter_dest (gentity_t *self);
void SP_misc_model(gentity_t *ent);
void SP_misc_portal_camera(gentity_t *ent);
void SP_misc_portal_surface(gentity_t *ent);

void SP_shooter_rocket( gentity_t *ent );
void SP_shooter_plasma( gentity_t *ent );
void SP_shooter_grenade( gentity_t *ent );

void SP_team_CTF_redplayer( gentity_t *ent );
void SP_team_CTF_blueplayer( gentity_t *ent );

void SP_team_CTF_redspawn( gentity_t *ent );
void SP_team_CTF_bluespawn( gentity_t *ent );

#ifdef MISSIONPACK
void SP_team_blueobelisk( gentity_t *ent );
void SP_team_redobelisk( gentity_t *ent );
void SP_team_neutralobelisk( gentity_t *ent );
#endif
void SP_item_botroam( gentity_t *ent ) {};
char *G_AddSpawnVarToken( const char *string );

namespace {
const std::array fields{
	field_t{ "classname", FOFS(classname), F_LSTRING },
	field_t{ "origin", FOFS(s.origin), F_VECTOR },
	field_t{ "model", FOFS(model), F_LSTRING },
	field_t{ "model2", FOFS(model2), F_LSTRING },
	field_t{ "spawnflags", FOFS(spawnflags), F_INT },
	field_t{ "speed", FOFS(speed), F_FLOAT },
	field_t{ "target", FOFS(target), F_LSTRING },
	field_t{ "targetname", FOFS(targetname), F_LSTRING },
	field_t{ "message", FOFS(message), F_LSTRING },
	field_t{ "team", FOFS(team), F_LSTRING },
	field_t{ "wait", FOFS(wait), F_FLOAT },
	field_t{ "random", FOFS(random), F_FLOAT },
	field_t{ "count", FOFS(count), F_INT },
	field_t{ "health", FOFS(health), F_INT },
	field_t{ "light", 0, F_IGNORE },
	field_t{ "dmg", FOFS(damage), F_INT },
	field_t{ "angles", FOFS(s.angles), F_VECTOR },
	field_t{ "angle", FOFS(s.angles), F_ANGLEHACK },
	field_t{ "targetShaderName", FOFS(targetShaderName), F_LSTRING },
	field_t{ "targetShaderNewName", FOFS(targetShaderNewName), F_LSTRING },
};

const std::array spawns{
	// info entities don't do anything at all, but provide positional
	// information for things controlled by other processes
	spawn_t{ "info_player_start", SP_info_player_start },
	spawn_t{ "info_player_deathmatch", SP_info_player_deathmatch },
	spawn_t{ "info_player_intermission", SP_info_player_intermission },
	spawn_t{ "info_null", SP_info_null },
	spawn_t{ "info_notnull", SP_info_notnull },		// use target_position instead
	spawn_t{ "info_camp", SP_info_camp },

	spawn_t{ "func_plat", SP_func_plat },
	spawn_t{ "func_button", SP_func_button },
	spawn_t{ "func_door", SP_func_door },
	spawn_t{ "func_static", SP_func_static },
	spawn_t{ "func_rotating", SP_func_rotating },
	spawn_t{ "func_bobbing", SP_func_bobbing },
	spawn_t{ "func_pendulum", SP_func_pendulum },
	spawn_t{ "func_train", SP_func_train },
	spawn_t{ "func_group", SP_info_null },
	spawn_t{ "func_timer", SP_func_timer },			// rename trigger_timer?

	// Triggers are brush objects that cause an effect when contacted
	// by a living player, usually involving firing targets.
	// While almost everything could be done with
	// a single trigger class and different targets, triggered effects
	// could not be client side predicted (push and teleport).
	spawn_t{ "trigger_always", SP_trigger_always },
	spawn_t{ "trigger_multiple", SP_trigger_multiple },
	spawn_t{ "trigger_push", SP_trigger_push },
	spawn_t{ "trigger_teleport", SP_trigger_teleport },
	spawn_t{ "trigger_hurt", SP_trigger_hurt },

	// targets perform no action by themselves, but must be triggered
	// by another entity
	spawn_t{ "target_give", SP_target_give },
	spawn_t{ "target_remove_powerups", SP_target_remove_powerups },
	spawn_t{ "target_delay", SP_target_delay },
	spawn_t{ "target_speaker", SP_target_speaker },
	spawn_t{ "target_print", SP_target_print },
	spawn_t{ "target_laser", SP_target_laser },
	spawn_t{ "target_score", SP_target_score },
	spawn_t{ "target_teleporter", SP_target_teleporter },
	spawn_t{ "target_relay", SP_target_relay },
	spawn_t{ "target_kill", SP_target_kill },
	spawn_t{ "target_position", SP_target_position },
	spawn_t{ "target_location", SP_target_location },
	spawn_t{ "target_push", SP_target_push },

	spawn_t{ "light", SP_light },
	spawn_t{ "path_corner", SP_path_corner },

	spawn_t{ "misc_teleporter_dest", SP_misc_teleporter_dest },
	spawn_t{ "misc_model", SP_misc_model },
	spawn_t{ "misc_portal_surface", SP_misc_portal_surface },
	spawn_t{ "misc_portal_camera", SP_misc_portal_camera },

	spawn_t{ "shooter_rocket", SP_shooter_rocket },
	spawn_t{ "shooter_grenade", SP_shooter_grenade },
	spawn_t{ "shooter_plasma", SP_shooter_plasma },

	spawn_t{ "team_CTF_redplayer", SP_team_CTF_redplayer },
	spawn_t{ "team_CTF_blueplayer", SP_team_CTF_blueplayer },

	spawn_t{ "team_CTF_redspawn", SP_team_CTF_redspawn },
	spawn_t{ "team_CTF_bluespawn", SP_team_CTF_bluespawn },

#ifdef MISSIONPACK
	spawn_t{ "team_redobelisk", SP_team_redobelisk },
	spawn_t{ "team_blueobelisk", SP_team_blueobelisk },
	spawn_t{ "team_neutralobelisk", SP_team_neutralobelisk },
#endif
	spawn_t{ "item_botroam", SP_item_botroam },
};

constexpr std::array gametypeNames{
	"ffa",
	"tournament",
	"single",
	"team",
	"ctf",
	"oneflag",
	"obelisk",
	"harvester",
	"teamtournament",
};

auto FindSpawnField( const char *key ) -> const field_t * {
	for ( const auto &field : fields ) {
		if ( !Q_stricmp( field.name, key ) ) {
			return &field;
		}
	}
	return nullptr;
}

auto FindSpawnHandler( const char *classname ) -> const spawn_t * {
	for ( const auto &spawn : spawns ) {
		if ( !strcmp( spawn.name, classname ) ) {
			return &spawn;
		}
	}
	return nullptr;
}

void ParseVectorField( const char *value, vec3_t out ) {
	Q_sscanf( value, "%f %f %f", &out[0], &out[1], &out[2] );
}

auto GetEntityToken( std::array<char, MAX_TOKEN_CHARS> &token ) -> qboolean {
	return trap_GetEntityToken( token.data(), token.size() );
}

void ReadEntityTokenOrError( std::array<char, MAX_TOKEN_CHARS> &token, const char *error ) {
	if ( !GetEntityToken( token ) ) {
		G_Error( "%s", error );
	}
}

auto CurrentGameTypeName() -> const char * {
	if ( g_gametype.integer < GT_FFA || g_gametype.integer >= GT_MAX_GAME_TYPE ) {
		return nullptr;
	}
	return gametypeNames[g_gametype.integer];
}

void AddSpawnVar( const char *key, const char *value ) {
	if ( level.numSpawnVars == MAX_SPAWN_VARS ) {
		G_Error( "G_ParseSpawnVars: MAX_SPAWN_VARS" );
	}

	level.spawnVars[ level.numSpawnVars ][0] = G_AddSpawnVarToken( key );
	level.spawnVars[ level.numSpawnVars ][1] = G_AddSpawnVarToken( value );
	level.numSpawnVars++;
}
}

/*
===============
G_CallSpawn

Finds the spawn function for the entity and calls it,
returning qfalse if not found
===============
*/
qboolean G_CallSpawn( gentity_t *ent ) {
	gitem_t	*item;

	if ( !ent->classname ) {
		G_Printf ("G_CallSpawn: NULL classname\n");
		return qfalse;
	}

	// check item spawn functions
	for ( item=bg_itemlist+1 ; item->classname ; item++ ) {
		if ( !strcmp(item->classname, ent->classname) ) {
			G_SpawnItem( ent, item );
			return qtrue;
		}
	}

	// check normal spawn functions
	if ( const auto *spawn = FindSpawnHandler( ent->classname ) ) {
		spawn->spawn( ent );
		return qtrue;
	}
	G_Printf ("%s doesn't have a spawn function\n", ent->classname);
	return qfalse;
}

/*
=============
G_NewString

Builds a copy of the string, translating \n to real linefeeds
so message texts can be multi-line
=============
*/
char *G_NewString( const char *string ) {
	char	*newb, *new_p;
	int		i,l;
	
	l = (int)strlen(string) + 1;

	newb = G_Alloc( l );

	new_p = newb;

	// turn \n into a real linefeed
	for ( i=0 ; i< l ; i++ ) {
		if (string[i] == '\\' && i < l-1) {
			i++;
			if (string[i] == 'n') {
				*new_p++ = '\n';
			} else {
				*new_p++ = '\\';
			}
		} else {
			*new_p++ = string[i];
		}
	}
	
	return newb;
}




/*
===============
G_ParseField

Takes a key/value pair and sets the binary values
in a gentity
===============
*/
void G_ParseField( const char *key, const char *value, gentity_t *ent ) {
	byte	*b;
	float	v;
	vec3_t	vec;

	const field_t *f = FindSpawnField( key );
	if ( !f ) {
		return;
	}

	b = (byte *)ent;
	switch( f->type ) {
	case F_LSTRING:
		*(char **)(b+f->ofs) = G_NewString (value);
		break;
	case F_VECTOR:
		ParseVectorField( value, vec );
		((float *)(b+f->ofs))[0] = vec[0];
		((float *)(b+f->ofs))[1] = vec[1];
		((float *)(b+f->ofs))[2] = vec[2];
		break;
	case F_INT:
		*(int *)(b+f->ofs) = atoi(value);
		break;
	case F_FLOAT:
		*(float *)(b+f->ofs) = atof(value);
		break;
	case F_ANGLEHACK:
		v = atof(value);
		((float *)(b+f->ofs))[0] = 0;
		((float *)(b+f->ofs))[1] = v;
		((float *)(b+f->ofs))[2] = 0;
		break;
	default:
	case F_IGNORE:
		break;
	}
}




/*
===================
G_SpawnGEntityFromSpawnVars

Spawn an entity and fill in all of the level fields from
level.spawnVars[], then call the class specfic spawn function
===================
*/
void G_SpawnGEntityFromSpawnVars( void ) {
	int			i;
	gentity_t	*ent;
	char		*s, *value;

	// get the next free entity
	ent = G_Spawn();

	for ( i = 0 ; i < level.numSpawnVars ; i++ ) {
		G_ParseField( level.spawnVars[i][0], level.spawnVars[i][1], ent );
	}

	// check for "notsingle" flag
	if ( g_gametype.integer == GT_SINGLE_PLAYER ) {
		G_SpawnInt( "notsingle", "0", &i );
		if ( i ) {
			G_FreeEntity( ent );
			return;
		}
	}
	// check for "notteam" flag (GT_FFA, GT_TOURNAMENT, GT_SINGLE_PLAYER)
	if ( g_gametype.integer >= GT_TEAM ) {
		G_SpawnInt( "notteam", "0", &i );
		if ( i ) {
			G_FreeEntity( ent );
			return;
		}
	} else {
		G_SpawnInt( "notfree", "0", &i );
		if ( i ) {
			G_FreeEntity( ent );
			return;
		}
	}

#ifdef MISSIONPACK
	G_SpawnInt( "notta", "0", &i );
	if ( i ) {
		G_FreeEntity( ent );
		return;
	}
#else
	G_SpawnInt( "notq3a", "0", &i );
	if ( i ) {
		G_FreeEntity( ent );
		return;
	}
#endif

	if( G_SpawnString( "gametype", NULL, &value ) ) {
		if ( const auto *gametypeName = CurrentGameTypeName() ) {
			s = strstr( value, gametypeName );
			if( !s ) {
				G_FreeEntity( ent );
				return;
			}
		}
	}

	// move editor origin to pos
	VectorCopy( ent->s.origin, ent->s.pos.trBase );
	VectorCopy( ent->s.origin, ent->r.currentOrigin );

	// if we didn't get a classname, don't bother spawning anything
	if ( !G_CallSpawn( ent ) ) {
		G_FreeEntity( ent );
	}
}



/*
====================
G_AddSpawnVarToken
====================
*/
char *G_AddSpawnVarToken( const char *string ) {
	int		l;
	char	*dest;

	l = (int)strlen( string );
	if ( level.numSpawnVarChars + l + 1 > MAX_SPAWN_VARS_CHARS ) {
		G_Error( "G_AddSpawnVarToken: MAX_SPAWN_VARS_CHARS" );
	}

	dest = level.spawnVarChars + level.numSpawnVarChars;
	memcpy( dest, string, l+1 );

	level.numSpawnVarChars += l + 1;

	return dest;
}

/*
====================
G_ParseSpawnVars

Parses a brace bounded set of key / value pairs out of the
level's entity strings into level.spawnVars[]

This does not actually spawn an entity.
====================
*/
qboolean G_ParseSpawnVars( void ) {
	std::array<char, MAX_TOKEN_CHARS> keyname{};
	std::array<char, MAX_TOKEN_CHARS> token{};

	level.numSpawnVars = 0;
	level.numSpawnVarChars = 0;

	// parse the opening brace
	if ( !GetEntityToken( token ) ) {
		// end of spawn string
		return qfalse;
	}
	if ( token[0] != '{' ) {
		G_Error( "G_ParseSpawnVars: found %s when expecting {", token.data() );
	}

	// go through all the key / value pairs
	while ( 1 ) {	
		// parse key
		ReadEntityTokenOrError( keyname, "G_ParseSpawnVars: EOF without closing brace" );

		if ( keyname[0] == '}' ) {
			break;
		}
		
		// parse value	
		ReadEntityTokenOrError( token, "G_ParseSpawnVars: EOF without closing brace" );

		if ( token[0] == '}' ) {
			G_Error( "G_ParseSpawnVars: closing brace without data" );
		}
		AddSpawnVar( keyname.data(), token.data() );
	}

	return qtrue;
}



/*QUAKED worldspawn (0 0 0) ?

Every map should have exactly one worldspawn.
"music"		music wav file
"gravity"	800 is default gravity
"message"	Text to print during connection process
*/
void SP_worldspawn( void ) {
	char	*s;

	G_SpawnString( "classname", "", &s );
	if ( Q_stricmp( s, "worldspawn" ) ) {
		G_Error( "SP_worldspawn: The first entity isn't 'worldspawn'" );
	}

	// make some data visible to connecting client
	trap_SetConfigstring( CS_GAME_VERSION, GAME_VERSION );

	trap_SetConfigstring( CS_LEVEL_START_TIME, va("%i", level.startTime ) );

	G_SpawnString( "music", "", &s );
	trap_SetConfigstring( CS_MUSIC, s );

	G_SpawnString( "message", "", &s );
	trap_SetConfigstring( CS_MESSAGE, s );				// map specific message

	trap_SetConfigstring( CS_MOTD, g_motd.string );		// message of the day

	G_SpawnString( "gravity", "800", &s );
	trap_Cvar_Set( "g_gravity", s );

	G_SpawnString( "enableDust", "0", &s );
	trap_Cvar_Set( "g_enableDust", s );

	G_SpawnString( "enableBreath", "0", &s );
	trap_Cvar_Set( "g_enableBreath", s );

	g_entities[ENTITYNUM_WORLD].s.number = ENTITYNUM_WORLD;
	g_entities[ENTITYNUM_WORLD].r.ownerNum = ENTITYNUM_NONE;
	g_entities[ENTITYNUM_WORLD].classname = "worldspawn";

	g_entities[ENTITYNUM_NONE].s.number = ENTITYNUM_NONE;
	g_entities[ENTITYNUM_NONE].r.ownerNum = ENTITYNUM_NONE;
	g_entities[ENTITYNUM_NONE].classname = "nothing";

	// see if we want a warmup time
	if ( /*g_restarted.integer ||*/ g_gametype.integer == GT_SINGLE_PLAYER ) {
		//trap_Cvar_Set( "g_restarted", "0" );
		level.warmupTime = 0;
		trap_SetConfigstring( CS_WARMUP, "" );
	} else {
		// assume that g_doWarmup is always 1
		level.warmupTime = -1;
		if ( g_warmup.integer > 0 ) {
			trap_SetConfigstring( CS_WARMUP, va( "%i", level.warmupTime ) );
		} else {
			trap_SetConfigstring( CS_WARMUP, "" );
		}
		G_LogPrintf( "Warmup:\n" );
	}
}


/*
==============
G_SpawnEntitiesFromString

Parses textual entity definitions out of an entstring and spawns gentities.
==============
*/
void G_SpawnEntitiesFromString( void ) {
	// allow calls to G_Spawn*()
	level.spawning = qtrue;
	level.numSpawnVars = 0;

	// the worldspawn is not an actual entity, but it still
	// has a "spawn" function to perform any global setup
	// needed by a level (setting configstrings or cvars, etc)
	if ( !G_ParseSpawnVars() ) {
		G_Error( "SpawnEntities: no entities" );
	}
	SP_worldspawn();

	// parse ents
	while( G_ParseSpawnVars() ) {
		G_SpawnGEntityFromSpawnVars();
	}	

	level.spawning = qfalse;			// any future calls to G_Spawn*() will be errors
}
