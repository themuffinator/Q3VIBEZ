// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_ents.c -- present snapshot entities, happens every single frame

#include "cg_local.h"

#include <span>

static void CG_General( const centity_t *cent );
static void CG_Speaker( centity_t *cent );
static void CG_Item( centity_t *cent );
static void CG_Missile( centity_t *cent );
static void CG_Grapple( centity_t *cent );
static void CG_Mover( const centity_t *cent );
void CG_Beam( const centity_t *cent );
static void CG_Portal( const centity_t *cent );
static void CG_TeamBase( centity_t *cent );

namespace {

constexpr byte kOpaqueEntityAlpha = 255;
constexpr float kItemWeaponScale = 1.5f;
#ifdef MISSIONPACK
constexpr float kKamikazeHoldableScale = 2.0f;
#endif
constexpr float kItemBobScaleBase = 0.005f;
constexpr float kItemBobScaleEntityFactor = 0.00001f;
constexpr float kItemBobHeightOffset = 4.0f;
constexpr float kItemBobHeightAmplitude = 4.0f;

[[nodiscard]] refEntity_t EntityAtLerpOrigin( const centity_t &cent ) noexcept {
	refEntity_t entity{};
	VectorCopy( cent.lerpOrigin, entity.origin );
	VectorCopy( cent.lerpOrigin, entity.oldorigin );
	return entity;
}

[[nodiscard]] refEntity_t ModelEntityAtLerpOrigin( const centity_t &cent ) noexcept {
	refEntity_t entity{};
	entity.reType = RT_MODEL;
	VectorCopy( cent.lerpOrigin, entity.lightingOrigin );
	VectorCopy( cent.lerpOrigin, entity.origin );
	return entity;
}

void SetOpaqueEntityColor( refEntity_t &entity, const byte red = kOpaqueEntityAlpha, const byte green = kOpaqueEntityAlpha,
	const byte blue = kOpaqueEntityAlpha, const byte alpha = kOpaqueEntityAlpha ) noexcept {
	entity.shaderRGBA[0] = red;
	entity.shaderRGBA[1] = green;
	entity.shaderRGBA[2] = blue;
	entity.shaderRGBA[3] = alpha;
}

void ScaleEntityAxes( refEntity_t &entity, const float scale ) noexcept {
	VectorScale( entity.axis[0], scale, entity.axis[0] );
	VectorScale( entity.axis[1], scale, entity.axis[1] );
	VectorScale( entity.axis[2], scale, entity.axis[2] );
	entity.nonNormalizedAxes = qtrue;
}

void ApplyRespawnScale( refEntity_t &entity, const float scale ) noexcept {
	if ( scale == 1.0f ) {
		return;
	}

	ScaleEntityAxes( entity, scale );
}

[[nodiscard]] float ItemRespawnScale( const int elapsedMsec ) noexcept {
	return static_cast<float>( elapsedMsec ) / ITEM_SCALEUP_TIME;
}

[[nodiscard]] float ItemBobScale( const centity_t &cent ) noexcept {
	return kItemBobScaleBase + cent.currentState.number * kItemBobScaleEntityFactor;
}

void ApplyWeaponItemMidpointOffset( vec3_t origin, const weaponInfo_t &weaponInfo, vec3_t axis[3] ) {
	origin[0] -= weaponInfo.weaponMidpoint[0] * axis[0][0]
		+ weaponInfo.weaponMidpoint[1] * axis[1][0]
		+ weaponInfo.weaponMidpoint[2] * axis[2][0];
	origin[1] -= weaponInfo.weaponMidpoint[0] * axis[0][1]
		+ weaponInfo.weaponMidpoint[1] * axis[1][1]
		+ weaponInfo.weaponMidpoint[2] * axis[2][1];
	origin[2] -= weaponInfo.weaponMidpoint[0] * axis[0][2]
		+ weaponInfo.weaponMidpoint[1] * axis[1][2]
		+ weaponInfo.weaponMidpoint[2] * axis[2][2];
	origin[2] += 8.0f;
}

void SetRailgunItemColor( refEntity_t &entity, const clientInfo_t &clientInfo ) noexcept {
	entity.shaderRGBA[0] = static_cast<byte>( clientInfo.color1[0] * 255.0f );
	entity.shaderRGBA[1] = static_cast<byte>( clientInfo.color1[1] * 255.0f );
	entity.shaderRGBA[2] = static_cast<byte>( clientInfo.color1[2] * 255.0f );
	entity.shaderRGBA[3] = kOpaqueEntityAlpha;
}

[[nodiscard]] entityState_t MissileRenderState( const entityState_t &state ) noexcept {
	entityState_t renderState = state;
	renderState.powerups &= ~( ( 1 << PW_INVIS ) | ( 1 << PW_REGEN ) );
	return renderState;
}

[[nodiscard]] int SafeWeaponNum( const int weaponNum ) noexcept {
	return weaponNum < WP_NUM_WEAPONS ? weaponNum : WP_NONE;
}

[[nodiscard]] qhandle_t MoverModelHandle( const entityState_t &state ) noexcept {
	return state.solid == SOLID_BMODEL ? cgs.inlineDrawModel[state.modelindex] : cgs.gameModels[state.modelindex];
}

void AddMoverSecondaryModel( refEntity_t &entity, const entityState_t &state ) {
	if ( !state.modelindex2 ) {
		return;
	}

	entity.skinNum = 0;
	entity.hModel = cgs.gameModels[state.modelindex2 % MAX_MODELS];
	trap_R_AddRefEntityToScene( &entity );
}

[[nodiscard]] qhandle_t TeamBaseModelHandle( const int modelIndex ) noexcept {
	if ( modelIndex == TEAM_RED ) {
		return cgs.media.redFlagBaseModel;
	}
	if ( modelIndex == TEAM_BLUE ) {
		return cgs.media.blueFlagBaseModel;
	}
	return cgs.media.neutralFlagBaseModel;
}

#ifdef MISSIONPACK
void SetEntityColor( refEntity_t &entity, const byte red, const byte green, const byte blue,
	const byte alpha = kOpaqueEntityAlpha ) noexcept {
	entity.shaderRGBA[0] = red;
	entity.shaderRGBA[1] = green;
	entity.shaderRGBA[2] = blue;
	entity.shaderRGBA[3] = alpha;
}

[[nodiscard]] qhandle_t HarvesterModelHandle( const int modelIndex ) noexcept {
	return ( modelIndex == TEAM_RED || modelIndex == TEAM_BLUE ) ? cgs.media.harvesterModel : cgs.media.harvesterNeutralModel;
}

[[nodiscard]] qhandle_t HarvesterSkinHandle( const int modelIndex ) noexcept {
	if ( modelIndex == TEAM_RED ) {
		return cgs.media.harvesterRedSkin;
	}
	if ( modelIndex == TEAM_BLUE ) {
		return cgs.media.harvesterBlueSkin;
	}
	return 0;
}

void ResetObeliskState( centity_t &cent ) noexcept {
	cent.miscTime = 0;
	cent.muzzleFlashTime = 0;
}
#endif

void UpdateAutoRotationAxis( vec3_t angles, vec3_t axis[3], const int mask, const float divisor ) {
	VectorClear( angles );
	angles[YAW] = ( cg.time & mask ) * 360 / divisor;
	AnglesToAxis( angles, axis );
}

[[nodiscard]] bool IsBrushModelEntity( const centity_t &cent ) noexcept {
	return cent.currentState.solid == SOLID_BMODEL;
}

void UpdateBrushModelSoundPosition( const centity_t &cent ) {
	vec3_t soundOrigin;
	VectorAdd( cent.lerpOrigin, cgs.inlineModelMidpoints[cent.currentState.modelindex], soundOrigin );
	trap_S_UpdateEntityPosition( cent.currentState.number, soundOrigin );
}

[[nodiscard]] bool UsesRealLoopingSound( const centity_t &cent ) noexcept {
	return cent.currentState.eType == ET_SPEAKER;
}

void AddEntityLoopSound( const centity_t &cent ) {
	if ( !cent.currentState.loopSound ) {
		return;
	}

	if ( UsesRealLoopingSound( cent ) ) {
		trap_S_AddRealLoopingSound( cent.currentState.number, cent.lerpOrigin, vec3_origin,
			cgs.gameSounds[cent.currentState.loopSound] );
		return;
	}

	trap_S_AddLoopingSound( cent.currentState.number, cent.lerpOrigin, vec3_origin,
		cgs.gameSounds[cent.currentState.loopSound] );
}

void AddEntityConstantLight( const centity_t &cent ) {
	if ( !cent.currentState.constantLight ) {
		return;
	}

	const int packedLight = cent.currentState.constantLight;
	const float red = static_cast<float>( ( packedLight >> 0 ) & 255 ) / 255.0f;
	const float green = static_cast<float>( ( packedLight >> 8 ) & 255 ) / 255.0f;
	const float blue = static_cast<float>( ( packedLight >> 16 ) & 255 ) / 255.0f;
	const float intensity = static_cast<float>( ( packedLight >> 24 ) & 255 ) * 4.0f;
	trap_R_AddLightToScene( cent.lerpOrigin, intensity, red, green, blue );
}

[[nodiscard]] int NextSpeakerTriggerTime( const centity_t &cent ) noexcept {
	return cg.time + cent.currentState.frame * 100 + cent.currentState.clientNum * 100 * crandom();
}

void InterpolateTrajectoryPosition( const trajectory_t &currentTrajectory, const int currentTime,
	const trajectory_t &nextTrajectory, const int nextTime, const float interpolation, vec3_t out ) {
	vec3_t currentPosition;
	vec3_t nextPosition;
	BG_EvaluateTrajectory( &currentTrajectory, currentTime, currentPosition );
	BG_EvaluateTrajectory( &nextTrajectory, nextTime, nextPosition );

	out[0] = currentPosition[0] + interpolation * ( nextPosition[0] - currentPosition[0] );
	out[1] = currentPosition[1] + interpolation * ( nextPosition[1] - currentPosition[1] );
	out[2] = currentPosition[2] + interpolation * ( nextPosition[2] - currentPosition[2] );
}

void InterpolateTrajectoryAngles( const trajectory_t &currentTrajectory, const int currentTime,
	const trajectory_t &nextTrajectory, const int nextTime, const float interpolation, vec3_t out ) {
	vec3_t currentAngles;
	vec3_t nextAngles;
	BG_EvaluateTrajectory( &currentTrajectory, currentTime, currentAngles );
	BG_EvaluateTrajectory( &nextTrajectory, nextTime, nextAngles );

	out[0] = LerpAngle( currentAngles[0], nextAngles[0], interpolation );
	out[1] = LerpAngle( currentAngles[1], nextAngles[1], interpolation );
	out[2] = LerpAngle( currentAngles[2], nextAngles[2], interpolation );
}

[[nodiscard]] bool ShouldForceClientInterpolation( const centity_t &cent ) noexcept {
	return !cg_smoothClients.integer && cent.currentState.number < MAX_CLIENTS;
}

void ForceClientInterpolation( centity_t &cent ) noexcept {
	cent.currentState.pos.trType = TR_INTERPOLATE;
	cent.nextState.pos.trType = TR_INTERPOLATE;
}

[[nodiscard]] bool UsesInterpolatedPosition( const centity_t &cent ) noexcept {
	if ( !cent.interpolate ) {
		return false;
	}

	if ( cent.currentState.pos.trType == TR_INTERPOLATE ) {
		return true;
	}

	return cent.currentState.pos.trType == TR_LINEAR_STOP && cent.currentState.number < MAX_CLIENTS;
}

[[nodiscard]] bool ShouldAdjustForGroundMover( const centity_t &cent ) noexcept {
	return &cent != &cg.predictedPlayerEntity;
}

[[nodiscard]] float PacketFrameInterpolation() noexcept {
	if ( !cg.nextSnap ) {
		return 0.0f;
	}

	const int delta = cg.nextSnap->serverTime - cg.snap->serverTime;
	if ( delta == 0 ) {
		return 0.0f;
	}

	return static_cast<float>( cg.time - cg.snap->serverTime ) / delta;
}

[[nodiscard]] std::span<const entityState_t> CurrentSnapshotEntities() noexcept {
	return { cg.snap->entities, static_cast<size_t>( cg.snap->numEntities ) };
}

void DispatchCEntity( centity_t &cent ) {
	switch ( cent.currentState.eType ) {
	default:
		CG_Error( "Bad entity type: %i", cent.currentState.eType );
		break;
	case ET_INVISIBLE:
	case ET_PUSH_TRIGGER:
	case ET_TELEPORT_TRIGGER:
		break;
	case ET_GENERAL:
		CG_General( &cent );
		break;
	case ET_PLAYER:
		CG_Player( &cent );
		break;
	case ET_ITEM:
		CG_Item( &cent );
		break;
	case ET_MISSILE:
		CG_Missile( &cent );
		break;
	case ET_MOVER:
		CG_Mover( &cent );
		break;
	case ET_BEAM:
		CG_Beam( &cent );
		break;
	case ET_PORTAL:
		CG_Portal( &cent );
		break;
	case ET_SPEAKER:
		CG_Speaker( &cent );
		break;
	case ET_GRAPPLE:
		CG_Grapple( &cent );
		break;
	case ET_TEAM:
		CG_TeamBase( &cent );
		break;
	}
}

} // namespace


