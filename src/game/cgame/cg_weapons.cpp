// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_weapons.c -- events and effects dealing with weapons
#include "cg_local.h"

#include <array>
#include <span>

namespace {

constexpr byte kWeaponEntityAlpha = 255;
constexpr byte kMinimumWeaponGreenChannel = 64;
constexpr float kRailgunRefireDurationMs = 1500.0f;
constexpr int kAmmoFontSize = 12;
constexpr float kShotgunTraceDistance = 8192.0f * 16.0f;
constexpr int kBubbleTrailSpacing = 32;
constexpr float kBrassWaterScale = 0.10f;
constexpr int kRandomAngleMask = 31;
constexpr std::array<float, 2> kShotgunLateralVelocityBiases{ 40.0f, -40.0f };
constexpr int kProjectileTrailStepMs = 50;
constexpr int kProjectileBubbleTrailSpacing = 8;
constexpr int kRailAxisCount = 36;
constexpr int kRailRotationStep = 1;
constexpr int kRailSpacing = 5;
constexpr float kRailRingOffset = 20.0f;
constexpr float kRailRingRadius = 4.0f;
constexpr float kRailRingSpriteRadius = 1.1f;
constexpr float kRailRingVelocityScale = 6.0f;
constexpr int kRailInitialAxisIndex = 18;
constexpr int kRailRotationDegrees = 10;
constexpr int kPlasmaTrailLifetimeMs = 600;
constexpr float kPlasmaTrailSpriteRadius = 0.25f;
constexpr float kPlasmaTrailShaderColorScale = 63.0f;
constexpr byte kPlasmaTrailShaderAlpha = 63;
constexpr float kPlasmaTrailLocalColorScale = 0.2f;
constexpr float kPlasmaTrailLocalAlpha = 0.25f;
constexpr float kGrappleTrailMinDistance = 64.0f;
constexpr std::array<float, 3> kWhiteColor{ 1.0f, 1.0f, 1.0f };

struct WeaponSelectLayout {
	int x;
	int y;
	int dx;
	int dy;
};

struct MissileImpactSpec {
	qhandle_t model = 0;
	qhandle_t mark = 0;
	qhandle_t shader = 0;
	sfxHandle_t sound = 0;
	float radius = 32.0f;
	float light = 0.0f;
	std::array<float, 3> lightColor{ 1.0f, 1.0f, 0.0f };
	bool isSprite = false;
	int duration = 600;
};

[[nodiscard]] bool WeaponHasPowerup( const int powerups, const powerup_t powerup ) noexcept {
	return ( powerups & ( 1 << powerup ) ) != 0;
}

[[nodiscard]] bool IsCrouchedLegsAnimation( const int legsAnimation ) noexcept {
	return legsAnimation == LEGS_WALKCR || legsAnimation == LEGS_IDLECR;
}

[[nodiscard]] float WrappedAngleDelta( const float sourceAngle, const float referenceAngle ) noexcept {
	float delta = sourceAngle - referenceAngle;
	if ( delta > 180.0f ) {
		delta -= 360.0f;
	}
	if ( delta < -180.0f ) {
		delta += 360.0f;
	}
	return delta;
}

[[nodiscard]] refEntity_t WeaponAttachmentEntity( const refEntity_t &parent ) noexcept {
	refEntity_t entity{};
	VectorCopy( parent.lightingOrigin, entity.lightingOrigin );
	entity.shadowPlane = parent.shadowPlane;
	entity.renderfx = parent.renderfx;
	return entity;
}

void SetWeaponEntityColor( refEntity_t &entity, const vec3_t color, const float scale = 255.0f ) noexcept {
	entity.shaderRGBA[0] = static_cast<byte>( color[0] * scale );
	entity.shaderRGBA[1] = static_cast<byte>( color[1] * scale );
	entity.shaderRGBA[2] = static_cast<byte>( color[2] * scale );
	entity.shaderRGBA[3] = kWeaponEntityAlpha;
}

void ApplyPredictedWeaponColor( refEntity_t &weaponEntity, const clientInfo_t &clientInfo, const playerState_t &playerState ) noexcept {
	if ( playerState.weapon == WP_RAILGUN && playerState.weaponstate == WEAPON_FIRING ) {
		const float fade = 1.0f - static_cast<float>( playerState.weaponTime ) / kRailgunRefireDurationMs;
		SetWeaponEntityColor( weaponEntity, clientInfo.color1, 255.0f * fade );
		return;
	}

	SetWeaponEntityColor( weaponEntity, clientInfo.color1 );
	if ( weaponEntity.shaderRGBA[1] < kMinimumWeaponGreenChannel ) {
		weaponEntity.shaderRGBA[1] = kMinimumWeaponGreenChannel;
	}
}

void AddWeaponPowerupShaders( refEntity_t &weaponEntity, const int powerups ) {
	if ( WeaponHasPowerup( powerups, PW_BATTLESUIT ) ) {
		weaponEntity.customShader = cgs.media.battleWeaponShader;
		trap_R_AddRefEntityToScene( &weaponEntity );
	}
	if ( WeaponHasPowerup( powerups, PW_QUAD ) ) {
		weaponEntity.customShader = cgs.media.quadWeaponShader;
		trap_R_AddRefEntityToScene( &weaponEntity );
	}
}

void AddWeaponReadyLoopSound( centity_t &cent, const weaponInfo_t &weapon ) {
	cent.pe.lightningFiring = qfalse;
	if ( ( cent.currentState.eFlags & EF_FIRING ) && weapon.firingSound ) {
		trap_S_AddLoopingSound( cent.currentState.number, cent.lerpOrigin, vec3_origin, weapon.firingSound );
		cent.pe.lightningFiring = qtrue;
		return;
	}

	if ( weapon.readySound ) {
		trap_S_AddLoopingSound( cent.currentState.number, cent.lerpOrigin, vec3_origin, weapon.readySound );
	}
}

[[nodiscard]] centity_t *ResolveNonPredictedMuzzleCentity( centity_t *cent ) noexcept {
	centity_t *nonPredictedCent = &cg_entities[cent->currentState.clientNum];
	if ( ( nonPredictedCent - cg_entities ) != cent->currentState.clientNum ) {
		return cent;
	}
	return nonPredictedCent;
}

[[nodiscard]] bool UsesContinuousWeaponFlash( const int weaponNum ) noexcept {
	return weaponNum == WP_LIGHTNING || weaponNum == WP_GAUNTLET || weaponNum == WP_GRAPPLING_HOOK;
}

[[nodiscard]] bool ShouldRenderWeaponFlash( const centity_t &cent, const centity_t &nonPredictedCent, const int weaponNum ) noexcept {
	if ( UsesContinuousWeaponFlash( weaponNum ) && ( nonPredictedCent.currentState.eFlags & EF_FIRING ) ) {
		return true;
	}

	return cg.time - cent.muzzleFlashTime <= MUZZLE_FLASH_TIME || cent.pe.railgunFlash || weaponNum == WP_GRAPPLING_HOOK;
}

void ResetWeaponInfo( weaponInfo_t &weaponInfo ) noexcept {
	weaponInfo = weaponInfo_t{};
	weaponInfo.registered = qtrue;
}

void ResetItemInfo( itemInfo_t &itemInfo ) noexcept {
	itemInfo = itemInfo_t{};
	itemInfo.registered = qtrue;
}

[[nodiscard]] gitem_t *FindWeaponItemByTag( const int weaponNum ) noexcept {
	for ( gitem_t *item = bg_itemlist + 1; item->classname; ++item ) {
		if ( item->giType == IT_WEAPON && item->giTag == weaponNum ) {
			return item;
		}
	}
	return nullptr;
}

[[nodiscard]] gitem_t *FindAmmoItemByTag( const int weaponNum ) noexcept {
	for ( gitem_t *item = bg_itemlist + 1; item->classname; ++item ) {
		if ( item->giType == IT_AMMO && item->giTag == weaponNum ) {
			return item;
		}
	}
	return nullptr;
}

void SetWeaponMidpoint( const qhandle_t modelHandle, vec3_t midpoint ) {
	vec3_t mins;
	vec3_t maxs;
	trap_R_ModelBounds( modelHandle, mins, maxs );
	for ( int axisIndex = 0; axisIndex < 3; ++axisIndex ) {
		midpoint[axisIndex] = mins[axisIndex] + 0.5f * ( maxs[axisIndex] - mins[axisIndex] );
	}
}

void RegisterWeaponModelVariant( std::array<char, MAX_QPATH> &pathBuffer, const char *baseModel, const char *suffix, qhandle_t &target ) {
	COM_StripExtension( baseModel, pathBuffer.data(), pathBuffer.size() );
	Q_strcat( pathBuffer.data(), pathBuffer.size(), suffix );
	target = trap_R_RegisterModel( pathBuffer.data() );
}

using WeaponTrailFunction = void (*)( centity_t *, const weaponInfo_t * );

void SetWeaponFlashLightColor( weaponInfo_t &weaponInfo, const float red, const float green, const float blue ) noexcept {
	MAKERGB( weaponInfo.flashDlightColor, red, green, blue );
}

void SetWeaponMissileLightColor( weaponInfo_t &weaponInfo, const float red, const float green, const float blue ) noexcept {
	MAKERGB( weaponInfo.missileDlightColor, red, green, blue );
}

void RegisterWeaponFlashSounds( weaponInfo_t &weaponInfo, std::span<const char *const> soundPaths ) {
	for ( size_t soundIndex = 0; soundIndex < soundPaths.size(); ++soundIndex ) {
		weaponInfo.flashSound[soundIndex] = trap_S_RegisterSound( soundPaths[soundIndex], qfalse );
	}
}

void RegisterWeaponFlashSound( weaponInfo_t &weaponInfo, const char *soundPath ) {
	const std::array<const char *, 1> soundPaths{ soundPath };
	RegisterWeaponFlashSounds( weaponInfo, soundPaths );
}

void ConfigureWeaponTrail( weaponInfo_t &weaponInfo, WeaponTrailFunction trailFunction, const float trailTime, const float trailRadius ) noexcept {
	weaponInfo.missileTrailFunc = trailFunction;
	weaponInfo.wiTrailTime = trailTime;
	weaponInfo.trailRadius = trailRadius;
}

void ConfigureWeaponMissileTrail(
	weaponInfo_t &weaponInfo,
	WeaponTrailFunction trailFunction,
	const float missileDlight,
	const float trailTime,
	const float trailRadius ) noexcept {
	ConfigureWeaponTrail( weaponInfo, trailFunction, trailTime, trailRadius );
	weaponInfo.missileDlight = missileDlight;
}

void RegisterLightningBeamMedia() {
	cgs.media.lightningShader = trap_R_RegisterShader( "lightningBoltNew" );
}

void RegisterLightningImpactMedia() {
	cgs.media.lightningExplosionModel = trap_R_RegisterModel( "models/weaphits/crackle.md3" );
	cgs.media.sfx_lghit1 = trap_S_RegisterSound( "sound/weapons/lightning/lg_hit.wav", qfalse );
	cgs.media.sfx_lghit2 = trap_S_RegisterSound( "sound/weapons/lightning/lg_hit2.wav", qfalse );
	cgs.media.sfx_lghit3 = trap_S_RegisterSound( "sound/weapons/lightning/lg_hit3.wav", qfalse );
}

void RegisterBulletExplosionShader() {
	cgs.media.bulletExplosionShader = trap_R_RegisterShader( "bulletExplosion" );
}

void RegisterGrenadeExplosionShader() {
	cgs.media.grenadeExplosionShader = trap_R_RegisterShader( "grenadeExplosion" );
}

void RegisterRocketExplosionShader() {
	cgs.media.rocketExplosionShader = trap_R_RegisterShader( "rocketExplosion" );
}

void RegisterRailDiscShader() {
	cgs.media.railRingsShader = trap_R_RegisterShader( "railDisc" );
}

void RegisterPlasmaMedia() {
	cgs.media.plasmaExplosionShader = trap_R_RegisterShader( "plasmaExplosion" );
	RegisterRailDiscShader();
}

void RegisterRailgunMedia() {
	cgs.media.railExplosionShader = trap_R_RegisterShader( "railExplosion" );
	RegisterRailDiscShader();
	cgs.media.railCoreShader = trap_R_RegisterShader( "railCore" );
}

void RegisterBfgMedia() {
	cgs.media.bfgExplosionShader = trap_R_RegisterShader( "bfgExplosion" );
}

[[nodiscard]] bool IsDirectViewLightning( const centity_t &cent ) noexcept {
	return !cg.renderingThirdPerson && cent.currentState.number == cg.predictedPlayerState.clientNum;
}

void CalculateLightningMuzzlePoint( const centity_t &cent, const bool directView, vec3_t muzzlePoint ) {
	if ( directView ) {
		VectorCopy( cg.refdef.vieworg, muzzlePoint );
		return;
	}

	VectorCopy( cent.lerpOrigin, muzzlePoint );
	const int legsAnimation = cent.currentState.legsAnim & ~ANIM_TOGGLEBIT;
	muzzlePoint[2] += IsCrouchedLegsAnimation( legsAnimation ) ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT;
}

void CalculateLightningForward( const centity_t &cent, const bool directView, vec3_t forward ) {
	if ( directView && cg_trueLightning.value ) {
		vec3_t angle;
		for ( int axisIndex = 0; axisIndex < 3; ++axisIndex ) {
			const float delta = WrappedAngleDelta( cent.lerpAngles[axisIndex], cg.refdefViewAngles[axisIndex] );
			angle[axisIndex] = cg.refdefViewAngles[axisIndex] + delta * ( 1.0f - cg_trueLightning.value );
			if ( angle[axisIndex] < 0.0f ) {
				angle[axisIndex] += 360.0f;
			}
			if ( angle[axisIndex] > 360.0f ) {
				angle[axisIndex] -= 360.0f;
			}
		}

		AngleVectors( angle, forward, nullptr, nullptr );
		return;
	}

	AngleVectors( cent.lerpAngles, forward, nullptr, nullptr );
}

[[nodiscard]] refEntity_t LightningBeamEntity( const vec3_t origin, const vec3_t endPoint ) noexcept {
	refEntity_t beam{};
	VectorCopy( endPoint, beam.oldorigin );
	VectorCopy( origin, beam.origin );
	beam.reType = RT_LIGHTNING;
	beam.customShader = cgs.media.lightningShader;
	return beam;
}

void AddLightningImpact( const trace_t &trace, const refEntity_t &beam ) {
	vec3_t dir;
	VectorSubtract( beam.oldorigin, beam.origin, dir );
	VectorNormalize( dir );

	refEntity_t impact{};
	impact.hModel = cgs.media.lightningExplosionModel;
	VectorMA( trace.endpos, -16, dir, impact.origin );

	vec3_t angles;
	angles[0] = rand() % 360;
	angles[1] = rand() % 360;
	angles[2] = rand() % 360;
	AnglesToAxis( angles, impact.axis );
	trap_R_AddRefEntityToScene( &impact );
}

void ComputeViewWeaponFovOffset( vec3_t fovOffset ) noexcept {
	if ( cgs.fov > 90.0f ) {
		VectorSet( fovOffset, 0.0f, 0.0f, -0.2f * ( cgs.fov - 90.0f ) );
		return;
	}

	VectorSet( fovOffset, -0.2f * ( cgs.fov - 90.0f ), 0.0f, 0.0f );
}

void ApplyViewWeaponPositionOffset( refEntity_t &hand, const vec3_t fovOffset ) {
	VectorMA( hand.origin, cg_gun_x.value + fovOffset[0], cg.refdef.viewaxis[0], hand.origin );
	VectorMA( hand.origin, cg_gun_y.value, cg.refdef.viewaxis[1], hand.origin );
	VectorMA( hand.origin, cg_gun_z.value + fovOffset[2], cg.refdef.viewaxis[2], hand.origin );
}

[[nodiscard]] float *WeaponSelectColor() {
	if ( cg_drawWeaponSelect.integer < 0 ) {
		return colorWhite;
	}

	return CG_FadeColor( cg.weaponSelectTime, WEAPON_SELECT_TIME );
}

[[nodiscard]] bool HasWeaponBit( const int weaponBits, const int weaponNum ) noexcept {
	return ( weaponBits & ( 1 << weaponNum ) ) != 0;
}

[[nodiscard]] int CountOwnedWeapons( const int weaponBits ) noexcept {
	int count = 0;
	for ( int weaponNum = WP_GAUNTLET; weaponNum < MAX_WEAPONS; ++weaponNum ) {
		if ( HasWeaponBit( weaponBits, weaponNum ) ) {
			++count;
		}
	}
	return count;
}

[[nodiscard]] WeaponSelectLayout ResolveWeaponSelectLayout( const int weaponSelectMode, const int count ) noexcept {
	if ( weaponSelectMode < 3 ) {
		return WeaponSelectLayout{
			320 - count * 20,
			static_cast<int>( cgs.screenYmax + 1 - 100 ),
			40,
			0,
		};
	}

	return WeaponSelectLayout{
		static_cast<int>( cgs.screenXmin + 6 ),
		240 - count * 20,
		0,
		40,
	};
}

void DrawWeaponSelectAmmoCount( const int x, const int y, const int weaponSelectMode, const int ammo, float *color ) {
	if ( ammo <= 0 || weaponSelectMode <= 1 ) {
		return;
	}

	std::array<char, 16> ammoText{};
	BG_sprintf( ammoText.data(), "%i", ammo );

	if ( weaponSelectMode == 2 ) {
		CG_DrawString( x + 32 / 2, y - 20, ammoText.data(), color, kAmmoFontSize, kAmmoFontSize, 0, DS_CENTER | DS_PROPORTIONAL );
		return;
	}

	CG_DrawString( x + 39 + ( 3 * kAmmoFontSize ), y + ( 32 - kAmmoFontSize ) / 2, ammoText.data(), color, kAmmoFontSize, kAmmoFontSize, 0, DS_RIGHT );
}

[[nodiscard]] sfxHandle_t ChooseWeaponFlashSound( const weaponInfo_t &weapon ) {
	int soundCount = 0;
	for ( const sfxHandle_t flashSound : std::span{ weapon.flashSound } ) {
		if ( !flashSound ) {
			break;
		}
		++soundCount;
	}

	if ( soundCount == 0 ) {
		return 0;
	}

	return weapon.flashSound[rand() % soundCount];
}

void AddProjectileWaterTrailEffects( const vec3_t start, const vec3_t waterTraceEnd, const vec3_t impactPoint ) {
	trace_t trace{};
	const int sourceContentType = CG_PointContents( start, 0 );
	const int destContentType = CG_PointContents( impactPoint, 0 );

	if ( sourceContentType == destContentType && ( sourceContentType & CONTENTS_WATER ) ) {
		CG_BubbleTrail( start, impactPoint, kBubbleTrailSpacing );
		return;
	}

	if ( sourceContentType & CONTENTS_WATER ) {
		trap_CM_BoxTrace( &trace, waterTraceEnd, start, nullptr, nullptr, 0, CONTENTS_WATER );
		CG_BubbleTrail( start, trace.endpos, kBubbleTrailSpacing );
		return;
	}

	if ( destContentType & CONTENTS_WATER ) {
		trap_CM_BoxTrace( &trace, start, waterTraceEnd, nullptr, nullptr, 0, CONTENTS_WATER );
		CG_BubbleTrail( impactPoint, trace.endpos, kBubbleTrailSpacing );
	}
}

[[nodiscard]] trace_t TraceShotgunPellet( const vec3_t start, const vec3_t end, const int skipNum ) {
	trace_t trace{};
	CG_Trace( &trace, start, nullptr, nullptr, end, skipNum, MASK_SHOT );
	return trace;
}

[[nodiscard]] impactSound_t ImpactSoundForSurfaceFlags( const int surfaceFlags ) noexcept {
	return ( surfaceFlags & SURF_METALSTEPS ) != 0 ? IMPACTSOUND_METAL : IMPACTSOUND_DEFAULT;
}

void SetTracerVertex( polyVert_t &vertex, const vec3_t position, const float s, const float t ) {
	VectorCopy( position, vertex.xyz );
	vertex.st[0] = s;
	vertex.st[1] = t;
	vertex.modulate[0] = kWeaponEntityAlpha;
	vertex.modulate[1] = kWeaponEntityAlpha;
	vertex.modulate[2] = kWeaponEntityAlpha;
	vertex.modulate[3] = kWeaponEntityAlpha;
}

void AddShotgunSmoke( const entityState_t &entityState, const vec3_t smokeOrigin ) {
	if ( cgs.glconfig.hardwareType == GLHW_RAGEPRO ) {
		return;
	}

	if ( CG_PointContents( entityState.pos.trBase, 0 ) & CONTENTS_WATER ) {
		return;
	}

	vec3_t up;
	VectorSet( up, 0, 0, 8 );
	CG_SmokePuff( smokeOrigin, up, 32, 1, 1, 1, 0.33f, 900, cg.time, 0, LEF_PUFF_DONT_SCALE, cgs.media.shotgunSmokePuffShader );
}

[[nodiscard]] sfxHandle_t RandomLightningImpactSound() noexcept {
	const int soundIndex = rand() & 3;
	if ( soundIndex < 2 ) {
		return cgs.media.sfx_lghit2;
	}
	if ( soundIndex == 2 ) {
		return cgs.media.sfx_lghit1;
	}
	return cgs.media.sfx_lghit3;
}

[[nodiscard]] sfxHandle_t RandomRicochetSound() noexcept {
	switch ( rand() & 3 ) {
	case 0:
		return cgs.media.sfx_ric1;
	case 1:
		return cgs.media.sfx_ric2;
	default:
		return cgs.media.sfx_ric3;
	}
}

#ifdef MISSIONPACK
[[nodiscard]] sfxHandle_t NailgunImpactSound( const impactSound_t soundType ) noexcept {
	switch ( soundType ) {
	case IMPACTSOUND_FLESH:
		return cgs.media.sfx_nghitflesh;
	case IMPACTSOUND_METAL:
		return cgs.media.sfx_nghitmetal;
	default:
		return cgs.media.sfx_nghit;
	}
}
#endif

void SpawnRocketImpactParticles( const vec3_t origin, const vec3_t dir ) {
	vec3_t particleOrigin;
	vec3_t particleVelocity;
	VectorMA( origin, 24, dir, particleOrigin );
	VectorScale( dir, 64, particleVelocity );
	CG_ParticleExplosion( "explode1", particleOrigin, particleVelocity, 1400, 20, 30 );
}

void AddMissileImpactSound( const vec3_t origin, const sfxHandle_t sound ) {
	if ( sound ) {
		trap_S_StartSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, sound );
	}
}

