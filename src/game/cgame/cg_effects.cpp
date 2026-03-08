// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_effects.c -- these functions generate localentities, usually as a result
// of event processing

#include <array>

#include "cg_local.h"

namespace {

void SetRefEntityShaderTime( refEntity_t &ref_entity, int shader_time_ms ) {
	if ( intShaderTime ) {
		ref_entity.u.intShaderTime = shader_time_ms;
	} else {
		ref_entity.u.shaderTime = shader_time_ms / 1000.0f;
	}
}

void InitializeTimedLocalEntity( localEntity_t &entity, leType_t type, int start_time, int end_time, int le_flags = 0 ) {
	entity.leFlags = le_flags;
	entity.leType = type;
	entity.startTime = start_time;
	entity.endTime = end_time;
	entity.lifeRate = 1.0 / ( entity.endTime - entity.startTime );
}

void SetLocalEntityColor( localEntity_t &entity, float red, float green, float blue, float alpha ) {
	entity.color[0] = red;
	entity.color[1] = green;
	entity.color[2] = blue;
	entity.color[3] = alpha;
}

void SetLocalEntityColorWhite( localEntity_t &entity ) {
	SetLocalEntityColor( entity, 1.0f, 1.0f, 1.0f, 1.0f );
}

void InitializeFragmentLocalEntity(
	localEntity_t &entity,
	const vec3_t origin,
	const vec3_t velocity,
	qhandle_t model,
	int lifetime,
	float bounce_factor,
	leBounceSoundType_t bounce_sound_type,
	leMarkType_t mark_type ) {
	refEntity_t &ref_entity = entity.refEntity;

	InitializeTimedLocalEntity( entity, LE_FRAGMENT, cg.time, cg.time + lifetime );

	VectorCopy( origin, ref_entity.origin );
	AxisCopy( axisDefault, ref_entity.axis );
	ref_entity.hModel = model;

	entity.pos.trType = TR_GRAVITY;
	VectorCopy( origin, entity.pos.trBase );
	VectorCopy( velocity, entity.pos.trDelta );
	entity.pos.trTime = cg.time;

	entity.bounceFactor = bounce_factor;
	entity.leBounceSoundType = bounce_sound_type;
	entity.leMarkType = mark_type;
}

void SetRandomLaunchVelocity( vec3_t velocity, float horizontal_speed, float vertical_base ) {
	velocity[0] = crandom() * horizontal_speed;
	velocity[1] = crandom() * horizontal_speed;
	velocity[2] = vertical_base + crandom() * horizontal_speed;
}

void LaunchFragmentModel(
	const vec3_t origin,
	const vec3_t velocity,
	qhandle_t model,
	int lifetime,
	float bounce_factor,
	leBounceSoundType_t bounce_sound_type,
	leMarkType_t mark_type ) {
	localEntity_t *entity = CG_AllocLocalEntity();
	InitializeFragmentLocalEntity( *entity, origin, velocity, model, lifetime, bounce_factor, bounce_sound_type, mark_type );
}

} // namespace


/*
==================
CG_BubbleTrail

Bullets shot underwater
==================
*/
void CG_BubbleTrail( const vec3_t start, const vec3_t end, float spacing ) {
	vec3_t		move;
	vec3_t		vec;
	float		len;
	int			i;

	if ( cg_noProjectileTrail.integer ) {
		return;
	}

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	// advance a random amount first
	i = rand() % (int)spacing;
	VectorMA( move, i, vec, move );

	VectorScale (vec, spacing, vec);

	for ( ; i < len; i += spacing ) {
		localEntity_t	*le;
		refEntity_t		*re;

		le = CG_AllocLocalEntity();
		InitializeTimedLocalEntity( *le, LE_MOVE_SCALE_FADE, cg.time, cg.time + 1000 + random() * 250, LEF_PUFF_DONT_SCALE );

		re = &le->refEntity;
		SetRefEntityShaderTime( *re, cg.time );

		re->reType = RT_SPRITE;
		re->rotation = 0;
		re->radius = 3;
		re->customShader = cgs.media.waterBubbleShader;
		re->shaderRGBA[0] = 0xff;
		re->shaderRGBA[1] = 0xff;
		re->shaderRGBA[2] = 0xff;
		re->shaderRGBA[3] = 0xff;

		le->color[3] = 1.0f;

		le->pos.trType = TR_LINEAR;
		le->pos.trTime = cg.time;
		VectorCopy( move, le->pos.trBase );
		le->pos.trDelta[0] = crandom()*5;
		le->pos.trDelta[1] = crandom()*5;
		le->pos.trDelta[2] = crandom()*5 + 6;

		VectorAdd (move, vec, move);
	}
}