/*
======================
CG_PositionEntityOnTag

Modifies the entities position and axis by the given
tag location
======================
*/
void CG_PositionEntityOnTag( refEntity_t *entity, const refEntity_t *parent, 
							qhandle_t parentModel, const char *tagName ) {
	int				i;
	orientation_t	lerped;
	
	// lerp the tag
	trap_R_LerpTag( &lerped, parentModel, parent->oldframe, parent->frame,
		1.0 - parent->backlerp, tagName );

	// FIXME: allow origin offsets along tag?
	VectorCopy( parent->origin, entity->origin );
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorMA( entity->origin, lerped.origin[i], parent->axis[i], entity->origin );
	}

	// had to cast away the const to avoid compiler problems...
	MatrixMultiply( lerped.axis, ((refEntity_t *)parent)->axis, entity->axis );
	entity->backlerp = parent->backlerp;
}


/*
======================
CG_PositionRotatedEntityOnTag

Modifies the entities position and axis by the given
tag location
======================
*/
void CG_PositionRotatedEntityOnTag( refEntity_t *entity, const refEntity_t *parent, 
							qhandle_t parentModel, const char *tagName ) {
	int				i;
	orientation_t	lerped;
	vec3_t			tempAxis[3];

//AxisClear( entity->axis );
	// lerp the tag
	trap_R_LerpTag( &lerped, parentModel, parent->oldframe, parent->frame,
		1.0 - parent->backlerp, tagName );

	// FIXME: allow origin offsets along tag?
	VectorCopy( parent->origin, entity->origin );
	for ( i = 0 ; i < 3 ; i++ ) {
		VectorMA( entity->origin, lerped.origin[i], parent->axis[i], entity->origin );
	}

	// had to cast away the const to avoid compiler problems...
	MatrixMultiply( entity->axis, lerped.axis, tempAxis );
	MatrixMultiply( tempAxis, ((refEntity_t *)parent)->axis, entity->axis );
}