void ApplyMissileImpactLighting( localEntity_t &explosion, const MissileImpactSpec &spec ) {
	explosion.light = spec.light;
	VectorCopy( spec.lightColor.data(), explosion.lightColor );
}

void ApplyRailgunExplosionColor( localEntity_t &explosion, const int clientNum ) {
	const float *color = cgs.clientinfo[clientNum].color1;
	VectorCopy( color, explosion.color );
	SetWeaponEntityColor( explosion.refEntity, color );
}

void AddMissileImpactExplosion( const MissileImpactSpec &spec, const int weapon, const int clientNum, const vec3_t origin, const vec3_t dir ) {
	if ( !spec.model ) {
		return;
	}

	localEntity_t *explosion = CG_MakeExplosion( origin, dir, spec.model, spec.shader, spec.duration, spec.isSprite ? qtrue : qfalse );
	ApplyMissileImpactLighting( *explosion, spec );
	if ( weapon == WP_RAILGUN ) {
		ApplyRailgunExplosionColor( *explosion, clientNum );
	}
}

void AddMissileImpactMark( const MissileImpactSpec &spec, const int weapon, const int clientNum, const vec3_t origin, const vec3_t dir ) {
	const qboolean alphaFade = spec.mark == cgs.media.energyMarkShader ? qtrue : qfalse;
	if ( weapon == WP_RAILGUN ) {
		const float *color = cgs.clientinfo[clientNum].color1;
		CG_ImpactMark( spec.mark, origin, dir, random() * 360, color[0], color[1], color[2], 1.0f, alphaFade, spec.radius, qfalse );
		return;
	}

	CG_ImpactMark( spec.mark, origin, dir, random() * 360, 1.0f, 1.0f, 1.0f, 1.0f, alphaFade, spec.radius, qfalse );
}