/*
=====================
CG_SmokePuff

Adds a smoke puff or blood trail localEntity.
=====================
*/
localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, 
				   float radius,
				   float r, float g, float b, float a,
				   float duration,
				   int startTime,
				   int fadeInTime,
				   int leFlags,
				   qhandle_t hShader ) {
	static int	seed = 0x92;
	localEntity_t	*le;
	refEntity_t		*re;
//	int fadeInTime = startTime + duration / 2;

	le = CG_AllocLocalEntity();
	InitializeTimedLocalEntity( *le, LE_MOVE_SCALE_FADE, startTime, startTime + duration, leFlags );
	le->radius = radius;

	re = &le->refEntity;
	re->rotation = Q_random( &seed ) * 360;
	re->radius = radius;

	SetRefEntityShaderTime( *re, startTime );

	le->fadeInTime = fadeInTime;
	if ( fadeInTime > startTime ) {
		le->lifeRate = 1.0 / ( le->endTime - le->fadeInTime );
	}
	else {
		le->lifeRate = 1.0 / ( le->endTime - le->startTime );
	}
	SetLocalEntityColor( *le, r, g, b, a );


	le->pos.trType = TR_LINEAR;
	le->pos.trTime = startTime;
	VectorCopy( vel, le->pos.trDelta );
	VectorCopy( p, le->pos.trBase );

	VectorCopy( p, re->origin );
	re->customShader = hShader;

	// rage pro can't alpha fade, so use a different shader
	if ( cgs.glconfig.hardwareType == GLHW_RAGEPRO ) {
		re->customShader = cgs.media.smokePuffRageProShader;
		re->shaderRGBA[0] = 0xff;
		re->shaderRGBA[1] = 0xff;
		re->shaderRGBA[2] = 0xff;
		re->shaderRGBA[3] = 0xff;
	} else {
		re->shaderRGBA[0] = le->color[0] * 0xff;
		re->shaderRGBA[1] = le->color[1] * 0xff;
		re->shaderRGBA[2] = le->color[2] * 0xff;
		re->shaderRGBA[3] = 0xff;
	}

	re->reType = RT_SPRITE;
	re->radius = le->radius;

	return le;
}

/*
==================
CG_SpawnEffect

Player teleporting in or out
==================
*/
void CG_SpawnEffect( const vec3_t origin ) {
	localEntity_t	*le;
	refEntity_t		*re;

	le = CG_AllocLocalEntity();
	InitializeTimedLocalEntity( *le, LE_FADE_RGB, cg.time, cg.time + 500 );
	SetLocalEntityColorWhite( *le );

	re = &le->refEntity;

	re->reType = RT_MODEL;

	SetRefEntityShaderTime( *re, cg.time );

#ifndef MISSIONPACK
	re->customShader = cgs.media.teleportEffectShader;
#endif
	re->hModel = cgs.media.teleportEffectModel;
	AxisClear( re->axis );

	VectorCopy( origin, re->origin );

#ifdef MISSIONPACK
	re->origin[2] += 16;
#else
	re->origin[2] -= 24;
#endif
}


#ifdef MISSIONPACK
/*
===============
CG_LightningBoltBeam
===============
*/
void CG_LightningBoltBeam( vec3_t start, vec3_t end ) {
	localEntity_t	*le;
	refEntity_t		*beam;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_SHOWREFENTITY;
	le->startTime = cg.time;
	le->endTime = cg.time + 50;

	beam = &le->refEntity;

	VectorCopy( start, beam->origin );
	// this is the end point
	VectorCopy( end, beam->oldorigin );

	beam->reType = RT_LIGHTNING;
	beam->customShader = cgs.media.lightningShader;
}


/*
==================
CG_KamikazeEffect
==================
*/
void CG_KamikazeEffect( vec3_t org ) {
	localEntity_t	*le;
	refEntity_t		*re;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_KAMIKAZE;
	le->startTime = cg.time;
	le->endTime = cg.time + 3000;//2250;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

	VectorClear(le->angles.trBase);

	re = &le->refEntity;

	re->reType = RT_MODEL;

	if ( intShaderTime )
		re->intShaderTime = cg.time;
	else
		re->shaderTime = cg.time / 1000.0f;

	re->hModel = cgs.media.kamikazeEffectModel;

	VectorCopy( org, re->origin );

}

/*
==================
CG_ObeliskExplode
==================
*/
void CG_ObeliskExplode( vec3_t org, int entityNum ) {
	localEntity_t	*le;
	vec3_t origin;

	// create an explosion
	VectorCopy( org, origin );
	origin[2] += 64;
	le = CG_MakeExplosion( origin, vec3_origin,
						   cgs.media.dishFlashModel,
						   cgs.media.rocketExplosionShader,
						   600, qtrue );
	le->light = 300;
	le->lightColor[0] = 1;
	le->lightColor[1] = 0.75;
	le->lightColor[2] = 0.0;
}