/*
==========================================================================

FUNCTIONS CALLED EACH FRAME

==========================================================================
*/

/*
======================
CG_SetEntitySoundPosition

Also called by event processing code
======================
*/
void CG_SetEntitySoundPosition( const centity_t *cent ) {
	if ( IsBrushModelEntity( *cent ) ) {
		UpdateBrushModelSoundPosition( *cent );
		return;
	}

	trap_S_UpdateEntityPosition( cent->currentState.number, cent->lerpOrigin );
}

/*
==================
CG_EntityEffects

Add continuous entity effects, like local entity emission and lighting
==================
*/
static void CG_EntityEffects( const centity_t *cent ) {
	// update sound origins
	CG_SetEntitySoundPosition( cent );

	AddEntityLoopSound( *cent );
	AddEntityConstantLight( *cent );
}


/*
==================
CG_General
==================
*/
static void CG_General( const centity_t *cent ) {
	refEntity_t ent = EntityAtLerpOrigin( *cent );
	const entityState_t &s1 = cent->currentState;

	// if set to invisible, skip
	if ( !s1.modelindex ) {
		return;
	}

	// set frame
	ent.frame = s1.frame;
	ent.oldframe = ent.frame;
	ent.backlerp = 0;
	ent.hModel = cgs.gameModels[s1.modelindex];

	// player model
	if ( s1.number == cg.snap->ps.clientNum ) {
		ent.renderfx |= RF_THIRD_PERSON;	// only draw from mirrors
	}

	// convert angles to axis
	AnglesToAxis( cent->lerpAngles, ent.axis );

	// add to refresh list
	trap_R_AddRefEntityToScene( &ent );
}