[[nodiscard]] bool WeaponCreatesFleshExplosion( const int weapon ) noexcept {
	switch ( weapon ) {
	case WP_GRENADE_LAUNCHER:
	case WP_ROCKET_LAUNCHER:
	case WP_PLASMAGUN:
	case WP_BFG:
#ifdef MISSIONPACK
	case WP_NAILGUN:
	case WP_CHAINGUN:
	case WP_PROX_LAUNCHER:
#endif
		return true;
	default:
		return false;
	}
}

void TransformLocalVector( const vec3_t *axis, const vec3_t localVector, vec3_t worldVector ) {
	worldVector[0] = localVector[0] * axis[0][0] + localVector[1] * axis[1][0] + localVector[2] * axis[2][0];
	worldVector[1] = localVector[0] * axis[0][1] + localVector[1] * axis[1][1] + localVector[2] * axis[2][1];
	worldVector[2] = localVector[0] * axis[0][2] + localVector[1] * axis[1][2] + localVector[2] * axis[2][2];
}

void OffsetOriginFromBase( const vec3_t baseOrigin, const vec3_t *axis, const vec3_t localOffset, vec3_t origin ) {
	vec3_t transformedOffset;
	TransformLocalVector( axis, localOffset, transformedOffset );
	VectorAdd( baseOrigin, transformedOffset, origin );
}

void BrassOriginForEntity( const centity_t &cent, const vec3_t *axis, const vec3_t localOffset, vec3_t origin ) {
	OffsetOriginFromBase( cent.lerpOrigin, axis, localOffset, origin );
}

[[nodiscard]] float BrassWaterScaleAtOrigin( const vec3_t origin ) noexcept {
	return ( CG_PointContents( origin, -1 ) & CONTENTS_WATER ) != 0 ? kBrassWaterScale : 1.0f;
}

void InitializeBrassFragment( localEntity_t &fragment, const int startTime, const int endTime, const int trajectoryTime, const vec3_t origin, const vec3_t velocity, const qhandle_t model, const float bounceFactor ) {
	fragment.leType = LE_FRAGMENT;
	fragment.startTime = startTime;
	fragment.endTime = endTime;
	fragment.pos.trType = TR_GRAVITY;
	fragment.pos.trTime = trajectoryTime;
	VectorCopy( origin, fragment.refEntity.origin );
	VectorCopy( origin, fragment.pos.trBase );
	VectorCopy( velocity, fragment.pos.trDelta );
	AxisCopy( axisDefault, fragment.refEntity.axis );
	fragment.refEntity.hModel = model;
	fragment.bounceFactor = bounceFactor;
	fragment.leFlags = LEF_TUMBLE;
	fragment.leBounceSoundType = LEBS_BRASS;
	fragment.leMarkType = LEMT_NONE;
}

void InitializeBrassTumble( localEntity_t &fragment, const int trajectoryTime, const vec3_t angularVelocity ) {
	fragment.angles.trType = TR_LINEAR;
	fragment.angles.trTime = trajectoryTime;
	fragment.angles.trBase[0] = rand() & kRandomAngleMask;
	fragment.angles.trBase[1] = rand() & kRandomAngleMask;
	fragment.angles.trBase[2] = rand() & kRandomAngleMask;
	VectorCopy( angularVelocity, fragment.angles.trDelta );
}

void InitializeTimedLocalEntity( localEntity_t &entity, const leType_t type, const int startTime, const int endTime ) {
	entity.leType = type;
	entity.startTime = startTime;
	entity.endTime = endTime;
	entity.lifeRate = 1.0f / static_cast<float>( endTime - startTime );
}

void SetCurrentShaderTime( refEntity_t &entity ) {
	if ( intShaderTime ) {
		entity.u.intShaderTime = cg.time;
		return;
	}

	entity.u.shaderTime = cg.time / 1000.0f;
}

void SetRefEntityColor( refEntity_t &entity, const vec3_t color, const float scale = 255.0f, const byte alpha = kWeaponEntityAlpha ) {
	entity.shaderRGBA[0] = static_cast<byte>( color[0] * scale );
	entity.shaderRGBA[1] = static_cast<byte>( color[1] * scale );
	entity.shaderRGBA[2] = static_cast<byte>( color[2] * scale );
	entity.shaderRGBA[3] = alpha;
}

void SetLocalEntityColor( localEntity_t &entity, const vec3_t color, const float scale, const float alpha ) {
	entity.color[0] = color[0] * scale;
	entity.color[1] = color[1] * scale;
	entity.color[2] = color[2] * scale;
	entity.color[3] = alpha;
}

void InitializeGravitySpriteTrail(
	localEntity_t &entity,
	const int startTime,
	const int endTime,
	const int trajectoryTime,
	const vec3_t origin,
	const vec3_t velocity,
	const qhandle_t shader,
	const float radius,
	const float bounceFactor ) {
	InitializeTimedLocalEntity( entity, LE_MOVE_SCALE_FADE, startTime, endTime );
	entity.leFlags = LEF_TUMBLE;
	entity.leBounceSoundType = LEBS_NONE;
	entity.leMarkType = LEMT_NONE;
	entity.pos.trType = TR_GRAVITY;
	entity.pos.trTime = trajectoryTime;
	VectorCopy( origin, entity.refEntity.origin );
	VectorCopy( origin, entity.pos.trBase );
	VectorCopy( velocity, entity.pos.trDelta );
	AxisCopy( axisDefault, entity.refEntity.axis );
	SetCurrentShaderTime( entity.refEntity );
	entity.refEntity.reType = RT_SPRITE;
	entity.refEntity.radius = radius;
	entity.refEntity.customShader = shader;
	entity.bounceFactor = bounceFactor;
}

void InitializeProjectileSmokeTrail( centity_t &entity, const weaponInfo_t &weaponInfo, const qhandle_t shader ) {
	if ( cg_noProjectileTrail.integer ) {
		return;
	}

	entityState_t &state = entity.currentState;
	vec3_t currentOrigin;
	BG_EvaluateTrajectory( &state.pos, cg.time, currentOrigin );
	const int currentContents = CG_PointContents( currentOrigin, -1 );

	// If the projectile is stationary, there is no trail to emit.
	if ( state.pos.trType == TR_STATIONARY ) {
		entity.trailTime = cg.time;
		return;
	}

	vec3_t lastOrigin;
	BG_EvaluateTrajectory( &state.pos, entity.trailTime, lastOrigin );
	const int lastContents = CG_PointContents( lastOrigin, -1 );

	const int trailEndTime = cg.time;
	int trailTime = kProjectileTrailStepMs * ( ( entity.trailTime + kProjectileTrailStepMs ) / kProjectileTrailStepMs );
	entity.trailTime = trailEndTime;

	if ( currentContents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		if ( currentContents & lastContents & CONTENTS_WATER ) {
			CG_BubbleTrail( lastOrigin, currentOrigin, kProjectileBubbleTrailSpacing );
		}
		return;
	}

	vec3_t up{};
	for ( ; trailTime <= trailEndTime; trailTime += kProjectileTrailStepMs ) {
		BG_EvaluateTrajectory( &state.pos, trailTime, lastOrigin );
		localEntity_t *smoke = CG_SmokePuff(
			lastOrigin,
			up,
			weaponInfo.trailRadius,
			1.0f,
			1.0f,
			1.0f,
			0.33f,
			weaponInfo.wiTrailTime,
			trailTime,
			0,
			0,
			shader );
		smoke->leType = LE_SCALE_FADE;
	}
}

} // namespace

/*
==========================
CG_MachineGunEjectBrass
==========================
*/
static void CG_MachineGunEjectBrass( centity_t *cent ) {
	if ( cg_brassTime.integer <= 0 ) {
		return;
	}

	vec3_t axis[3];
	AnglesToAxis( cent->lerpAngles, axis );

	vec3_t localVelocity{
		0.0f,
		static_cast<float>( -50 + 40 * crandom() ),
		static_cast<float>( 100 + 50 * crandom() ),
	};
	vec3_t localOffset{ 8, -4, 24 };
	vec3_t origin;
	BrassOriginForEntity( *cent, axis, localOffset, origin );

	const float waterScale = BrassWaterScaleAtOrigin( origin );

	vec3_t velocity;
	TransformLocalVector( axis, localVelocity, velocity );
	VectorScale( velocity, waterScale, velocity );

	localEntity_t &fragment = *CG_AllocLocalEntity();
	InitializeBrassFragment(
		fragment,
		cg.time,
		cg.time + cg_brassTime.integer + ( cg_brassTime.integer / 4 ) * random(),
		cg.time - ( rand() & 15 ),
		origin,
		velocity,
		cgs.media.machinegunBrassModel,
		0.4f * waterScale );

	vec3_t angularVelocity{ 2, 1, 0 };
	InitializeBrassTumble( fragment, cg.time, angularVelocity );
}