/*
==================
CG_ObeliskPain
==================
*/
void CG_ObeliskPain( vec3_t org ) {
	float r;
	sfxHandle_t sfx;

	// hit sound
	r = rand() & 3;
	if ( r < 2 ) {
		sfx = cgs.media.obeliskHitSound1;
	} else if ( r == 2 ) {
		sfx = cgs.media.obeliskHitSound2;
	} else {
		sfx = cgs.media.obeliskHitSound3;
	}
	trap_S_StartSound ( org, ENTITYNUM_NONE, CHAN_BODY, sfx );
}


/*
==================
CG_InvulnerabilityImpact
==================
*/
void CG_InvulnerabilityImpact( vec3_t org, vec3_t angles ) {
	localEntity_t	*le;
	refEntity_t		*re;
	int				r;
	sfxHandle_t		sfx;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_INVULIMPACT;
	le->startTime = cg.time;
	le->endTime = cg.time + 1000;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

	re = &le->refEntity;

	re->reType = RT_MODEL;

	if ( intShaderTime )
		re->u.intShaderTime = cg.time;
	else
		re->u.shaderTime = cg.time / 1000.0f;

	re->hModel = cgs.media.invulnerabilityImpactModel;

	VectorCopy( org, re->origin );
	AnglesToAxis( angles, re->axis );

	r = rand() & 3;
	if ( r < 2 ) {
		sfx = cgs.media.invulnerabilityImpactSound1;
	} else if ( r == 2 ) {
		sfx = cgs.media.invulnerabilityImpactSound2;
	} else {
		sfx = cgs.media.invulnerabilityImpactSound3;
	}
	trap_S_StartSound (org, ENTITYNUM_NONE, CHAN_BODY, sfx );
}

/*
==================
CG_InvulnerabilityJuiced
==================
*/
void CG_InvulnerabilityJuiced( vec3_t org ) {
	localEntity_t	*le;
	refEntity_t		*re;
	vec3_t			angles;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_INVULJUICED;
	le->startTime = cg.time;
	le->endTime = cg.time + 10000;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

	re = &le->refEntity;

	re->reType = RT_MODEL;

	if ( intShaderTime )
		re->u.intShaderTime = cg.time;
	else
		re->u.shaderTime = cg.time / 1000.0f;

	re->hModel = cgs.media.invulnerabilityJuicedModel;

	VectorCopy( org, re->origin );
	VectorClear(angles);
	AnglesToAxis( angles, re->axis );

	trap_S_StartSound (org, ENTITYNUM_NONE, CHAN_BODY, cgs.media.invulnerabilityJuicedSound );
}
#endif


/*
==================
CG_ScorePlum
==================
*/
void CG_ScorePlum( int client, const vec3_t origin, int score ) {
	localEntity_t	*le;
	refEntity_t		*re;
	vec3_t			angles;
	static vec3_t lastPos;

	// only visualize for the client that scored
	if (client != cg.predictedPlayerState.clientNum || cg_scorePlum.integer == 0) {
		return;
	}

	le = CG_AllocLocalEntity();
	InitializeTimedLocalEntity( *le, LE_SCOREPLUM, cg.time, cg.time + 4000 );

	
	SetLocalEntityColorWhite( *le );
	le->radius = score;
	
	VectorCopy( origin, le->pos.trBase );
	if ( origin[2] >= lastPos[2] - 20 && origin[2] <= lastPos[2] + 20 ) {
		le->pos.trBase[2] -= 20;
	}

	//CG_Printf( "Plum origin %i %i %i -- %i\n", (int)org[0], (int)org[1], (int)org[2], (int)Distance(org, lastPos));
	VectorCopy(origin, lastPos);

	re = &le->refEntity;

	re->reType = RT_SPRITE;
	re->radius = 16;

	VectorClear(angles);
	AnglesToAxis( angles, re->axis );
}