/*
==================
CG_Speaker

Speaker entities can automatically play sounds
==================
*/
static void CG_Speaker( centity_t *cent ) {
	if ( ! cent->currentState.clientNum ) {	// FIXME: use something other than clientNum...
		return;		// not auto triggering
	}

	if ( cg.time < cent->miscTime ) {
		return;
	}

	trap_S_StartSound( nullptr, cent->currentState.number, CHAN_ITEM, cgs.gameSounds[cent->currentState.eventParm] );

	//	ent->s.frame = ent->wait * 10;
	//	ent->s.clientNum = ent->random * 10;
	cent->miscTime = NextSpeakerTriggerTime( *cent );
}

/*
==================
CG_Item
==================
*/
static void CG_Item( centity_t *cent ) {
	refEntity_t ent{};
	entityState_t &es = cent->currentState;
	const weaponInfo_t *weaponInfo = nullptr;
	vec3_t displayOrigin;

	if ( es.modelindex >= bg_numItems ) {
		CG_Error( "Bad item index %i on entity", es.modelindex );
	}

	// if set to invisible, skip
	if ( !es.modelindex || ( es.eFlags & EF_NODRAW ) || cent->delaySpawn > cg.time ) {
		return;
	}

	itemInfo_t &itemInfo = cg_items[es.modelindex];
	if ( !itemInfo.registered ) {
		CG_RegisterItemVisuals( es.modelindex );
		if ( !itemInfo.registered ) {
			return;
		}
	}

	const gitem_t &item = bg_itemlist[es.modelindex];
	if ( cg_simpleItems.integer && item.giType != IT_TEAM ) {
		ent = refEntity_t{};
		ent.reType = RT_SPRITE;
		VectorCopy( cent->lerpOrigin, ent.origin );
		ent.radius = 14;
		ent.customShader = itemInfo.icon_df;
		SetOpaqueEntityColor( ent );
		trap_R_AddRefEntityToScene( &ent );
		return;
	}

	// items bob up and down continuously
	VectorCopy( cent->lerpOrigin, displayOrigin );
	const float bobScale = ItemBobScale( *cent );
	const int bobModulus = static_cast<int>( 2 * M_PI * 20228 / bobScale );
	displayOrigin[2] += kItemBobHeightOffset + cos( ( ( cg.time + 1000 ) % bobModulus ) * bobScale ) * kItemBobHeightAmplitude;

	// autorotate at one of two speeds
	if ( item.giType == IT_HEALTH ) {
		VectorCopy( cg.autoAnglesFast, cent->lerpAngles );
		AxisCopy( cg.autoAxisFast, ent.axis );
	} else {
		VectorCopy( cg.autoAngles, cent->lerpAngles );
		AxisCopy( cg.autoAxis, ent.axis );
	}

	// the weapons have their origin where they attatch to player
	// models, so we need to offset them or they will rotate
	// eccentricly
	if ( item.giType == IT_WEAPON ) {
		weaponInfo = &cg_weapons[item.giTag];
		ApplyWeaponItemMidpointOffset( displayOrigin, *weaponInfo, ent.axis );
	}

	ent.hModel = itemInfo.models[0];

	VectorCopy( displayOrigin, ent.origin );
	VectorCopy( displayOrigin, ent.oldorigin );

	ent.nonNormalizedAxes = qfalse;

	// if just respawned, slowly scale up
	const int elapsedMsec = cg.time - cent->miscTime;
	const float respawnScale = ( elapsedMsec >= 0 && elapsedMsec < ITEM_SCALEUP_TIME ) ? ItemRespawnScale( elapsedMsec ) : 1.0f;
	ApplyRespawnScale( ent, respawnScale );

	// items without glow textures need to keep a minimum light value
	// so they are always visible
	if ( item.giType == IT_WEAPON || item.giType == IT_ARMOR ) {
		ent.renderfx |= RF_MINLIGHT;
	}

	// increase the size of the weapons when they are presented as items
	if ( item.giType == IT_WEAPON ) {
		ScaleEntityAxes( ent, kItemWeaponScale );
#ifdef MISSIONPACK
		trap_S_AddLoopingSound( cent->currentState.number, displayOrigin, vec3_origin, cgs.media.weaponHoverSound );
#endif
		// pickup color from spectaror/own client
		if ( item.giTag == WP_RAILGUN ) {
			const clientInfo_t *ci = cgs.clientinfo + cg.snap->ps.clientNum;
			SetRailgunItemColor( ent, *ci );
		}
	}

#ifdef MISSIONPACK
	if ( item.giType == IT_HOLDABLE && item.giTag == HI_KAMIKAZE ) {
		ScaleEntityAxes( ent, kKamikazeHoldableScale );
	}
#endif

	// add to refresh list
	trap_R_AddRefEntityToScene( &ent );

#ifdef MISSIONPACK
	if ( item.giType == IT_WEAPON && weaponInfo->barrelModel ) {
		refEntity_t barrel{};

		barrel.hModel = weaponInfo->barrelModel;

		VectorCopy( ent.lightingOrigin, barrel.lightingOrigin );
		barrel.shadowPlane = ent.shadowPlane;
		barrel.renderfx = ent.renderfx;

		CG_PositionRotatedEntityOnTag( &barrel, &ent, weaponInfo->weaponModel, "tag_barrel" );

		AxisCopy( ent.axis, barrel.axis );
		barrel.nonNormalizedAxes = ent.nonNormalizedAxes;

		trap_R_AddRefEntityToScene( &barrel );
	}
#endif

	// accompanying rings / spheres for powerups
	if ( !cg_simpleItems.integer ) 
	{
		vec3_t spinAngles;

		VectorClear( spinAngles );

		if ( item.giType == IT_HEALTH || item.giType == IT_POWERUP )
		{
			if ( ( ent.hModel = itemInfo.models[1] ) != 0 )
			{
				if ( item.giType == IT_POWERUP )
				{
					ent.origin[2] += 12;
					spinAngles[1] = ( cg.time & 1023 ) * 360 / -1024.0f;
				}
				AnglesToAxis( spinAngles, ent.axis );
				
				// scale up if respawning
				ApplyRespawnScale( ent, respawnScale );
				trap_R_AddRefEntityToScene( &ent );
			}
		}
	}
}