/*
==========================
CG_ShotgunEjectBrass
==========================
*/
static void CG_ShotgunEjectBrass( centity_t *cent ) {
	if ( cg_brassTime.integer <= 0 ) {
		return;
	}

	vec3_t axis[3];
	AnglesToAxis( cent->lerpAngles, axis );

	vec3_t localOffset{ 8, 0, 24 };
	vec3_t angularVelocity{ 1, 0.5f, 0 };

	for ( const float lateralVelocityBias : kShotgunLateralVelocityBiases ) {
		vec3_t localVelocity{
			static_cast<float>( 60 + 60 * crandom() ),
			static_cast<float>( lateralVelocityBias + 10 * crandom() ),
			static_cast<float>( 100 + 50 * crandom() ),
		};
		vec3_t origin;
		BrassOriginForEntity( *cent, axis, localOffset, origin );

		const float waterScale = BrassWaterScaleAtOrigin( origin );

		vec3_t velocity;
		TransformLocalVector( axis, localVelocity, velocity );
		VectorScale( velocity, waterScale, velocity );

		localEntity_t &fragment = *CG_AllocLocalEntity();
		InitializeBrassFragment(
			fragment,
			cg.time,
			cg.time + cg_brassTime.integer * 3 + cg_brassTime.integer * random(),
			cg.time,
			origin,
			velocity,
			cgs.media.shotgunBrassModel,
			0.3f );

		InitializeBrassTumble( fragment, cg.time, angularVelocity );
	}
}


#ifdef MISSIONPACK
/*
==========================
CG_NailgunEjectBrass
==========================
*/
static void CG_NailgunEjectBrass( centity_t *cent ) {
	localEntity_t	*smoke;
	vec3_t origin;
	vec3_t axis[3];
	AnglesToAxis( cent->lerpAngles, axis );

	vec3_t localOffset{ 0, -12, 24 };
	BrassOriginForEntity( *cent, axis, localOffset, origin );

	vec3_t up;
	VectorSet( up, 0, 0, 64 );

	smoke = CG_SmokePuff( origin, up, 32, 1, 1, 1, 0.33f, 700, cg.time, 0, 0, cgs.media.smokePuffShader );
	// use the optimized local entity add
	smoke->leType = LE_SCALE_FADE;
}
#endif


/*
==========================
CG_RailTrail
==========================
*/
void CG_RailTrail( const clientInfo_t *ci, const vec3_t start, const vec3_t end ) {
	std::array<vec3_t, kRailAxisCount> axis{};
	vec3_t move;
	vec3_t direction;
	vec3_t temp;

	localEntity_t &core = *CG_AllocLocalEntity();
	InitializeTimedLocalEntity( core, LE_FADE_RGB, cg.time, cg.time + cg_railTrailTime.value );

	refEntity_t &coreEntity = core.refEntity;
	SetCurrentShaderTime( coreEntity );
	coreEntity.reType = RT_RAIL_CORE;
	coreEntity.customShader = cgs.media.railCoreShader;
	VectorCopy( start, coreEntity.origin );
	VectorCopy( end, coreEntity.oldorigin );
	SetRefEntityColor( coreEntity, ci->color1 );
	SetLocalEntityColor( core, ci->color1, 0.75f, 1.0f );
	AxisClear( coreEntity.axis );

	if ( cg_oldRail.integer != 0 ) {
		// nudge down a bit so it isn't exactly in center
		//re->origin[2] -= 8;
		//re->oldorigin[2] -= 8;
		return;
	}

	//start[2] -= 4;
	VectorCopy( start, move );
	VectorSubtract( end, start, direction );
	const float length = VectorNormalize( direction );
	PerpendicularVector( temp, direction );

	for ( int axisIndex = 0; axisIndex < kRailAxisCount; ++axisIndex ) {
		RotatePointAroundVector( axis[axisIndex], direction, temp, axisIndex * kRailRotationDegrees ); //banshee 2.4 was 10
	}

	VectorMA( move, kRailRingOffset, direction, move );
	vec3_t stepDirection;
	VectorScale( direction, kRailSpacing, stepDirection );

	int skip = -1;
	int ringAxisIndex = kRailInitialAxisIndex;
	for ( int distance = 0; distance < length; distance += kRailSpacing ) {
		if ( distance != skip ) {
			skip = distance + kRailSpacing;

			localEntity_t &ring = *CG_AllocLocalEntity();
			ring.leFlags = LEF_PUFF_DONT_SCALE;
			InitializeTimedLocalEntity( ring, LE_MOVE_SCALE_FADE, cg.time, cg.time + ( distance >> 1 ) + 600 );

			refEntity_t &ringEntity = ring.refEntity;
			SetCurrentShaderTime( ringEntity );
			ringEntity.reType = RT_SPRITE;
			ringEntity.radius = kRailRingSpriteRadius;
			ringEntity.customShader = cgs.media.railRingsShader;
			SetRefEntityColor( ringEntity, ci->color2 );
			SetLocalEntityColor( ring, ci->color2, 0.75f, 1.0f );

			ring.pos.trType = TR_LINEAR;
			ring.pos.trTime = cg.time;

			vec3_t ringOrigin;
			VectorCopy( move, ringOrigin );
			VectorMA( ringOrigin, kRailRingRadius, axis[ringAxisIndex], ringOrigin );
			VectorCopy( ringOrigin, ring.pos.trBase );
			ring.pos.trDelta[0] = axis[ringAxisIndex][0] * kRailRingVelocityScale;
			ring.pos.trDelta[1] = axis[ringAxisIndex][1] * kRailRingVelocityScale;
			ring.pos.trDelta[2] = axis[ringAxisIndex][2] * kRailRingVelocityScale;
		}

		VectorAdd( move, stepDirection, move );

		ringAxisIndex = ( ringAxisIndex + kRailRotationStep ) % kRailAxisCount;
	}
}


/*
==========================
CG_RocketTrail
==========================
*/
static void CG_RocketTrail( centity_t *ent, const weaponInfo_t *wi ) {
	InitializeProjectileSmokeTrail( *ent, *wi, cgs.media.smokePuffShader );
}

#ifdef MISSIONPACK
/*
==========================
CG_NailTrail
==========================
*/
static void CG_NailTrail( centity_t *ent, const weaponInfo_t *wi ) {
	InitializeProjectileSmokeTrail( *ent, *wi, cgs.media.nailPuffShader );
}
#endif

/*
==========================
CG_PlasmaTrail
==========================
*/
static void CG_PlasmaTrail( centity_t *cent, const weaponInfo_t *wi ) {
	if ( cg_noProjectileTrail.integer || cg_oldPlasma.integer ) {
		return;
	}

	entityState_t &state = cent->currentState;
	vec3_t origin;
	BG_EvaluateTrajectory( &state.pos, cg.time, origin );

	vec3_t axis[3];
	AnglesToAxis( cent->lerpAngles, axis );

	vec3_t localVelocity{
		static_cast<float>( 60 - 120 * crandom() ),
		static_cast<float>( 40 - 80 * crandom() ),
		static_cast<float>( 100 - 200 * crandom() ),
	};
	vec3_t localOffset{ 2.0f, 2.0f, 2.0f };
	vec3_t trailOrigin;
	OffsetOriginFromBase( origin, axis, localOffset, trailOrigin );

	const float waterScale = BrassWaterScaleAtOrigin( trailOrigin );

	vec3_t velocity;
	TransformLocalVector( axis, localVelocity, velocity );
	VectorScale( velocity, waterScale, velocity );

	localEntity_t &trail = *CG_AllocLocalEntity();
	InitializeGravitySpriteTrail(
		trail,
		cg.time,
		cg.time + kPlasmaTrailLifetimeMs,
		cg.time,
		trailOrigin,
		velocity,
		cgs.media.railRingsShader,
		kPlasmaTrailSpriteRadius,
		0.3f );

	SetRefEntityColor( trail.refEntity, wi->flashDlightColor, kPlasmaTrailShaderColorScale, kPlasmaTrailShaderAlpha );
	SetLocalEntityColor( trail, wi->flashDlightColor, kPlasmaTrailLocalColorScale, kPlasmaTrailLocalAlpha );

	vec3_t angularVelocity{ 1.0f, 0.5f, 0.0f };
	InitializeBrassTumble( trail, cg.time, angularVelocity );
}


/*
==========================
CG_GrappleTrail
==========================
*/
void CG_GrappleTrail( centity_t *ent, const weaponInfo_t *wi ) {
	vec3_t origin;
	entityState_t &state = ent->currentState;
	BG_EvaluateTrajectory( &state.pos, cg.time, origin );
	ent->trailTime = cg.time;

	refEntity_t beam{};
	VectorCopy( cg_entities[state.otherEntityNum].pe.muzzleOrigin, beam.origin );
	VectorCopy( origin, beam.oldorigin );

	if ( Distance( beam.origin, beam.oldorigin ) < kGrappleTrailMinDistance ) {
		return; // Don't draw if close
	}

	beam.reType = RT_LIGHTNING;
	beam.customShader = cgs.media.lightningShader;
	AxisClear( beam.axis );
	SetRefEntityColor( beam, kWhiteColor.data() );
	trap_R_AddRefEntityToScene( &beam );
}

/*
==========================
CG_GrenadeTrail
==========================
*/
static void CG_GrenadeTrail( centity_t *ent, const weaponInfo_t *wi ) {
	CG_RocketTrail( ent, wi );
}