/*
====================
CG_MakeExplosion
====================
*/
localEntity_t *CG_MakeExplosion( const vec3_t origin, const vec3_t dir,
								qhandle_t hModel, qhandle_t shader,
								int msec, qboolean isSprite ) {
	float			ang;
	localEntity_t	*ex;
	int				offset;
	vec3_t			tmpVec, newOrigin;

	if ( msec <= 0 ) {
		CG_Error( "CG_MakeExplosion: msec = %i", msec );
	}

	// skew the time a bit so they aren't all in sync
	offset = rand() & 63;

	ex = CG_AllocLocalEntity();
	if ( isSprite ) {
		ex->leType = LE_SPRITE_EXPLOSION;

		// randomly rotate sprite orientation
		ex->refEntity.rotation = rand() % 360;
		VectorScale( dir, 16, tmpVec );
		VectorAdd( tmpVec, origin, newOrigin );
	} else {
		ex->leType = LE_EXPLOSION;
		VectorCopy( origin, newOrigin );

		// set axis with random rotate
		if ( !dir ) {
			AxisClear( ex->refEntity.axis );
		} else {
			ang = rand() % 360;
			VectorCopy( dir, ex->refEntity.axis[0] );
			RotateAroundDirection( ex->refEntity.axis, ang );
		}
	}

	ex->startTime = cg.time - offset;
	ex->endTime = ex->startTime + msec;

	// bias the time so all shader effects start correctly
	SetRefEntityShaderTime( ex->refEntity, ex->startTime );

	ex->refEntity.hModel = hModel;
	ex->refEntity.customShader = shader;

	// set origin
	VectorCopy( newOrigin, ex->refEntity.origin );
	VectorCopy( newOrigin, ex->refEntity.oldorigin );

	ex->color[0] = ex->color[1] = ex->color[2] = 1.0f;

	return ex;
}


/*
=================
CG_Bleed

This is the spurt of blood when a character gets hit
=================
*/
void CG_Bleed( const vec3_t origin, int entityNum ) {
	localEntity_t	*ex;

	if ( !cg_blood.integer ) {
		return;
	}

	ex = CG_AllocLocalEntity();
	ex->leType = LE_EXPLOSION;

	ex->startTime = cg.time;
	ex->endTime = ex->startTime + 500;
	
	VectorCopy ( origin, ex->refEntity.origin);
	ex->refEntity.reType = RT_SPRITE;
	ex->refEntity.rotation = rand() % 360;
	ex->refEntity.radius = 24;

	ex->refEntity.customShader = cgs.media.bloodExplosionShader;

	// don't show player's own blood in view
	if ( entityNum == cg.snap->ps.clientNum ) {
		ex->refEntity.renderfx |= RF_THIRD_PERSON;
	}
}



/*
==================
CG_LaunchGib
==================
*/
static void CG_LaunchGib( const vec3_t origin, const vec3_t velocity, qhandle_t hModel ) {
	LaunchFragmentModel( origin, velocity, hModel, 5000 + random() * 3000, 0.6f, LEBS_BLOOD, LEMT_BLOOD );
}

/*
===================
CG_GibPlayer

Generated a bunch of gibs launching out from the bodies location
===================
*/
#define	GIB_VELOCITY	250
#define	GIB_JUMP		250
void CG_GibPlayer( const vec3_t playerOrigin ) {
	vec3_t	origin, velocity;
	const auto gib_models = std::to_array<qhandle_t>( {
		cgs.media.gibAbdomen,
		cgs.media.gibArm,
		cgs.media.gibChest,
		cgs.media.gibFist,
		cgs.media.gibFoot,
		cgs.media.gibForearm,
		cgs.media.gibIntestine,
		cgs.media.gibLeg,
		cgs.media.gibLeg,
	} );

	if ( !cg_blood.integer ) {
		return;
	}

	VectorCopy( playerOrigin, origin );
	SetRandomLaunchVelocity( velocity, GIB_VELOCITY, GIB_JUMP );
	if ( rand() & 1 ) {
		CG_LaunchGib( origin, velocity, cgs.media.gibSkull );
	} else {
		CG_LaunchGib( origin, velocity, cgs.media.gibBrain );
	}

	// allow gibs to be turned off for speed
	if ( !cg_gibs.integer ) {
		return;
	}

	for ( const qhandle_t model : gib_models ) {
		VectorCopy( playerOrigin, origin );
		SetRandomLaunchVelocity( velocity, GIB_VELOCITY, GIB_JUMP );
		CG_LaunchGib( origin, velocity, model );
	}
}

/*
==================
CG_LaunchExplode
==================
*/
void CG_LaunchExplode( vec3_t origin, vec3_t velocity, qhandle_t hModel ) {
	LaunchFragmentModel( origin, velocity, hModel, 10000 + random() * 6000, 0.1f, LEBS_BRASS, LEMT_NONE );
}

#define	EXP_VELOCITY	100
#define	EXP_JUMP		150
/*
===================
CG_BigExplode

Generated a bunch of gibs launching out from the bodies location
===================
*/
void CG_BigExplode( vec3_t playerOrigin ) {
	vec3_t	origin, velocity;
	const auto velocity_scales = std::to_array<float>( { 1.0f, 1.0f, 1.5f, 2.0f, 2.5f } );

	if ( !cg_blood.integer ) {
		return;
	}

	for ( const float velocity_scale : velocity_scales ) {
		VectorCopy( playerOrigin, origin );
		SetRandomLaunchVelocity( velocity, EXP_VELOCITY * velocity_scale, EXP_JUMP );
		CG_LaunchExplode( origin, velocity, cgs.media.smoke2 );
	}
}