//============================================================================

/*
===============
CG_Missile
===============
*/
static void CG_Missile( centity_t *cent ) {
	refEntity_t ent = EntityAtLerpOrigin( *cent );
	entityState_t &s1 = cent->currentState;
	const weaponInfo_t	*weapon;
	const clientInfo_t	*ci;
//	int	col;

	s1.weapon = SafeWeaponNum( s1.weapon );
	weapon = &cg_weapons[s1.weapon];

	// calculate the axis
	VectorCopy( s1.angles, cent->lerpAngles );

	// add trails
	if ( weapon->missileTrailFunc ) 
	{
		weapon->missileTrailFunc( cent, weapon );
	}
/*
	if ( cent->currentState.modelindex == TEAM_RED ) {
		col = 1;
	}
	else if ( cent->currentState.modelindex == TEAM_BLUE ) {
		col = 2;
	}
	else {
		col = 0;
	}

	// add dynamic light
	if ( weapon->missileDlight ) {
		trap_R_AddLightToScene(cent->lerpOrigin, weapon->missileDlight, 
			weapon->missileDlightColor[col][0], weapon->missileDlightColor[col][1], weapon->missileDlightColor[col][2] );
	}
*/
	// add dynamic light
	if ( weapon->missileDlight ) {
		trap_R_AddLightToScene( cent->lerpOrigin, weapon->missileDlight, 
			weapon->missileDlightColor[0], weapon->missileDlightColor[1], weapon->missileDlightColor[2] );
	}

	// add missile sound
	if ( weapon->missileSound ) {
		vec3_t	velocity;

		BG_EvaluateTrajectoryDelta( &s1.pos, cg.time, velocity );

		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, velocity, weapon->missileSound );
	}

	if ( s1.weapon == WP_PLASMAGUN ) {
		ent.reType = RT_SPRITE;
		ent.radius = 16;
		ent.rotation = 0;
		ent.customShader = cgs.media.plasmaBallShader;
		trap_R_AddRefEntityToScene( &ent );
		return;
	}

	// flicker between two skins
	ent.skinNum = cg.clientFrame & 1;
	ent.hModel = weapon->missileModel;
	ent.renderfx = weapon->missileRenderfx | RF_NOSHADOW;