/*
=================
CG_RegisterWeapon

The server says this item is used on this level
=================
*/
void CG_RegisterWeapon( int weaponNum ) {
	weaponInfo_t	*weaponInfo;
	std::array<char, MAX_QPATH> path{};

	weaponInfo = &cg_weapons[weaponNum];

	if ( weaponNum == 0 ) {
		return;
	}

	if ( weaponInfo->registered ) {
		return;
	}

	ResetWeaponInfo( *weaponInfo );

	gitem_t *item = FindWeaponItemByTag( weaponNum );
	if ( item == nullptr ) {
		CG_Error( "Couldn't find weapon %i", weaponNum );
	}
	weaponInfo->item = item;
	CG_RegisterItemVisuals( static_cast<int>( item - bg_itemlist ) );

	// load cmodel before model so filecache works
	weaponInfo->weaponModel = trap_R_RegisterModel( item->world_model[0] );

	// calc midpoint for rotation
	SetWeaponMidpoint( weaponInfo->weaponModel, weaponInfo->weaponMidpoint );

	weaponInfo->weaponIcon = trap_R_RegisterShader( item->icon );
	weaponInfo->ammoIcon = trap_R_RegisterShader( item->icon );

	if ( gitem_t *ammo = FindAmmoItemByTag( weaponNum ); ammo != nullptr && ammo->world_model[0] ) {
		weaponInfo->ammoModel = trap_R_RegisterModel( ammo->world_model[0] );
	}

	RegisterWeaponModelVariant( path, item->world_model[0], "_flash.md3", weaponInfo->flashModel );
	RegisterWeaponModelVariant( path, item->world_model[0], "_barrel.md3", weaponInfo->barrelModel );
	RegisterWeaponModelVariant( path, item->world_model[0], "_hand.md3", weaponInfo->handsModel );

	if ( !weaponInfo->handsModel ) {
		weaponInfo->handsModel = trap_R_RegisterModel( "models/weapons2/shotgun/shotgun_hand.md3" );
	}

	weaponInfo->loopFireSound = qfalse;

	switch ( weaponNum ) {
	case WP_GAUNTLET:
		SetWeaponFlashLightColor( *weaponInfo, 0.6f, 0.6f, 1.0f );
		weaponInfo->firingSound = trap_S_RegisterSound( "sound/weapons/melee/fstrun.wav", qfalse );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/melee/fstatck.wav" );
		break;

	case WP_LIGHTNING:
		SetWeaponFlashLightColor( *weaponInfo, 0.6f, 0.6f, 1.0f );
		weaponInfo->readySound = trap_S_RegisterSound( "sound/weapons/melee/fsthum.wav", qfalse );
		weaponInfo->firingSound = trap_S_RegisterSound( "sound/weapons/lightning/lg_hum.wav", qfalse );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/lightning/lg_fire.wav" );
		RegisterLightningBeamMedia();
		RegisterLightningImpactMedia();
		break;

	case WP_GRAPPLING_HOOK:
		SetWeaponFlashLightColor( *weaponInfo, 0.6f, 0.6f, 1.0f );
		weaponInfo->missileModel = trap_R_RegisterModel( "models/ammo/rocket/rocket.md3" );
		ConfigureWeaponMissileTrail( *weaponInfo, CG_GrappleTrail, HOOK_GLOW_RADIUS, 2000, 64 );
		SetWeaponMissileLightColor( *weaponInfo, 1.0f, 0.75f, 0.0f );
		weaponInfo->readySound = trap_S_RegisterSound( "sound/weapons/melee/fsthum.wav", qfalse );
		weaponInfo->firingSound = trap_S_RegisterSound( "sound/weapons/melee/fstrun.wav", qfalse );
		RegisterLightningBeamMedia();
		break;

#ifdef MISSIONPACK
	case WP_CHAINGUN:
	{
		constexpr std::array<const char *, 4> kChaingunFlashSounds{
			"sound/weapons/vulcan/vulcanf1b.wav",
			"sound/weapons/vulcan/vulcanf2b.wav",
			"sound/weapons/vulcan/vulcanf3b.wav",
			"sound/weapons/vulcan/vulcanf4b.wav",
		};

		weaponInfo->firingSound = trap_S_RegisterSound( "sound/weapons/vulcan/wvulfire.wav", qfalse );
		weaponInfo->loopFireSound = qtrue;
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 1.0f, 0.0f );
		RegisterWeaponFlashSounds( *weaponInfo, kChaingunFlashSounds );
		weaponInfo->ejectBrassFunc = CG_MachineGunEjectBrass;
		RegisterBulletExplosionShader();
		break;
	}
#endif

	case WP_MACHINEGUN:
	{
		constexpr std::array<const char *, 4> kMachineGunFlashSounds{
			"sound/weapons/machinegun/machgf1b.wav",
			"sound/weapons/machinegun/machgf2b.wav",
			"sound/weapons/machinegun/machgf3b.wav",
			"sound/weapons/machinegun/machgf4b.wav",
		};

		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 1.0f, 0.0f );
		RegisterWeaponFlashSounds( *weaponInfo, kMachineGunFlashSounds );
		weaponInfo->ejectBrassFunc = CG_MachineGunEjectBrass;
		RegisterBulletExplosionShader();
		break;
	}

	case WP_SHOTGUN:
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 1.0f, 0.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/shotgun/sshotf1b.wav" );
		weaponInfo->ejectBrassFunc = CG_ShotgunEjectBrass;
		break;

	case WP_ROCKET_LAUNCHER:
		weaponInfo->missileModel = trap_R_RegisterModel( "models/ammo/rocket/rocket.md3" );
		weaponInfo->missileSound = trap_S_RegisterSound( "sound/weapons/rocket/rockfly.wav", qfalse );
		ConfigureWeaponMissileTrail( *weaponInfo, CG_RocketTrail, MISSILE_GLOW_RADIUS, 2000, 64 );
		SetWeaponMissileLightColor( *weaponInfo, 1.0f, 0.75f, 0.0f );
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 0.75f, 0.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/rocket/rocklf1a.wav" );
		RegisterRocketExplosionShader();
		break;

#ifdef MISSIONPACK
	case WP_PROX_LAUNCHER:
		weaponInfo->missileModel = trap_R_RegisterModel( "models/weaphits/proxmine.md3" );
		ConfigureWeaponTrail( *weaponInfo, CG_GrenadeTrail, 700, 32 );
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 0.70f, 0.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/proxmine/wstbfire.wav" );
		RegisterGrenadeExplosionShader();
		break;
#endif

	case WP_GRENADE_LAUNCHER:
		weaponInfo->missileModel = trap_R_RegisterModel( "models/ammo/grenade1.md3" );
		ConfigureWeaponTrail( *weaponInfo, CG_GrenadeTrail, 700, 32 );
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 0.70f, 0.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/grenade/grenlf1a.wav" );
		RegisterGrenadeExplosionShader();
		break;

#ifdef MISSIONPACK
	case WP_NAILGUN:
		weaponInfo->ejectBrassFunc = CG_NailgunEjectBrass;
		ConfigureWeaponTrail( *weaponInfo, CG_NailTrail, 250, 16 );
//		weaponInfo->missileSound = trap_S_RegisterSound( "sound/weapons/nailgun/wnalflit.wav", qfalse );
		weaponInfo->missileModel = trap_R_RegisterModel( "models/weaphits/nail.md3" );
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 0.75f, 0.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/nailgun/wnalfire.wav" );
		break;
#endif

	case WP_PLASMAGUN:
//		weaponInfo->missileModel = cgs.media.invulnerabilityPowerupModel;
		weaponInfo->missileTrailFunc = CG_PlasmaTrail;
		weaponInfo->missileSound = trap_S_RegisterSound( "sound/weapons/plasma/lasfly.wav", qfalse );

		// plasmagun dlight
		weaponInfo->missileDlight = MISSILE_GLOW_RADIUS;
		SetWeaponMissileLightColor( *weaponInfo, 0.2f, 0.2f, 1.0f );

		SetWeaponFlashLightColor( *weaponInfo, 0.6f, 0.6f, 1.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/plasma/hyprbf1a.wav" );
		RegisterPlasmaMedia();
		break;

	case WP_RAILGUN:
		weaponInfo->readySound = trap_S_RegisterSound( "sound/weapons/railgun/rg_hum.wav", qfalse );
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 0.5f, 0.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/railgun/railgf1a.wav" );
		RegisterRailgunMedia();
		break;

	case WP_BFG:
		weaponInfo->readySound = trap_S_RegisterSound( "sound/weapons/bfg/bfg_hum.wav", qfalse );

		// bfg dlight
		weaponInfo->missileDlight = MISSILE_GLOW_RADIUS;
		SetWeaponMissileLightColor( *weaponInfo, 0.2f, 1.0f, 0.2f );

		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 0.7f, 1.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/bfg/bfg_fire.wav" );
		RegisterBfgMedia();
		weaponInfo->missileModel = trap_R_RegisterModel( "models/weaphits/bfg.md3" );
		weaponInfo->missileSound = trap_S_RegisterSound( "sound/weapons/rocket/rockfly.wav", qfalse );
		break;

	 default:
		SetWeaponFlashLightColor( *weaponInfo, 1.0f, 1.0f, 1.0f );
		RegisterWeaponFlashSound( *weaponInfo, "sound/weapons/rocket/rocklf1a.wav" );
		break;
	}
}

/*
=================
CG_RegisterItemVisuals

The server says this item is used on this level
=================
*/
void CG_RegisterItemVisuals( int itemNum ) {
	if ( itemNum < 0 || itemNum >= bg_numItems ) {
		CG_Error( "CG_RegisterItemVisuals: itemNum %d out of range [0-%d]", itemNum, bg_numItems-1 );
	}

	itemInfo_t &itemInfo = cg_items[itemNum];
	if ( itemInfo.registered ) {
		return;
	}

	const gitem_t &item = bg_itemlist[itemNum];

	ResetItemInfo( itemInfo );

	itemInfo.models[0] = trap_R_RegisterModel( item.world_model[0] );

	itemInfo.icon = trap_R_RegisterShader( item.icon );

	// try to register depth-fragment shaders
	if ( cg.clientFrame == 0 && cg.skipDFshaders ) {
		itemInfo.icon_df = 0;
	} else {
		itemInfo.icon_df = trap_R_RegisterShader( va( "%s_df", item.icon ) );
	}

	if ( !itemInfo.icon_df ) {
		itemInfo.icon_df = itemInfo.icon;
		if ( cg.clientFrame == 0 ) {
			cg.skipDFshaders = qtrue; // skip all further tries to avoid shader debug mesages in 1.32c during map loading
		} else {
			cg.skipDFshaders = qfalse;
		}
	} else {
		cg.skipDFshaders = qfalse;
	}

	if ( item.giType == IT_WEAPON ) {
		CG_RegisterWeapon( item.giTag );
	}

	//
	// powerups have an accompanying ring or sphere
	//
	if ( item.giType == IT_POWERUP || item.giType == IT_HEALTH || 
		item.giType == IT_ARMOR || item.giType == IT_HOLDABLE ) {
		if ( item.world_model[1] ) {
			itemInfo.models[1] = trap_R_RegisterModel( item.world_model[1] );
		}
	}
}


/*
========================================================================================

VIEW WEAPON

========================================================================================
*/

/*
=================
CG_MapTorsoToWeaponFrame

=================
*/
static int CG_MapTorsoToWeaponFrame( const clientInfo_t *ci, int frame ) {

	// change weapon
	if ( frame >= ci->animations[TORSO_DROP].firstFrame 
		&& frame < ci->animations[TORSO_DROP].firstFrame + 9 ) {
		return frame - ci->animations[TORSO_DROP].firstFrame + 6;
	}

	// stand attack
	if ( frame >= ci->animations[TORSO_ATTACK].firstFrame 
		&& frame < ci->animations[TORSO_ATTACK].firstFrame + 6 ) {
		return 1 + frame - ci->animations[TORSO_ATTACK].firstFrame;
	}

	// stand attack 2
	if ( frame >= ci->animations[TORSO_ATTACK2].firstFrame 
		&& frame < ci->animations[TORSO_ATTACK2].firstFrame + 6 ) {
		return 1 + frame - ci->animations[TORSO_ATTACK2].firstFrame;
	}
	
	return 0;
}


/*
==============
CG_CalculateWeaponPosition
==============
*/
static void CG_CalculateWeaponPosition( vec3_t origin, vec3_t angles ) {
	float	scale;
	int		delta;
	float	fracsin;

	VectorCopy( cg.refdef.vieworg, origin );
	VectorCopy( cg.refdefViewAngles, angles );

	// on odd legs, invert some angles
	if ( cg.bobcycle & 1 ) {
		scale = -cg.xyspeed;
	} else {
		scale = cg.xyspeed;
	}

	// gun angles from bobbing
	angles[ROLL] += scale * cg.bobfracsin * 0.005;
	angles[YAW] += scale * cg.bobfracsin * 0.01;
	angles[PITCH] += cg.xyspeed * cg.bobfracsin * 0.005;

	// drop the weapon when landing
	delta = cg.time - cg.landTime;
	if ( delta < LAND_DEFLECT_TIME ) {
		origin[2] += cg.landChange*0.25 * delta / LAND_DEFLECT_TIME;
	} else if ( delta < LAND_DEFLECT_TIME + LAND_RETURN_TIME ) {
		origin[2] += cg.landChange*0.25 * 
			(LAND_DEFLECT_TIME + LAND_RETURN_TIME - delta) / LAND_RETURN_TIME;
	}

#if 0
	// drop the weapon when stair climbing
	delta = cg.time - cg.stepTime;
	if ( delta < STEP_TIME/2 ) {
		origin[2] -= cg.stepChange*0.25 * delta / (STEP_TIME/2);
	} else if ( delta < STEP_TIME ) {
		origin[2] -= cg.stepChange*0.25 * (STEP_TIME - delta) / (STEP_TIME/2);
	}
#endif

	// idle drift
	scale = cg.xyspeed + 40;
	fracsin = sin( ( cg.time % TMOD_1000 ) * 0.001 );
	angles[ROLL] += scale * fracsin * 0.01;
	angles[YAW] += scale * fracsin * 0.01;
	angles[PITCH] += scale * fracsin * 0.01;
}