#ifdef MISSIONPACK
	if ( s1.weapon == WP_PROX_LAUNCHER ) {
		if ( s1.generic1 == TEAM_BLUE ) {
			ent.hModel = cgs.media.blueProxMine;
		}
	}
#endif

	// convert direction of travel into axis
	if ( VectorNormalize2( s1.pos.trDelta, ent.axis[0] ) == 0 ) {
		ent.axis[0][2] = 1;
	}

	// spin as it moves
	if ( s1.pos.trType != TR_STATIONARY ) {
		RotateAroundDirection( ent.axis, ( cg.time % TMOD_004 ) / 4.0 );
	} else {
#ifdef MISSIONPACK
		if ( s1.weapon == WP_PROX_LAUNCHER ) {
			AnglesToAxis( cent->lerpAngles, ent.axis );
		}
		else
#endif
		{
			RotateAroundDirection( ent.axis, s1.time );
		}
	}

	// add to refresh list, possibly with quad glow

	entityState_t renderState = MissileRenderState( s1 );
	ci = &cgs.clientinfo[ s1.clientNum & MAX_CLIENTS ];
	if ( ci->infoValid ) {
		CG_AddRefEntityWithPowerups( &ent, &renderState, ci->team );
	} else {
		CG_AddRefEntityWithPowerups( &ent, &renderState, TEAM_FREE );
	}

}

/*
===============
CG_Grapple

This is called when the grapple is sitting up against the wall
===============
*/
static void CG_Grapple( centity_t *cent ) {
	refEntity_t ent = EntityAtLerpOrigin( *cent );
	entityState_t &s1 = cent->currentState;
	const weaponInfo_t		*weapon;

	s1.weapon = SafeWeaponNum( s1.weapon );
	weapon = &cg_weapons[s1.weapon];

	// calculate the axis
	VectorCopy( s1.angles, cent->lerpAngles );

#if 0 // FIXME add grapple pull sound here..?
	// add missile sound
	if ( weapon->missileSound ) {
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, weapon->missileSound );
	}
#endif

	// Will draw cable if needed
	CG_GrappleTrail ( cent, weapon );

	// flicker between two skins
	ent.skinNum = cg.clientFrame & 1;
	ent.hModel = weapon->missileModel;
	ent.renderfx = weapon->missileRenderfx | RF_NOSHADOW;

	// convert direction of travel into axis
	if ( VectorNormalize2( s1.pos.trDelta, ent.axis[0] ) == 0 ) {
		ent.axis[0][2] = 1;
	}

	trap_R_AddRefEntityToScene( &ent );
}

/*
===============
CG_Mover
===============
*/
static void CG_Mover( const centity_t *cent ) {
	refEntity_t ent = EntityAtLerpOrigin( *cent );
	const entityState_t &s1 = cent->currentState;
	AnglesToAxis( cent->lerpAngles, ent.axis );

	ent.renderfx = RF_NOSHADOW;

	// flicker between two skins (FIXME?)
	ent.skinNum = ( cg.time >> 6 ) & 1;

	// get the model, either as a bmodel or a modelindex
	ent.hModel = MoverModelHandle( s1 );

	// add to refresh list
	trap_R_AddRefEntityToScene( &ent );
	AddMoverSecondaryModel( ent, s1 );
}

/*
===============
CG_Beam

Also called as an event
===============
*/
void CG_Beam( const centity_t *cent ) {
	refEntity_t ent{};
	const entityState_t &s1 = cent->currentState;
	VectorCopy( s1.pos.trBase, ent.origin );
	VectorCopy( s1.origin2, ent.oldorigin );
	AxisClear( ent.axis );
	ent.reType = RT_BEAM;

	ent.renderfx = RF_NOSHADOW;
	ent.customShader = cgs.media.whiteShader;

	// add to refresh list
	trap_R_AddRefEntityToScene(&ent);
}


/*
===============
CG_Portal
===============
*/
static void CG_Portal( const centity_t *cent ) {
	refEntity_t ent = EntityAtLerpOrigin( *cent );
	const entityState_t &s1 = cent->currentState;
	VectorCopy( s1.origin2, ent.oldorigin );
	ByteToDir( s1.eventParm, ent.axis[0] );
	PerpendicularVector( ent.axis[1], ent.axis[0] );

	// negating this tends to get the directions like they want
	// we really should have a camera roll value
	VectorSubtract( vec3_origin, ent.axis[1], ent.axis[1] );

	CrossProduct( ent.axis[0], ent.axis[1], ent.axis[2] );
	ent.reType = RT_PORTALSURFACE;
	ent.oldframe = s1.powerups;
	ent.frame = s1.frame;		// rotation speed
	ent.skinNum = s1.clientNum/256.0 * 360;	// roll offset

	// add to refresh list
	trap_R_AddRefEntityToScene(&ent);
}


/*
=========================
CG_AdjustPositionForMover

Also called by client movement prediction code
=========================
*/
void CG_AdjustPositionForMover( const vec3_t in, int moverNum, int fromTime, int toTime, vec3_t out, const vec3_t angles_in, vec3_t angles_out ) {
	centity_t	*cent;
	vec3_t	oldOrigin, origin, deltaOrigin;
	vec3_t	oldAngles, angles, deltaAngles;

	if ( moverNum <= 0 || moverNum >= ENTITYNUM_MAX_NORMAL ) {
		VectorCopy( in, out );
		VectorCopy( angles_in, angles_out );
		return;
	}

	cent = &cg_entities[ moverNum ];
	if ( cent->currentState.eType != ET_MOVER ) {
		VectorCopy( in, out );
		VectorCopy( angles_in, angles_out );
		return;
	}

	BG_EvaluateTrajectory( &cent->currentState.pos, fromTime, oldOrigin );
	BG_EvaluateTrajectory( &cent->currentState.apos, fromTime, oldAngles );

	BG_EvaluateTrajectory( &cent->currentState.pos, toTime, origin );
	BG_EvaluateTrajectory( &cent->currentState.apos, toTime, angles );

	VectorSubtract( origin, oldOrigin, deltaOrigin );
	VectorSubtract( angles, oldAngles, deltaAngles );

	VectorAdd( in, deltaOrigin, out );
	VectorAdd( angles_in, deltaAngles, angles_out );
	// FIXME: origin change when on a rotating object
}


/*
=============================
CG_InterpolateEntityPosition
=============================
*/
static void CG_InterpolateEntityPosition( centity_t *cent ) {
	// it would be an internal error to find an entity that interpolates without
	// a snapshot ahead of the current one
	if ( cg.nextSnap == nullptr ) {
		CG_Error( "CG_InterpoateEntityPosition: cg.nextSnap == NULL" );
	}

	const float interpolation = cg.frameInterpolation;

	// this will linearize a sine or parabolic curve, but it is important
	// to not extrapolate player positions if more recent data is available
	InterpolateTrajectoryPosition( cent->currentState.pos, cg.snap->serverTime,
		cent->nextState.pos, cg.nextSnap->serverTime, interpolation, cent->lerpOrigin );
	InterpolateTrajectoryAngles( cent->currentState.apos, cg.snap->serverTime,
		cent->nextState.apos, cg.nextSnap->serverTime, interpolation, cent->lerpAngles );
}

/*
===============
CG_CalcEntityLerpPositions

===============
*/
static void CG_CalcEntityLerpPositions( centity_t *cent ) {
	// if this player does not want to see extrapolated players
	if ( ShouldForceClientInterpolation( *cent ) ) {
		ForceClientInterpolation( *cent );
	}

	if ( UsesInterpolatedPosition( *cent ) ) {
		CG_InterpolateEntityPosition( cent );
		return;
	}

	// just use the current frame and evaluate as best we can
	BG_EvaluateTrajectory( &cent->currentState.pos, cg.time, cent->lerpOrigin );
	BG_EvaluateTrajectory( &cent->currentState.apos, cg.time, cent->lerpAngles );

	// adjust for riding a mover if it wasn't rolled into the predicted
	// player state
	if ( ShouldAdjustForGroundMover( *cent ) ) {
		CG_AdjustPositionForMover( cent->lerpOrigin, cent->currentState.groundEntityNum, 
		cg.snap->serverTime, cg.time, cent->lerpOrigin, cent->lerpAngles, cent->lerpAngles );
	}
}