/*
===============
CG_LightningBolt

Origin will be the exact tag point, which is slightly
different than the muzzle point used for determining hits.
The cent should be the non-predicted cent if it is from the player,
so the endpoint will reflect the simulated strike (lagging the predicted
angle)
===============
*/
static void CG_LightningBolt( centity_t *cent, vec3_t origin ) {
	if ( cent->currentState.weapon != WP_LIGHTNING ) {
		return;
	}

	const bool directView = IsDirectViewLightning( *cent );
	trace_t trace{};
	vec3_t forward;
	vec3_t muzzlePoint;
	CalculateLightningMuzzlePoint( *cent, directView, muzzlePoint );
	CalculateLightningForward( *cent, directView, forward );

	VectorMA( muzzlePoint, 14, forward, muzzlePoint );

	// project forward by the lightning range
	vec3_t endPoint;
	VectorMA( muzzlePoint, LIGHTNING_RANGE, forward, endPoint );

	// see if it hit a wall
	CG_Trace( &trace, muzzlePoint, vec3_origin, vec3_origin, endPoint,
		cent->currentState.number, MASK_SHOT );

	refEntity_t beam = LightningBeamEntity( origin, trace.endpos );
	trap_R_AddRefEntityToScene( &beam );

	// add the impact flare if it hit something
	if ( trace.fraction < 1.0 ) {
		AddLightningImpact( trace, beam );
	}
}
/*

static void CG_LightningBolt( centity_t *cent, vec3_t origin ) {
	trace_t		trace;
	refEntity_t		beam;
	vec3_t			forward;
	vec3_t			muzzlePoint, endPoint;

	if ( cent->currentState.weapon != WP_LIGHTNING ) {
		return;
	}

	memset( &beam, 0, sizeof( beam ) );

	// find muzzle point for this frame
	VectorCopy( cent->lerpOrigin, muzzlePoint );
	AngleVectors( cent->lerpAngles, forward, NULL, NULL );

	// FIXME: crouch
	muzzlePoint[2] += DEFAULT_VIEWHEIGHT;

	VectorMA( muzzlePoint, 14, forward, muzzlePoint );

	// project forward by the lightning range
	VectorMA( muzzlePoint, LIGHTNING_RANGE, forward, endPoint );

	// see if it hit a wall
	CG_Trace( &trace, muzzlePoint, vec3_origin, vec3_origin, endPoint, 
		cent->currentState.number, MASK_SHOT );

	// this is the endpoint
	VectorCopy( trace.endpos, beam.oldorigin );

	// use the provided origin, even though it may be slightly
	// different than the muzzle origin
	VectorCopy( origin, beam.origin );

	beam.reType = RT_LIGHTNING;
	beam.customShader = cgs.media.lightningShader;
	trap_R_AddRefEntityToScene( &beam );

	// add the impact flare if it hit something
	if ( trace.fraction < 1.0 ) {
		vec3_t	angles;
		vec3_t	dir;

		VectorSubtract( beam.oldorigin, beam.origin, dir );
		VectorNormalize( dir );

		memset( &beam, 0, sizeof( beam ) );
		beam.hModel = cgs.media.lightningExplosionModel;

		VectorMA( trace.endpos, -16, dir, beam.origin );

		// make a random orientation
		angles[0] = rand() % 360;
		angles[1] = rand() % 360;
		angles[2] = rand() % 360;
		AnglesToAxis( angles, beam.axis );
		trap_R_AddRefEntityToScene( &beam );
	}
}
*/

/*
===============
CG_SpawnRailTrail

Origin will be the exact tag point, which is slightly
different than the muzzle point used for determining hits.
===============
*/
static void CG_SpawnRailTrail( centity_t *cent, vec3_t origin ) {
	clientInfo_t	*ci;

	if ( cent->currentState.weapon != WP_RAILGUN ) {
		return;
	}
	if ( !cent->pe.railgunFlash ) {
		return;
	}
	cent->pe.railgunFlash = qtrue;
	ci = &cgs.clientinfo[ cent->currentState.clientNum ];
	CG_RailTrail( ci, origin, cent->pe.railgunImpact );
}


/*
======================
CG_MachinegunSpinAngle
======================
*/
#define		SPIN_SPEED	0.9
#define		COAST_TIME	1000
static float	CG_MachinegunSpinAngle( centity_t *cent ) {
	int		delta;
	float	angle;
	float	speed;

	delta = cg.time - cent->pe.barrelTime;
	if ( cent->pe.barrelSpinning ) {
		angle = cent->pe.barrelAngle + delta * SPIN_SPEED;
	} else {
		if ( delta > COAST_TIME ) {
			delta = COAST_TIME;
		}

		speed = 0.5 * ( SPIN_SPEED + (float)( COAST_TIME - delta ) / COAST_TIME );
		angle = cent->pe.barrelAngle + delta * speed;
	}

	if ( cent->pe.barrelSpinning == !(cent->currentState.eFlags & EF_FIRING) ) {
		cent->pe.barrelTime = cg.time;
		cent->pe.barrelAngle = AngleMod( angle );
		cent->pe.barrelSpinning = !!(cent->currentState.eFlags & EF_FIRING);
#ifdef MISSIONPACK
		if ( cent->currentState.weapon == WP_CHAINGUN && !cent->pe.barrelSpinning ) {
			trap_S_StartSound( NULL, cent->currentState.number, CHAN_WEAPON, trap_S_RegisterSound( "sound/weapons/vulcan/wvulwind.wav", qfalse ) );
		}
#endif
	}

	return angle;
}


/*
========================
CG_AddWeaponWithPowerups
========================
*/
static void CG_AddWeaponWithPowerups( refEntity_t *gun, int powerups ) {
	// add powerup effects
	if ( WeaponHasPowerup( powerups, PW_INVIS ) ) {
		gun->customShader = cgs.media.invisShader;
		trap_R_AddRefEntityToScene( gun );
		return;
	}

	trap_R_AddRefEntityToScene( gun );
	AddWeaponPowerupShaders( *gun, powerups );
}


/*
=============
CG_AddPlayerWeapon

Used for both the view weapon (ps is valid) and the world modelother character models (ps is NULL)
The main player will have this called for BOTH cases, so effects like light and
sound should only be done on the world model case.
=============
*/
void CG_AddPlayerWeapon( refEntity_t *parent, playerState_t *ps, centity_t *cent, int team ) {
	refEntity_t	gun{};
	refEntity_t	barrel{};
	refEntity_t	flash{};
	vec3_t		angles;
	int		weaponNum;
	weaponInfo_t	*weapon;
	centity_t	*nonPredictedCent;
//	int	col;
	const	clientInfo_t	*ci;

	ci = &cgs.clientinfo[ cent->currentState.clientNum ];
	weaponNum = cent->currentState.weapon;

	CG_RegisterWeapon( weaponNum );
	weapon = &cg_weapons[weaponNum];

	// add the weapon
	gun = WeaponAttachmentEntity( *parent );

	// set custom shading for railgun refire rate
	if ( ps ) {
		ApplyPredictedWeaponColor( gun, *ci, *ps );
	}

	gun.hModel = weapon->weaponModel;
	if (!gun.hModel) {
		return;
	}

	if ( !ps ) {
		// add weapon ready sound
		AddWeaponReadyLoopSound( *cent, *weapon );
	}

	CG_PositionEntityOnTag( &gun, parent, parent->hModel, "tag_weapon");

	CG_AddWeaponWithPowerups( &gun, cent->currentState.powerups );

	// add the spinning barrel
	if ( weapon->barrelModel ) {
		barrel = WeaponAttachmentEntity( *parent );

		barrel.hModel = weapon->barrelModel;
		VectorClear( angles );
		angles[ROLL] = CG_MachinegunSpinAngle( cent );
		AnglesToAxis( angles, barrel.axis );

		CG_PositionRotatedEntityOnTag( &barrel, &gun, weapon->weaponModel, "tag_barrel" );

		CG_AddWeaponWithPowerups( &barrel, cent->currentState.powerups );
	}

	// if the index of the nonPredictedCent is not the same as the clientNum
	// then this is a fake player (like on teh single player podiums), so
	// go ahead and use the cent
	nonPredictedCent = ResolveNonPredictedMuzzleCentity( cent );

	// add the flash
	if ( !ShouldRenderWeaponFlash( *cent, *nonPredictedCent, weaponNum ) ) {
		return;
	}

	flash = WeaponAttachmentEntity( *parent );

	flash.hModel = weapon->flashModel;
	if (!flash.hModel) {
		return;
	}
	VectorClear( angles );
	angles[ROLL] = crandom() * 10;
	AnglesToAxis( angles, flash.axis );

	// colorize the railgun blast
	if ( weaponNum == WP_RAILGUN ) {
		SetWeaponEntityColor( flash, ci->color1 );
	}

	CG_PositionRotatedEntityOnTag( &flash, &gun, weapon->weaponModel, "tag_flash" );
	trap_R_AddRefEntityToScene( &flash );

	// attach muzzle origin to tag_flash for grappling hook
	if ( cg_drawGun.integer || cg.renderingThirdPerson || cent->currentState.number != cg.predictedPlayerState.clientNum ) {
		VectorCopy( flash.origin, nonPredictedCent->pe.muzzleOrigin );
	}

	if ( ps || cg.renderingThirdPerson || cent->currentState.number != cg.predictedPlayerState.clientNum ) {
		int radius;

		// add lightning bolt
		CG_LightningBolt( nonPredictedCent, flash.origin );

		// add rail trail
		CG_SpawnRailTrail( cent, flash.origin );

		// use our own muzzle point as dlight origin 
		// and put it a bit closer to vieworigin to avoid bad normals near walls
		if ( ps && cent->currentState.number == cg.predictedPlayerState.clientNum ) {
			vec3_t	start, end, muzzle, forward, up;
			trace_t	tr;
			AngleVectors( cg.refdefViewAngles, forward, NULL, up );
			VectorMA( cg.refdef.vieworg, 14, forward, muzzle );
			if ( weaponNum == WP_LIGHTNING )
				VectorMA( muzzle, -8, up, muzzle );
			else
				VectorMA( muzzle, -6, up, muzzle );
			VectorMA( cg.refdef.vieworg, 14, forward, start );
			VectorMA( cg.refdef.vieworg, 28, forward, end );
			CG_Trace( &tr, start, NULL, NULL, end, cent->currentState.number, MASK_SHOT | CONTENTS_TRANSLUCENT );
			if ( tr.fraction != 1.0 ) {
				VectorMA( muzzle, -13.0 * ( 1.0 - tr.fraction ), forward, flash.origin );
			} else {
				VectorCopy( muzzle, flash.origin );
			}
		}

		if ( weaponNum == WP_MACHINEGUN ) // make it a bit less annoying
			radius = MG_FLASH_RADIUS + (rand() & WEAPON_FLASH_RADIUS_MOD);
		else
			radius = WEAPON_FLASH_RADIUS + (rand() & WEAPON_FLASH_RADIUS_MOD);

		if ( weapon->flashDlightColor[0] || weapon->flashDlightColor[1] || weapon->flashDlightColor[2] ) {
			trap_R_AddLightToScene( flash.origin, radius, 
				weapon->flashDlightColor[0], weapon->flashDlightColor[1], weapon->flashDlightColor[2] );
		}
	}
}


/*
==============
CG_AddViewWeapon

Add the weapon, and flash for the player's view
==============
*/
void CG_AddViewWeapon( playerState_t *ps ) {
	const weaponInfo_t *weapon;
	refEntity_t hand{};
	centity_t *cent;
	vec3_t fovOffset;
	vec3_t		angles;

	if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}

	if ( ps->pm_type == PM_INTERMISSION ) {
		return;
	}

	// no gun if in third person view or a camera is active
	//if ( cg.renderingThirdPerson || cg.cameraMode) {
	if ( cg.renderingThirdPerson ) {
		return;
	}


	// allow the gun to be completely removed
	if ( !cg_drawGun.integer ) {
		vec3_t origin;

		if ( ( cg.predictedPlayerState.eFlags & EF_FIRING )
			|| ps->weapon == WP_GRAPPLING_HOOK ) {
			// special hack for lightning gun and grappling hook...
			VectorCopy( cg.refdef.vieworg, origin );
			VectorMA( origin, -8, cg.refdef.viewaxis[2], origin );
			VectorCopy( origin, cg_entities[ps->clientNum].pe.muzzleOrigin );
			CG_LightningBolt( &cg_entities[ps->clientNum], origin );
		}
		return;
	}

	// don't draw if testing a gun model
	if ( cg.testGun ) {
		return;
	}

	ComputeViewWeaponFovOffset( fovOffset );

	cent = &cg.predictedPlayerEntity;	// &cg_entities[cg.snap->ps.clientNum];
	CG_RegisterWeapon( ps->weapon );
	weapon = &cg_weapons[ ps->weapon ];

	// set up gun position
	CG_CalculateWeaponPosition( hand.origin, angles );
	ApplyViewWeaponPositionOffset( hand, fovOffset );

	AnglesToAxis( angles, hand.axis );

	// map torso animations to weapon animations
	if ( cg_gun_frame.integer ) {
		// development tool
		hand.frame = hand.oldframe = cg_gun_frame.integer;
		hand.backlerp = 0;
	} else {
		// get clientinfo for animation map
		const clientInfo_t &clientInfo = cgs.clientinfo[ cent->currentState.clientNum ];
		hand.frame = CG_MapTorsoToWeaponFrame( &clientInfo, cent->pe.torso.frame );
		hand.oldframe = CG_MapTorsoToWeaponFrame( &clientInfo, cent->pe.torso.oldFrame );
		hand.backlerp = cent->pe.torso.backlerp;
	}

	hand.hModel = weapon->handsModel;
	hand.renderfx = RF_DEPTHHACK | RF_FIRST_PERSON | RF_MINLIGHT;

	// add everything onto the hand
	CG_AddPlayerWeapon( &hand, ps, &cg.predictedPlayerEntity, ps->persistant[PERS_TEAM] );
}

/*
==============================================================================

WEAPON SELECTION

==============================================================================
*/


/*
===================
CG_DrawWeaponSelect
===================
*/
void CG_DrawWeaponSelect( void ) {
	int x;
	int y;
	const char *name;
	float	*color;

	// don't display if dead
	if ( cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 || cg_drawWeaponSelect.integer == 0 ) {
		return;
	}

	color = WeaponSelectColor();
	if ( !color ) {
		return;
	}
	trap_R_SetColor( color );

	const int weaponSelect = abs( cg_drawWeaponSelect.integer );

	// showing weapon select clears pickup item display, but not the blend blob
	cg.itemPickupTime = 0;

	// count the number of weapons owned
	const int weaponBits = cg.snap->ps.stats[ STAT_WEAPONS ];
	const WeaponSelectLayout layout = ResolveWeaponSelectLayout( weaponSelect, CountOwnedWeapons( weaponBits ) );
	x = layout.x;
	y = layout.y;

	for ( int weaponNum = WP_GAUNTLET; weaponNum < MAX_WEAPONS; ++weaponNum ) {
		if ( !HasWeaponBit( weaponBits, weaponNum ) ) {
			continue;
		}

		CG_RegisterWeapon( weaponNum );

		// draw weapon icon
		CG_DrawPic( x, y, 32, 32, cg_weapons[weaponNum].weaponIcon );

		// draw selection marker
		if ( weaponNum == cg.weaponSelect ) {
			CG_DrawPic( x-4, y-4, 32+8, 32+8, cgs.media.selectShader );
		}

		// no ammo cross on top
		if ( !cg.snap->ps.ammo[ weaponNum ] ) {
			CG_DrawPic( x, y, 32, 32, cgs.media.noammoShader );
		} else {
			DrawWeaponSelectAmmoCount( x, y, weaponSelect, cg.snap->ps.ammo[weaponNum], color );
		}

		x += layout.dx;
		y += layout.dy;
	}

	// draw the selected name
	if ( cg_weapons[ cg.weaponSelect ].item && weaponSelect == 1 ) {
		name = cg_weapons[ cg.weaponSelect ].item->pickup_name;
		if ( name ) {
			CG_DrawString( 320, y - 22, name, color, BIGCHAR_WIDTH, BIGCHAR_HEIGHT, 0, DS_SHADOW | DS_PROPORTIONAL | DS_CENTER | DS_FORCE_COLOR );
		}
	}

	trap_R_SetColor( nullptr );
}


/*
===============
CG_WeaponSelectable
===============
*/
static qboolean CG_WeaponSelectable( int i ) {
	if ( !cg.snap->ps.ammo[i] ) {
		return qfalse;
	}
	if ( ! (cg.snap->ps.stats[ STAT_WEAPONS ] & ( 1 << i ) ) ) {
		return qfalse;
	}

	return qtrue;
}


/*
===============
CG_NextWeapon_f
===============
*/
void CG_NextWeapon_f( void ) {
	int		i;
	int		original;

	if ( !cg.snap ) {
		return;
	}

	cg.weaponSelectTime = cg.time;

	if ( cg.snap->ps.pm_flags & PMF_FOLLOW || cg.demoPlayback ) {
		return;
	}

	original = cg.weaponSelect;

	for ( i = 0 ; i < MAX_WEAPONS ; i++ ) {
		cg.weaponSelect++;
		if ( cg.weaponSelect == MAX_WEAPONS ) {
			cg.weaponSelect = 0;
		}
		if ( cg.weaponSelect == WP_GAUNTLET ) {
			continue;		// never cycle to gauntlet
		}
		if ( CG_WeaponSelectable( cg.weaponSelect ) ) {
			break;
		}
	}
	if ( i == MAX_WEAPONS ) {
		cg.weaponSelect = original;
	}
}


/*
===============
CG_PrevWeapon_f
===============
*/
void CG_PrevWeapon_f( void ) {
	int		i;
	int		original;

	if ( !cg.snap ) {
		return;
	}

	cg.weaponSelectTime = cg.time;

	if ( cg.snap->ps.pm_flags & PMF_FOLLOW || cg.demoPlayback ) {
		return;
	}

	original = cg.weaponSelect;

	for ( i = 0 ; i < MAX_WEAPONS ; i++ ) {
		cg.weaponSelect--;
		if ( cg.weaponSelect == -1 ) {
			cg.weaponSelect = MAX_WEAPONS - 1;
		}
		if ( cg.weaponSelect == WP_GAUNTLET ) {
			continue;		// never cycle to gauntlet
		}
		if ( CG_WeaponSelectable( cg.weaponSelect ) ) {
			break;
		}
	}
	if ( i == MAX_WEAPONS ) {
		cg.weaponSelect = original;
	}
}


/*
===============
CG_Weapon_f
===============
*/
void CG_Weapon_f( void ) {
	int		num;

	if ( !cg.snap ) {
		return;
	}

	cg.weaponSelectTime = cg.time;

	if ( cg.snap->ps.pm_flags & PMF_FOLLOW || cg.demoPlayback ) {
		return;
	}

	num = atoi( CG_Argv( 1 ) );

	if ( num < 1 || num > MAX_WEAPONS-1 ) {
		return;
	}

	if ( ! ( cg.snap->ps.stats[STAT_WEAPONS] & ( 1 << num ) ) ) {
		return;		// don't have the weapon
	}

	cg.weaponSelect = num;
}


/*
===================
CG_OutOfAmmoChange

The current weapon has just run out of ammo
===================
*/
void CG_OutOfAmmoChange( void ) {
	int		i;

	cg.weaponSelectTime = cg.time;

	if ( cg.snap->ps.pm_flags & PMF_FOLLOW || cg.demoPlayback ) {
		return;
	}

	for ( i = MAX_WEAPONS-1 ; i > 0 ; i-- ) {
		if ( CG_WeaponSelectable( i ) ) {
			cg.weaponSelect = i;
			break;
		}
	}
}


/*
===================================================================================================

WEAPON EVENTS

===================================================================================================
*/

/*
================
CG_FireWeapon

Caused by an EV_FIRE_WEAPON event
================
*/
void CG_FireWeapon( centity_t *cent ) {
	const entityState_t &ent = cent->currentState;
	if ( ent.weapon == WP_NONE ) {
		return;
	}
	if ( ent.weapon >= WP_NUM_WEAPONS ) {
		CG_Error( "CG_FireWeapon: ent->weapon >= WP_NUM_WEAPONS" );
		return;
	}
	weaponInfo_t &weap = cg_weapons[ ent.weapon ];

	if ( ent.number >= 0 && ent.number < MAX_CLIENTS && cent != &cg.predictedPlayerEntity ) {
		// point from external event to client entity
		cent = &cg_entities[ ent.number ];
	}

	// mark the entity as muzzle flashing, so when it is added it will
	// append the flash to the weapon model
	cent->muzzleFlashTime = cg.time;

	// lightning gun only does this this on initial press
	if ( ent.weapon == WP_LIGHTNING ) {
		if ( cent->pe.lightningFiring ) {
			return;
		}
	}

	// play quad sound if needed
	if ( cent->currentState.powerups & ( 1 << PW_QUAD ) ) {
		trap_S_StartSound( nullptr, cent->currentState.number, CHAN_ITEM, cgs.media.quadSound );
	}

	// play a sound
	if ( const sfxHandle_t flashSound = ChooseWeaponFlashSound( weap ) ) {
		trap_S_StartSound( nullptr, ent.number, CHAN_WEAPON, flashSound );
	}

	// do brass ejection
	if ( weap.ejectBrassFunc && cg_brassTime.integer > 0 ) {
		weap.ejectBrassFunc( cent );
	}
}