/*
===============
CG_TeamBase
===============
*/
static void CG_TeamBase( centity_t *cent ) {
	refEntity_t model = ModelEntityAtLerpOrigin( *cent );
#ifdef MISSIONPACK
	vec3_t angles;
	int t, h;
	float c;

	if ( cgs.gametype == GT_CTF || cgs.gametype == GT_1FCTF ) {
#else
	if ( cgs.gametype == GT_CTF) {
#endif
		// show the flag base
		AnglesToAxis( cent->currentState.angles, model.axis );
		model.hModel = TeamBaseModelHandle( cent->currentState.modelindex );
		trap_R_AddRefEntityToScene( &model );
	}
#ifdef MISSIONPACK
	else if ( cgs.gametype == GT_OBELISK ) {
		// show the obelisk
		model = ModelEntityAtLerpOrigin( *cent );
		AnglesToAxis( cent->currentState.angles, model.axis );

		model.hModel = cgs.media.overloadBaseModel;
		trap_R_AddRefEntityToScene( &model );
		// if hit
		if ( cent->currentState.frame == 1) {
			// show hit model
			// modelindex2 is the health value of the obelisk
			const byte hitColor = static_cast<byte>( cent->currentState.modelindex2 );
			SetEntityColor( model, 0xff, hitColor, hitColor );
			//
			model.hModel = cgs.media.overloadEnergyModel;
			trap_R_AddRefEntityToScene( &model );
		}
		// if respawning
		if ( cent->currentState.frame == 2) {
			if ( !cent->miscTime ) {
				cent->miscTime = cg.time;
			}
			t = cg.time - cent->miscTime;
			h = (cg_obeliskRespawnDelay.integer - 5) * 1000;
			//
			if (t > h) {
				c = (float) (t - h) / h;
				if (c > 1)
					c = 1;
			}
			else {
				c = 0;
			}
			// show the lights
			AnglesToAxis( cent->currentState.angles, model.axis );
			//
			SetEntityColor( model, static_cast<byte>( c * 0xff ), static_cast<byte>( c * 0xff ), static_cast<byte>( c * 0xff ),
				static_cast<byte>( c * 0xff ) );

			model.hModel = cgs.media.overloadLightsModel;
			trap_R_AddRefEntityToScene( &model );
			// show the target
			if (t > h) {
				if ( !cent->muzzleFlashTime ) {
					trap_S_StartSound( cent->lerpOrigin, ENTITYNUM_NONE, CHAN_BODY, cgs.media.obeliskRespawnSound );
					cent->muzzleFlashTime = 1;
				}
				VectorCopy( cent->currentState.angles, angles );
				angles[YAW] += (float) 16 * acos(1-c) * 180 / M_PI;
				AnglesToAxis( angles, model.axis );

				VectorScale( model.axis[0], c, model.axis[0] );
				VectorScale( model.axis[1], c, model.axis[1] );
				VectorScale( model.axis[2], c, model.axis[2] );

				SetEntityColor( model, 0xff, 0xff, 0xff );
				//
				model.origin[2] += 56;
				model.hModel = cgs.media.overloadTargetModel;
				trap_R_AddRefEntityToScene( &model );
			}
			else {
				//FIXME: show animated smoke
			}
		}
		else {
			ResetObeliskState( *cent );
			// modelindex2 is the health value of the obelisk
			const byte hitColor = static_cast<byte>( cent->currentState.modelindex2 );
			SetEntityColor( model, 0xff, hitColor, hitColor );
			// show the lights
			model.hModel = cgs.media.overloadLightsModel;
			trap_R_AddRefEntityToScene( &model );
			// show the target
			model.origin[2] += 56;
			model.hModel = cgs.media.overloadTargetModel;
			trap_R_AddRefEntityToScene( &model );
		}
	}
	else if ( cgs.gametype == GT_HARVESTER ) {
		// show harvester model
		model = ModelEntityAtLerpOrigin( *cent );
		AnglesToAxis( cent->currentState.angles, model.axis );
		model.hModel = HarvesterModelHandle( cent->currentState.modelindex );
		model.customSkin = HarvesterSkinHandle( cent->currentState.modelindex );
		trap_R_AddRefEntityToScene( &model );
	}
#endif
}

/*
===============
CG_AddCEntity

===============
*/
static void CG_AddCEntity( centity_t *cent ) {
	// event-only entities will have been dealt with already
	if ( cent->currentState.eType >= ET_EVENTS ) {
		return;
	}

	// calculate the current origin
	CG_CalcEntityLerpPositions( cent );

	// add automatic effects
	CG_EntityEffects( cent );
	DispatchCEntity( *cent );
}

/*
===============
CG_AddPacketEntities

===============
*/
void CG_AddPacketEntities( void ) {
	// set cg.frameInterpolation
	cg.frameInterpolation = PacketFrameInterpolation();

	// the auto-rotating items will all have the same axis
	UpdateAutoRotationAxis( cg.autoAngles, cg.autoAxis, 2047, 2048.0f );
	UpdateAutoRotationAxis( cg.autoAnglesFast, cg.autoAxisFast, 1023, 1024.0f );

	// generate and add the entity from the playerstate
	BG_PlayerStateToEntityState( &cg.predictedPlayerState, &cg.predictedPlayerEntity.currentState, qfalse );
	CG_AddCEntity( &cg.predictedPlayerEntity );

	// lerp the non-predicted value for lightning gun origins
	CG_CalcEntityLerpPositions( &cg_entities[ cg.snap->ps.clientNum ] );

	// add each entity sent over by the server
	for ( const entityState_t &snapshotEntity : CurrentSnapshotEntities() ) {
		CG_AddCEntity( &cg_entities[snapshotEntity.number] );
	}
}