/*
=================
CG_MissileHitWall

Caused by an EV_MISSILE_MISS event, or directly by local bullet tracing
=================
*/
void CG_MissileHitWall( int weapon, int clientNum, vec3_t origin, vec3_t dir, impactSound_t soundType ) {
	MissileImpactSpec spec{};

	switch ( weapon ) {
	default:
#ifdef MISSIONPACK
	case WP_NAILGUN:
		spec.sound = NailgunImpactSound( soundType );
		spec.mark = cgs.media.holeMarkShader;
		spec.radius = 12.0f;
		break;
#endif
	case WP_LIGHTNING:
		// no explosion at LG impact, it is added with the beam
		spec.sound = RandomLightningImpactSound();
		spec.mark = cgs.media.holeMarkShader;
		spec.radius = 12.0f;
		break;
#ifdef MISSIONPACK
	case WP_PROX_LAUNCHER:
		spec.model = cgs.media.dishFlashModel;
		spec.shader = cgs.media.grenadeExplosionShader;
		spec.sound = cgs.media.sfx_proxexp;
		spec.mark = cgs.media.burnMarkShader;
		spec.radius = 64.0f;
		spec.light = 300.0f;
		spec.isSprite = true;
		break;
#endif
	case WP_GRENADE_LAUNCHER:
		spec.model = cgs.media.dishFlashModel;
		spec.shader = cgs.media.grenadeExplosionShader;
		spec.sound = cgs.media.sfx_rockexp;
		spec.mark = cgs.media.burnMarkShader;
		spec.radius = 64.0f;
		spec.light = GL_EXPLOSION_RADIUS;
		spec.isSprite = true;
		break;
	case WP_ROCKET_LAUNCHER:
		spec.model = cgs.media.dishFlashModel;
		spec.shader = cgs.media.rocketExplosionShader;
		spec.sound = cgs.media.sfx_rockexp;
		spec.mark = cgs.media.burnMarkShader;
		spec.radius = 64.0f;
		spec.light = RL_EXPLOSION_RADIUS;
		spec.lightColor = { 1.0f, 0.75f, 0.0f };
		spec.isSprite = true;
		spec.duration = 1000;
		if ( cg_oldRocket.integer == 0 ) {
			SpawnRocketImpactParticles( origin, dir );
		}
		break;
	case WP_RAILGUN:
		spec.model = cgs.media.ringFlashModel;
		spec.shader = cgs.media.railExplosionShader;
		spec.sound = cgs.media.sfx_plasmaexp;
		spec.mark = cgs.media.energyMarkShader;
		spec.radius = 24.0f;
		break;
	case WP_PLASMAGUN:
		spec.model = cgs.media.ringFlashModel;
		spec.shader = cgs.media.plasmaExplosionShader;
		spec.sound = cgs.media.sfx_plasmaexp;
		spec.mark = cgs.media.energyMarkShader;
		spec.radius = 16.0f;
		break;
	case WP_BFG:
		spec.model = cgs.media.dishFlashModel;
		spec.shader = cgs.media.bfgExplosionShader;
		spec.sound = cgs.media.sfx_rockexp;
		spec.mark = cgs.media.burnMarkShader;
		spec.radius = 32.0f;
		spec.light = BFG_EXPLOSION_RADIUS;
		spec.lightColor = { 0.2f, 1.0f, 0.2f };
		spec.isSprite = true;
		break;
	case WP_SHOTGUN:
		spec.model = cgs.media.bulletFlashModel;
		spec.shader = cgs.media.bulletExplosionShader;
		spec.mark = cgs.media.bulletMarkShader;
		spec.radius = 4.0f;
		break;

#ifdef MISSIONPACK
	case WP_CHAINGUN:
		spec.model = cgs.media.bulletFlashModel;
		spec.sound = RandomRicochetSound();
		spec.mark = cgs.media.bulletMarkShader;
		spec.radius = 8.0f;
		break;
#endif

	case WP_MACHINEGUN:
		spec.model = cgs.media.bulletFlashModel;
		spec.shader = cgs.media.bulletExplosionShader;
		spec.mark = cgs.media.bulletMarkShader;
		spec.sound = RandomRicochetSound();
		spec.radius = 8.0f;
		break;
	}

	AddMissileImpactSound( origin, spec.sound );
	AddMissileImpactExplosion( spec, weapon, clientNum, origin, dir );
	AddMissileImpactMark( spec, weapon, clientNum, origin, dir );
}


/*
=================
CG_MissileHitPlayer
=================
*/
void CG_MissileHitPlayer( int weapon, vec3_t origin, vec3_t dir, int entityNum ) {
	CG_Bleed( origin, entityNum );

	// some weapons will make an explosion with the blood, while
	// others will just make the blood
	if ( WeaponCreatesFleshExplosion( weapon ) ) {
		CG_MissileHitWall( weapon, 0, origin, dir, IMPACTSOUND_FLESH );
	}
}



/*
============================================================================

SHOTGUN TRACING

============================================================================
*/

/*
================
CG_ShotgunPellet
================
*/
static void CG_ShotgunPellet( vec3_t start, vec3_t end, int skipNum ) {
	trace_t trace = TraceShotgunPellet( start, end, skipNum );
	AddProjectileWaterTrailEffects( start, end, trace.endpos );

	if ( trace.surfaceFlags & SURF_NOIMPACT ) {
		return;
	}

	if ( cg_entities[trace.entityNum].currentState.eType == ET_PLAYER ) {
		CG_MissileHitPlayer( WP_SHOTGUN, trace.endpos, trace.plane.normal, trace.entityNum );
		return;
	}

	CG_MissileHitWall( WP_SHOTGUN, 0, trace.endpos, trace.plane.normal, ImpactSoundForSurfaceFlags( trace.surfaceFlags ) );
}

/*
================
CG_ShotgunPattern

Perform the same traces the server did to locate the
hit splashes
================
*/
static void CG_ShotgunPattern( vec3_t origin, vec3_t origin2, int seed, int otherEntNum ) {
	vec3_t forward;
	vec3_t right;
	vec3_t up;

	// derive the right and up vectors from the forward vector, because
	// the client won't have any other information
	VectorNormalize2( origin2, forward );
	PerpendicularVector( right, forward );
	CrossProduct( forward, right, up );

	// generate the "random" spread pattern
	for ( int pelletIndex = 0; pelletIndex < DEFAULT_SHOTGUN_COUNT; ++pelletIndex ) {
		const float spreadRight = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
		const float spreadUp = Q_crandom( &seed ) * DEFAULT_SHOTGUN_SPREAD * 16;

		vec3_t end;
		VectorMA( origin, kShotgunTraceDistance, forward, end );
		VectorMA( end, spreadRight, right, end );
		VectorMA( end, spreadUp, up, end );

		CG_ShotgunPellet( origin, end, otherEntNum );
	}
}

/*
==============
CG_ShotgunFire
==============
*/
void CG_ShotgunFire( entityState_t *es ) {
	vec3_t smokeOrigin;

	VectorSubtract( es->origin2, es->pos.trBase, smokeOrigin );
	VectorNormalize( smokeOrigin );
	VectorScale( smokeOrigin, 32, smokeOrigin );
	VectorAdd( es->pos.trBase, smokeOrigin, smokeOrigin );
	AddShotgunSmoke( *es, smokeOrigin );
	CG_ShotgunPattern( es->pos.trBase, es->origin2, es->eventParm, es->otherEntityNum );
}

/*
============================================================================

BULLETS

============================================================================
*/


/*
===============
CG_Tracer
===============
*/
void CG_Tracer( vec3_t source, vec3_t dest ) {
	vec3_t forward;
	vec3_t right;
	std::array<polyVert_t, 4> vertices{};
	vec3_t line;
	vec3_t start;
	vec3_t finish;
	vec3_t midpoint;

	// tracer
	VectorSubtract( dest, source, forward );
	const float length = VectorNormalize( forward );

	// start at least a little ways from the muzzle
	if ( length < 100 ) {
		return;
	}
	const float begin = 50 + random() * ( length - 60 );
	float endDistance = begin + cg_tracerLength.value;
	if ( endDistance > length ) {
		endDistance = length;
	}
	VectorMA( source, begin, forward, start );
	VectorMA( source, endDistance, forward, finish );

	line[0] = DotProduct( forward, cg.refdef.viewaxis[1] );
	line[1] = DotProduct( forward, cg.refdef.viewaxis[2] );

	VectorScale( cg.refdef.viewaxis[1], line[1], right );
	VectorMA( right, -line[0], cg.refdef.viewaxis[2], right );
	VectorNormalize( right );

	vec3_t vertexPosition;
	VectorMA( finish, cg_tracerWidth.value, right, vertexPosition );
	SetTracerVertex( vertices[0], vertexPosition, 0, 1 );

	VectorMA( finish, -cg_tracerWidth.value, right, vertexPosition );
	SetTracerVertex( vertices[1], vertexPosition, 1, 0 );

	VectorMA( start, -cg_tracerWidth.value, right, vertexPosition );
	SetTracerVertex( vertices[2], vertexPosition, 1, 1 );

	VectorMA( start, cg_tracerWidth.value, right, vertexPosition );
	SetTracerVertex( vertices[3], vertexPosition, 0, 0 );

	trap_R_AddPolyToScene( cgs.media.tracerShader, static_cast<int>( vertices.size() ), vertices.data() );

	midpoint[0] = ( start[0] + finish[0] ) * 0.5;
	midpoint[1] = ( start[1] + finish[1] ) * 0.5;
	midpoint[2] = ( start[2] + finish[2] ) * 0.5;

	// add the tracer sound
	trap_S_StartSound( midpoint, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.tracerSound );

}


/*
======================
CG_CalcMuzzlePoint
======================
*/
static qboolean	CG_CalcMuzzlePoint( int entityNum, vec3_t muzzle ) {
	vec3_t forward;

	if ( entityNum == cg.snap->ps.clientNum ) {
		VectorCopy( cg.snap->ps.origin, muzzle );
		muzzle[2] += cg.snap->ps.viewheight;
		AngleVectors( cg.snap->ps.viewangles, forward, nullptr, nullptr );
		VectorMA( muzzle, 14, forward, muzzle );
		return qtrue;
	}

	const centity_t &cent = cg_entities[entityNum];
	if ( !cent.currentValid ) {
		return qfalse;
	}

	VectorCopy( cent.currentState.pos.trBase, muzzle );

	AngleVectors( cent.currentState.apos.trBase, forward, nullptr, nullptr );
	const int legsAnimation = cent.currentState.legsAnim & ~ANIM_TOGGLEBIT;
	muzzle[2] += IsCrouchedLegsAnimation( legsAnimation ) ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT;

	VectorMA( muzzle, 14, forward, muzzle );

	return qtrue;

}

/*
======================
CG_Bullet

Renders bullet effects.
======================
*/
void CG_Bullet( vec3_t end, int sourceEntityNum, vec3_t normal, qboolean flesh, int fleshEntityNum ) {
	vec3_t start;

	// if the shooter is currently valid, calc a source point and possibly
	// do trail effects
	if ( sourceEntityNum >= 0 && cg_tracerChance.value > 0 ) {
		if ( CG_CalcMuzzlePoint( sourceEntityNum, start ) ) {
			AddProjectileWaterTrailEffects( start, end, end );

			// draw a tracer
			if ( random() < cg_tracerChance.value ) {
				CG_Tracer( start, end );
			}
		}
	}

	// impact splash and mark
	if ( flesh ) {
		CG_Bleed( end, fleshEntityNum );
	} else {
		CG_MissileHitWall( WP_MACHINEGUN, 0, end, normal, IMPACTSOUND_DEFAULT );
	}

}
