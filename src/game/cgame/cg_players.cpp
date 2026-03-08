// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_players.c -- handle the media and animation for player entities
#include "cg_local.h"

#include <algorithm>
#include <array>
#include <span>

#define	PM_SKIN "pm"

static const char *cg_customSoundNames[ MAX_CUSTOM_SOUNDS ] = {
	"*death1.wav",
	"*death2.wav",
	"*death3.wav",
	"*jump1.wav",
	"*pain25_1.wav",
	"*pain50_1.wav",
	"*pain75_1.wav",
	"*pain100_1.wav",
	"*falling1.wav",
	"*gasp.wav",
	"*drown.wav",
	"*fall1.wav",
	"*taunt.wav"
};

static void CG_TrailItem( const centity_t *cent, qhandle_t hModel );
static void CG_PlayerFlag( centity_t *cent, qhandle_t hSkin, refEntity_t *torso );

namespace {

enum class AnimationDirectiveParseResult {
	handled,
	unhandled,
	incomplete,
};

struct FlagPowerupVisual {
	qhandle_t flapSkin;
	qhandle_t trailModel;
	float lightRed;
	float lightGreen;
	float lightBlue;
};

struct SpriteFlagVisual {
	int flag;
	qhandle_t shader;
};

[[nodiscard]] const char *DefaultTeamName( const team_t team ) {
	return team == TEAM_BLUE ? DEFAULT_BLUETEAM_NAME : DEFAULT_REDTEAM_NAME;
}

[[nodiscard]] auto CustomSoundNameEntries() noexcept -> std::span<const char *const> {
	const auto soundNames = std::span{ cg_customSoundNames };
	const auto endIt = std::find( soundNames.begin(), soundNames.end(), nullptr );
	return { soundNames.data(), static_cast<std::size_t>( std::distance( soundNames.begin(), endIt ) ) };
}

[[nodiscard]] auto ActiveClientInfoEntries() noexcept -> std::span<const clientInfo_t> {
	return { cgs.clientinfo, static_cast<std::size_t>( cgs.maxclients ) };
}

[[nodiscard]] auto MutableClientInfoEntries() noexcept -> std::span<clientInfo_t> {
	return { cgs.clientinfo, static_cast<std::size_t>( cgs.maxclients ) };
}

[[nodiscard]] auto GameEntityEntries() noexcept -> std::span<centity_t> {
	return cg_entities;
}

void AddPowerupLight( const vec3_t origin, const float red, const float green, const float blue ) {
	trap_R_AddLightToScene( origin, ( POWERUP_GLOW_RADIUS + ( rand() & POWERUP_GLOW_RADIUS_MOD ) ), red, green, blue );
}

[[nodiscard]] team_t ClientTeam( const centity_t &cent ) {
	return cgs.clientinfo[cent.currentState.clientNum].team;
}

void AddFlagPowerupVisual( centity_t *cent, refEntity_t *torso, const FlagPowerupVisual &visual ) {
	const clientInfo_t &clientInfo = cgs.clientinfo[cent->currentState.clientNum];
	if ( clientInfo.newAnims ) {
		CG_PlayerFlag( cent, visual.flapSkin, torso );
	} else {
		CG_TrailItem( cent, visual.trailModel );
	}

	AddPowerupLight( cent->lerpOrigin, visual.lightRed, visual.lightGreen, visual.lightBlue );
}

[[nodiscard]] qhandle_t PlayerStatusSpriteShader( const int eFlags ) {
	const auto spriteVisuals = std::to_array<SpriteFlagVisual>( {
		{ EF_CONNECTION, cgs.media.connectionShader },
		{ EF_TALK, cgs.media.balloonShader },
		{ EF_AWARD_IMPRESSIVE, cgs.media.medalImpressive },
		{ EF_AWARD_EXCELLENT, cgs.media.medalExcellent },
		{ EF_AWARD_GAUNTLET, cgs.media.medalGauntlet },
		{ EF_AWARD_DEFEND, cgs.media.medalDefend },
		{ EF_AWARD_ASSIST, cgs.media.medalAssist },
		{ EF_AWARD_CAP, cgs.media.medalCapture },
	} );

	for ( const SpriteFlagVisual &visual : spriteVisuals ) {
		if ( eFlags & visual.flag ) {
			return visual.shader;
		}
	}

	return 0;
}

[[nodiscard]] bool HasPowerup( const int powerups, const powerup_t powerup ) {
	return ( powerups & ( 1 << powerup ) ) != 0;
}

[[nodiscard]] qhandle_t QuadPowerupShader( const int team ) {
	return team == TEAM_RED ? cgs.media.redQuadShader : cgs.media.quadShader;
}

void AddPowerupShaderEntity( refEntity_t *entity, const qhandle_t shader ) {
	entity->customShader = shader;
	trap_R_AddRefEntityToScene( entity );
}

void SetSplashVertex( polyVert_t &vertex, const vec3_t origin, const float xOffset, const float yOffset, const float s, const float t ) {
	VectorCopy( origin, vertex.xyz );
	vertex.xyz[0] += xOffset;
	vertex.xyz[1] += yOffset;
	vertex.st[0] = s;
	vertex.st[1] = t;
	vertex.modulate[0] = 255;
	vertex.modulate[1] = 255;
	vertex.modulate[2] = 255;
	vertex.modulate[3] = 255;
}

[[nodiscard]] char *NextAnimationToken( char **cursor ) {
	return COM_Parse( cursor );
}

[[nodiscard]] bool IsAnimationFrameToken( const char *token ) {
	return token[0] >= '0' && token[0] <= '9';
}

void ResetAnimationDefaults( clientInfo_t &clientInfo ) {
	clientInfo.footsteps = FOOTSTEP_NORMAL;
	VectorClear( clientInfo.headOffset );
	clientInfo.gender = GENDER_MALE;
	clientInfo.fixedlegs = qfalse;
	clientInfo.fixedtorso = qfalse;
}

void ApplyFootstepDirective( clientInfo_t &clientInfo, const char *token, const char *filename ) {
	if ( !Q_stricmp( token, "default" ) || !Q_stricmp( token, "normal" ) ) {
		clientInfo.footsteps = FOOTSTEP_NORMAL;
	} else if ( !Q_stricmp( token, "boot" ) ) {
		clientInfo.footsteps = FOOTSTEP_BOOT;
	} else if ( !Q_stricmp( token, "flesh" ) ) {
		clientInfo.footsteps = FOOTSTEP_FLESH;
	} else if ( !Q_stricmp( token, "mech" ) ) {
		clientInfo.footsteps = FOOTSTEP_MECH;
	} else if ( !Q_stricmp( token, "energy" ) ) {
		clientInfo.footsteps = FOOTSTEP_ENERGY;
	} else {
		CG_Printf( "Bad footsteps parm in %s: %s\n", filename, token );
	}
}

void ApplyGenderDirective( clientInfo_t &clientInfo, const char *token ) {
	if ( token[0] == 'f' || token[0] == 'F' ) {
		clientInfo.gender = GENDER_FEMALE;
	} else if ( token[0] == 'n' || token[0] == 'N' ) {
		clientInfo.gender = GENDER_NEUTER;
	} else {
		clientInfo.gender = GENDER_MALE;
	}
}

[[nodiscard]] AnimationDirectiveParseResult TryParseOptionalAnimationDirective( const char *token, char **cursor, clientInfo_t &clientInfo, const char *filename ) {
	if ( !Q_stricmp( token, "footsteps" ) ) {
		const char *directiveValue = NextAnimationToken( cursor );
		if ( !directiveValue[0] ) {
			return AnimationDirectiveParseResult::incomplete;
		}
		ApplyFootstepDirective( clientInfo, directiveValue, filename );
		return AnimationDirectiveParseResult::handled;
	}

	if ( !Q_stricmp( token, "headoffset" ) ) {
		for ( float &offset : clientInfo.headOffset ) {
			const char *directiveValue = NextAnimationToken( cursor );
			if ( !directiveValue[0] ) {
				return AnimationDirectiveParseResult::incomplete;
			}
			offset = static_cast<float>( atof( directiveValue ) );
		}
		return AnimationDirectiveParseResult::handled;
	}

	if ( !Q_stricmp( token, "sex" ) ) {
		const char *directiveValue = NextAnimationToken( cursor );
		if ( !directiveValue[0] ) {
			return AnimationDirectiveParseResult::incomplete;
		}
		ApplyGenderDirective( clientInfo, directiveValue );
		return AnimationDirectiveParseResult::handled;
	}

	if ( !Q_stricmp( token, "fixedlegs" ) ) {
		clientInfo.fixedlegs = qtrue;
		return AnimationDirectiveParseResult::handled;
	}

	if ( !Q_stricmp( token, "fixedtorso" ) ) {
		clientInfo.fixedtorso = qtrue;
		return AnimationDirectiveParseResult::handled;
	}

	return AnimationDirectiveParseResult::unhandled;
}

[[nodiscard]] bool TryReadAnimationInt( char **cursor, int &value ) {
	const char *token = NextAnimationToken( cursor );
	if ( !token[0] ) {
		return false;
	}

	value = atoi( token );
	return true;
}

[[nodiscard]] bool TryReadAnimationFloat( char **cursor, float &value ) {
	const char *token = NextAnimationToken( cursor );
	if ( !token[0] ) {
		return false;
	}

	value = static_cast<float>( atof( token ) );
	return true;
}

void ApplyAnimationTiming( animation_t &animation, float fps ) {
	if ( fps == 0.0f ) {
		fps = 1.0f;
	}

	const int lerp = static_cast<int>( 1000 / fps );
	animation.frameLerp = lerp;
	animation.initialLerp = lerp;
}

void CopyGestureAnimation( animation_t &animation, const animation_t &gestureAnimation ) {
	animation = gestureAnimation;
	animation.reversed = qfalse;
	animation.flipflop = qfalse;
}

void ConfigureFlagAnimation( animation_t &animation, const int firstFrame, const int numFrames, const int loopFrames, const float fps, const qboolean reversed ) {
	animation.firstFrame = firstFrame;
	animation.numFrames = numFrames;
	animation.loopFrames = loopFrames;
	ApplyAnimationTiming( animation, fps );
	animation.reversed = reversed;
}

void CopyModelAndSkin( const char *source, const char *defaultSkin, char *modelName, const int modelNameSize, char *skinName, const int skinNameSize ) {
	Q_strncpyz( modelName, source, modelNameSize );

	char *slash = strchr( modelName, '/' );
	if ( slash == nullptr ) {
		Q_strncpyz( skinName, defaultSkin, skinNameSize );
		return;
	}

	Q_strncpyz( skinName, slash + 1, skinNameSize );
	*slash = '\0';
}

[[nodiscard]] bool HasText( const char *text ) noexcept {
	return text != nullptr && text[0] != '\0';
}

[[nodiscard]] const char *ModelLookupTeamName( const clientInfo_t &clientInfo ) {
	if ( clientInfo.coloredSkin && !Q_stricmp( clientInfo.skinName, PM_SKIN ) ) {
		return PM_SKIN;
	}

	if ( cgs.gametype < GT_TEAM ) {
		return "default";
	}

	return clientInfo.team == TEAM_BLUE ? "blue" : "red";
}

[[nodiscard]] const char *HeadLookupTeamName( const clientInfo_t &clientInfo ) {
	if ( clientInfo.coloredSkin && !Q_stricmp( clientInfo.headSkinName, PM_SKIN ) ) {
		return PM_SKIN;
	}

	if ( cgs.gametype < GT_TEAM ) {
		return "default";
	}

	switch ( clientInfo.team ) {
		case TEAM_RED:
			return "red";
		case TEAM_BLUE:
			return "blue";
		default:
			return "default";
	}
}

[[nodiscard]] bool IsKnownPlayerModel( const char *modelName ) {
	const auto knownModels = std::to_array<const char *>( {
		"anarki", "biker", "bitterman", "bones", "crash", "doom", "grunt", "hunter",
		"keel", "klesk", "lucy", "major", "mynx", "orbb", "ranger", "razor",
		"sarge", "slash", "sorlag", "tankjr", "uriel", "visor", "xaero",
	} );

	return std::any_of( knownModels.begin(), knownModels.end(), [modelName]( const char *knownModel ) {
		return !Q_stricmp( modelName, knownModel );
	} );
}

void CopyPmModelSelection( const char *source, char *modelName, const int modelNameSize, char *skinName, const int skinNameSize ) {
	Q_strncpyz( modelName, source, modelNameSize );

	if ( char *skin = strchr( modelName, '/' ); skin != nullptr ) {
		*skin = '\0';
	}

	if ( !IsKnownPlayerModel( modelName ) ) {
		Q_strncpyz( modelName, "sarge", modelNameSize );
	}

	Q_strncpyz( skinName, PM_SKIN, skinNameSize );
}

[[nodiscard]] bool MatchesLoadedClientInfo( const clientInfo_t &lhs, const clientInfo_t &rhs ) {
	return !rhs.deferred
		&& rhs.infoValid
		&& !Q_stricmp( lhs.modelName, rhs.modelName )
		&& !Q_stricmp( lhs.skinName, rhs.skinName )
		&& !Q_stricmp( lhs.headModelName, rhs.headModelName )
		&& !Q_stricmp( lhs.headSkinName, rhs.headSkinName )
		&& ( cgs.gametype < GT_TEAM || lhs.team == rhs.team );
}

[[nodiscard]] bool MatchesLoadedClientModel( const clientInfo_t &lhs, const clientInfo_t &rhs ) {
	return !rhs.deferred
		&& rhs.infoValid
		&& !Q_stricmp( lhs.skinName, rhs.skinName )
		&& !Q_stricmp( lhs.modelName, rhs.modelName )
		&& ( cgs.gametype < GT_TEAM || lhs.team == rhs.team );
}

[[nodiscard]] bool MatchesLoadedClientSkin( const clientInfo_t &lhs, const clientInfo_t &rhs ) {
	return !rhs.deferred
		&& rhs.infoValid
		&& !Q_stricmp( lhs.skinName, rhs.skinName )
		&& ( cgs.gametype < GT_TEAM || lhs.team == rhs.team );
}

[[nodiscard]] const clientInfo_t *FindLoadedClientInfo( const clientInfo_t &clientInfo ) {
	const auto clientInfos = ActiveClientInfoEntries();
	const auto matchIt = std::find_if( clientInfos.begin(), clientInfos.end(), [&clientInfo]( const clientInfo_t &match ) {
		return MatchesLoadedClientInfo( clientInfo, match );
	} );

	return matchIt != clientInfos.end() ? &*matchIt : nullptr;
}

[[nodiscard]] const clientInfo_t *FindLoadedClientWithSameModel( const clientInfo_t &clientInfo ) {
	const auto clientInfos = ActiveClientInfoEntries();
	const auto matchIt = std::find_if( clientInfos.begin(), clientInfos.end(), [&clientInfo]( const clientInfo_t &match ) {
		return MatchesLoadedClientModel( clientInfo, match );
	} );

	return matchIt != clientInfos.end() ? &*matchIt : nullptr;
}

[[nodiscard]] const clientInfo_t *FindLoadedClientWithSameSkin( const clientInfo_t &clientInfo ) {
	const auto clientInfos = ActiveClientInfoEntries();
	const auto matchIt = std::find_if( clientInfos.begin(), clientInfos.end(), [&clientInfo]( const clientInfo_t &match ) {
		return MatchesLoadedClientSkin( clientInfo, match );
	} );

	return matchIt != clientInfos.end() ? &*matchIt : nullptr;
}

[[nodiscard]] const clientInfo_t *FindFirstValidClientInfo() {
	const auto clientInfos = ActiveClientInfoEntries();
	const auto matchIt = std::find_if( clientInfos.begin(), clientInfos.end(), []( const clientInfo_t &match ) {
		return match.infoValid;
	} );

	return matchIt != clientInfos.end() ? &*matchIt : nullptr;
}

#ifdef MISSIONPACK
[[nodiscard]] refEntity_t PowerupEntityFromTorso( const refEntity_t &torso ) noexcept {
	refEntity_t powerup = torso;
	powerup.frame = 0;
	powerup.oldframe = 0;
	powerup.customSkin = 0;
	return powerup;
}

[[nodiscard]] skulltrail_t &PlayerSkullTrail( centity_t &cent ) noexcept {
	return cg.skulltrails[cent.currentState.number];
}

[[nodiscard]] int ClampedSkullTokenCount( const centity_t &cent ) noexcept {
	return std::clamp( cent.currentState.generic1, 0, MAX_SKULLTRAIL );
}

void PushSkullTrailPosition( skulltrail_t &trail, const vec3_t position ) {
	for ( int positionIndex = trail.numpositions; positionIndex > 0; --positionIndex ) {
		VectorCopy( trail.positions[positionIndex - 1], trail.positions[positionIndex] );
	}
	VectorCopy( position, trail.positions[0] );
}

void ExtendSkullTrail( skulltrail_t &trail, const vec3_t position, const int tokenCount ) {
	while ( trail.numpositions < tokenCount ) {
		PushSkullTrailPosition( trail, position );
		++trail.numpositions;
	}
}

void AdvanceSkullTrail( skulltrail_t &trail, const vec3_t position ) {
	vec3_t origin;
	VectorCopy( position, origin );

	for ( int trailIndex = 0; trailIndex < trail.numpositions; ++trailIndex ) {
		vec3_t direction;
		VectorSubtract( trail.positions[trailIndex], origin, direction );
		if ( VectorNormalize( direction ) > 30.0f ) {
			VectorMA( origin, 30.0f, direction, trail.positions[trailIndex] );
		}
		VectorCopy( trail.positions[trailIndex], origin );
	}
}

[[nodiscard]] qhandle_t PlayerTokenModel( const centity_t &cent ) {
	return ClientTeam( cent ) == TEAM_BLUE ? cgs.media.redCubeModel : cgs.media.blueCubeModel;
}

void PreparePlayerTokenEntity( refEntity_t &entity, const vec3_t currentOrigin, const vec3_t trailPosition, const int trailIndex ) {
	VectorSubtract( currentOrigin, trailPosition, entity.axis[0] );
	entity.axis[0][2] = 0;
	VectorNormalize( entity.axis[0] );
	VectorSet( entity.axis[2], 0, 0, 1 );
	CrossProduct( entity.axis[0], entity.axis[2], entity.axis[1] );

	VectorCopy( trailPosition, entity.origin );
	const float angle = ((( cg.time + 500 * MAX_SKULLTRAIL - 500 * trailIndex ) / 16 ) & 255 ) * ( M_PI * 2 ) / 255;
	entity.origin[2] += sin( angle ) * 10;
}

void AddAttachedPowerupModel( const refEntity_t &torso, const qhandle_t model ) {
	refEntity_t powerup = PowerupEntityFromTorso( torso );
	powerup.hModel = model;
	trap_R_AddRefEntityToScene( &powerup );
}

void UpdateInvulnerabilityTimers( clientInfo_t &clientInfo, const bool invulnerable ) noexcept {
	if ( invulnerable ) {
		if ( !clientInfo.invulnerabilityStartTime ) {
			clientInfo.invulnerabilityStartTime = cg.time;
		}
		clientInfo.invulnerabilityStopTime = cg.time;
	} else {
		clientInfo.invulnerabilityStartTime = 0;
	}
}

[[nodiscard]] bool ShouldRenderInvulnerabilityPowerup( const bool invulnerable, const clientInfo_t &clientInfo ) noexcept {
	return invulnerable || cg.time - clientInfo.invulnerabilityStopTime < 250;
}

[[nodiscard]] float InvulnerabilityPowerupScale( const clientInfo_t &clientInfo ) noexcept {
	if ( cg.time - clientInfo.invulnerabilityStartTime < 250 ) {
		return static_cast<float>( cg.time - clientInfo.invulnerabilityStartTime ) / 250.0f;
	}
	if ( cg.time - clientInfo.invulnerabilityStopTime < 250 ) {
		return static_cast<float>( 250 - ( cg.time - clientInfo.invulnerabilityStopTime ) ) / 250.0f;
	}
	return 1.0f;
}

void AddInvulnerabilityPowerup( const centity_t &cent, const refEntity_t &torso, const clientInfo_t &clientInfo ) {
	refEntity_t powerup = PowerupEntityFromTorso( torso );
	powerup.hModel = cgs.media.invulnerabilityPowerupModel;
	powerup.renderfx &= ~RF_THIRD_PERSON;
	VectorCopy( cent.lerpOrigin, powerup.origin );

	const float scale = InvulnerabilityPowerupScale( clientInfo );
	VectorSet( powerup.axis[0], scale, 0, 0 );
	VectorSet( powerup.axis[1], 0, scale, 0 );
	VectorSet( powerup.axis[2], 0, 0, scale );
	trap_R_AddRefEntityToScene( &powerup );
}

[[nodiscard]] int MedkitUsageElapsed( const clientInfo_t &clientInfo ) noexcept {
	return cg.time - clientInfo.medkitUsageTime;
}

[[nodiscard]] bool IsMedkitUsageActive( const clientInfo_t &clientInfo ) noexcept {
	return clientInfo.medkitUsageTime != 0 && MedkitUsageElapsed( clientInfo ) < 500;
}

void AddMedkitUsagePowerup( const centity_t &cent, const refEntity_t &torso, const int elapsed ) {
	refEntity_t powerup = PowerupEntityFromTorso( torso );
	vec3_t angles;

	powerup.hModel = cgs.media.medkitUsageModel;
	powerup.renderfx &= ~RF_THIRD_PERSON;
	VectorClear( angles );
	AnglesToAxis( angles, powerup.axis );
	VectorCopy( cent.lerpOrigin, powerup.origin );
	powerup.origin[2] += -24.0f + static_cast<float>( elapsed ) * 80.0f / 500.0f;

	if ( elapsed > 400 ) {
		const float fadeAmount = static_cast<float>( elapsed - 1000 ) * 0xff / 100.0f;
		powerup.shaderRGBA[0] = 0xff - fadeAmount;
		powerup.shaderRGBA[1] = 0xff - fadeAmount;
		powerup.shaderRGBA[2] = 0xff - fadeAmount;
		powerup.shaderRGBA[3] = 0xff - fadeAmount;
	} else {
		powerup.shaderRGBA[0] = 0xff;
		powerup.shaderRGBA[1] = 0xff;
		powerup.shaderRGBA[2] = 0xff;
		powerup.shaderRGBA[3] = 0xff;
	}

	trap_R_AddRefEntityToScene( &powerup );
}

constexpr float kKamikazeOrbitRadius = 20.0f;
constexpr float kKamikazeDeadBobBaseHeight = 15.0f;
constexpr float kKamikazeDeadBobAmplitude = 8.0f;
constexpr float kKamikazePitchAmplitude = 30.0f;
constexpr float kFullCircleRadians = static_cast<float>( M_PI * 2.0 );
constexpr float kHalfPiRadians = static_cast<float>( M_PI * 0.5 );
constexpr float kRadiansToDegrees = 180.0f / static_cast<float>( M_PI );

[[nodiscard]] float WrappedKamikazeAngle( const int divisor, const float offsetRadians = 0.0f ) noexcept {
	float angle = static_cast<float>( (( cg.time / divisor ) & 255 ) * kFullCircleRadians / 255.0f ) + offsetRadians;
	if ( angle > kFullCircleRadians ) {
		angle -= kFullCircleRadians;
	}
	return angle;
}

[[nodiscard]] refEntity_t KamikazeSkullEntity( const centity_t &cent, const float shadowPlane, const int renderfx ) noexcept {
	refEntity_t skull{};
	VectorCopy( cent.lerpOrigin, skull.lightingOrigin );
	skull.shadowPlane = shadowPlane;
	skull.renderfx = renderfx;
	return skull;
}

void SetEntityAxisFromDirection( refEntity_t &entity, const vec3_t direction ) {
	VectorCopy( direction, entity.axis[1] );
	VectorNormalize( entity.axis[1] );
	VectorSet( entity.axis[2], 0, 0, 1 );
	CrossProduct( entity.axis[1], entity.axis[2], entity.axis[0] );
}

void SetEntityAxisFromAngles( refEntity_t &entity, const float pitch, const float yaw ) {
	vec3_t angles;
	angles[0] = pitch;
	angles[1] = yaw > 360.0f ? yaw - 360.0f : yaw;
	angles[2] = 0.0f;
	AnglesToAxis( angles, entity.axis );
}

void AddKamikazeSkullHeadAndTrail( refEntity_t skull, const bool invertTrailAxis = false ) {
	skull.hModel = cgs.media.kamikazeHeadModel;
	trap_R_AddRefEntityToScene( &skull );

	if ( invertTrailAxis ) {
		VectorInverse( skull.axis[1] );
	}

	skull.hModel = cgs.media.kamikazeHeadTrail;
	trap_R_AddRefEntityToScene( &skull );
}

void AddDeadKamikazeSkull( const centity_t &cent, const refEntity_t &torso, const float shadowPlane, const int renderfx ) {
	refEntity_t skull = KamikazeSkullEntity( cent, shadowPlane, renderfx );
	vec3_t direction;

	const float horizontalAngle = WrappedKamikazeAngle( 7 );
	direction[0] = sin( horizontalAngle ) * kKamikazeOrbitRadius;
	direction[1] = cos( horizontalAngle ) * kKamikazeOrbitRadius;

	const float verticalAngle = WrappedKamikazeAngle( 4 );
	direction[2] = kKamikazeDeadBobBaseHeight + sin( verticalAngle ) * kKamikazeDeadBobAmplitude;
	VectorAdd( torso.origin, direction, skull.origin );

	direction[2] = 0.0f;
	SetEntityAxisFromDirection( skull, direction );
	AddKamikazeSkullHeadAndTrail( skull );
}

void AddOrbitalKamikazeSkull( const centity_t &cent, const refEntity_t &torso, const float shadowPlane, const int renderfx,
	const float xOffset, const float yOffset, const float zOffset, const float pitch, const float yaw,
	const bool invertTrailAxis = false ) {
	refEntity_t skull = KamikazeSkullEntity( cent, shadowPlane, renderfx );
	vec3_t direction;

	direction[0] = xOffset;
	direction[1] = yOffset;
	direction[2] = zOffset;
	VectorAdd( torso.origin, direction, skull.origin );

	SetEntityAxisFromAngles( skull, pitch, yaw );
	AddKamikazeSkullHeadAndTrail( skull, invertTrailAxis );
}

void AddDirectionalKamikazeSkull( const centity_t &cent, const refEntity_t &torso, const float shadowPlane, const int renderfx,
	const float xOffset, const float yOffset, const float zOffset ) {
	refEntity_t skull = KamikazeSkullEntity( cent, shadowPlane, renderfx );
	vec3_t direction;

	direction[0] = xOffset;
	direction[1] = yOffset;
	direction[2] = zOffset;
	VectorAdd( torso.origin, direction, skull.origin );

	SetEntityAxisFromDirection( skull, direction );
	AddKamikazeSkullHeadAndTrail( skull );
}

void AddKamikazeOrbitSkulls( const centity_t &cent, const refEntity_t &torso, const float shadowPlane, const int renderfx ) {
	const float firstAngle = WrappedKamikazeAngle( 4 );
	AddOrbitalKamikazeSkull(
		cent,
		torso,
		shadowPlane,
		renderfx,
		cos( firstAngle ) * kKamikazeOrbitRadius,
		sin( firstAngle ) * kKamikazeOrbitRadius,
		cos( firstAngle ) * kKamikazeOrbitRadius,
		sin( firstAngle ) * kKamikazePitchAmplitude,
		firstAngle * kRadiansToDegrees + 90.0f,
		true
	);

	const float secondAngle = WrappedKamikazeAngle( 4, kFullCircleRadians / 2.0f );
	AddOrbitalKamikazeSkull(
		cent,
		torso,
		shadowPlane,
		renderfx,
		sin( secondAngle ) * kKamikazeOrbitRadius,
		cos( secondAngle ) * kKamikazeOrbitRadius,
		cos( secondAngle ) * kKamikazeOrbitRadius,
		cos( secondAngle - kHalfPiRadians ) * kKamikazePitchAmplitude,
		360.0f - secondAngle * kRadiansToDegrees
	);

	const float thirdAngle = WrappedKamikazeAngle( 3, kHalfPiRadians );
	AddDirectionalKamikazeSkull(
		cent,
		torso,
		shadowPlane,
		renderfx,
		sin( thirdAngle ) * kKamikazeOrbitRadius,
		cos( thirdAngle ) * kKamikazeOrbitRadius,
		0.0f
	);
}

void AddKamikazeEffect( const centity_t &cent, const refEntity_t &torso, const float shadowPlane, const int renderfx ) {
	if ( cent.currentState.eFlags & EF_DEAD ) {
		AddDeadKamikazeSkull( cent, torso, shadowPlane, renderfx );
		return;
	}

	AddKamikazeOrbitSkulls( cent, torso, shadowPlane, renderfx );
}

struct AttachedPowerupVisual {
	powerup_t powerup;
	qhandle_t model;
};

void AddMissionpackAttachedPowerups( const refEntity_t &torso, const int powerups ) {
	const auto visuals = std::to_array<AttachedPowerupVisual>( {
		{ PW_GUARD, cgs.media.guardPowerupModel },
		{ PW_SCOUT, cgs.media.scoutPowerupModel },
		{ PW_DOUBLER, cgs.media.doublerPowerupModel },
		{ PW_AMMOREGEN, cgs.media.ammoRegenPowerupModel },
	} );

	for ( const AttachedPowerupVisual &visual : visuals ) {
		if ( HasPowerup( powerups, visual.powerup ) ) {
			AddAttachedPowerupModel( torso, visual.model );
		}
	}
}

void AddMissionpackTorsoEffects( const centity_t &cent, const refEntity_t &torso, clientInfo_t &clientInfo,
	const float shadowPlane, const int renderfx ) {
	if ( cent.currentState.eFlags & EF_KAMIKAZE ) {
		AddKamikazeEffect( cent, torso, shadowPlane, renderfx );
	}

	const int powerups = cent.currentState.powerups;
	AddMissionpackAttachedPowerups( torso, powerups );

	const bool invulnerable = HasPowerup( powerups, PW_INVULNERABILITY );
	UpdateInvulnerabilityTimers( clientInfo, invulnerable );
	if ( ShouldRenderInvulnerabilityPowerup( invulnerable, clientInfo ) ) {
		AddInvulnerabilityPowerup( cent, torso, clientInfo );
	}

	if ( IsMedkitUsageActive( clientInfo ) ) {
		AddMedkitUsagePowerup( cent, torso, MedkitUsageElapsed( clientInfo ) );
	}
}

void AddMissionpackHeadEffects( centity_t &cent, const refEntity_t &head ) {
	CG_BreathPuffs( &cent, &head );
	CG_DustTrail( &cent );
}
#endif

constexpr byte kDarkenedPlayerShaderChannel = 85;
constexpr byte kOpaqueShaderAlpha = 255;
constexpr float kMaxVertexLightChannel = 255.0f;

[[nodiscard]] bool ShouldRenderRegenShader() noexcept {
	return ( ( cg.time / 100 ) % 10 ) == 1;
}

void AddStandardEntityPowerupShaders( refEntity_t &entity, const int powerups, const int team ) {
	if ( HasPowerup( powerups, PW_QUAD ) ) {
		AddPowerupShaderEntity( &entity, QuadPowerupShader( team ) );
	}
	if ( HasPowerup( powerups, PW_REGEN ) && ShouldRenderRegenShader() ) {
		AddPowerupShaderEntity( &entity, cgs.media.regenShader );
	}
	if ( HasPowerup( powerups, PW_BATTLESUIT ) ) {
		AddPowerupShaderEntity( &entity, cgs.media.battleSuitShader );
	}
}

[[nodiscard]] byte ClampedVertexLightChannel( const float value ) noexcept {
	return static_cast<byte>( std::clamp( value, 0.0f, kMaxVertexLightChannel ) );
}

void SetVertexAmbientLighting( polyVert_t &vertex, const vec3_t ambientLight ) noexcept {
	vertex.modulate[0] = ClampedVertexLightChannel( ambientLight[0] );
	vertex.modulate[1] = ClampedVertexLightChannel( ambientLight[1] );
	vertex.modulate[2] = ClampedVertexLightChannel( ambientLight[2] );
	vertex.modulate[3] = kOpaqueShaderAlpha;
}

void SetVertexDirectedLighting( polyVert_t &vertex, const vec3_t ambientLight, const vec3_t directedLight, const float incoming ) noexcept {
	vertex.modulate[0] = ClampedVertexLightChannel( ambientLight[0] + incoming * directedLight[0] );
	vertex.modulate[1] = ClampedVertexLightChannel( ambientLight[1] + incoming * directedLight[1] );
	vertex.modulate[2] = ClampedVertexLightChannel( ambientLight[2] + incoming * directedLight[2] );
	vertex.modulate[3] = kOpaqueShaderAlpha;
}

[[nodiscard]] bool ShouldSkipLocalPlayerModel( const centity_t &cent ) noexcept {
	return cent.currentState.number == cg.snap->ps.clientNum && cg.renderingThirdPerson && cg_cameraMode.integer;
}

[[nodiscard]] int BasePlayerRenderfx( const centity_t &cent ) noexcept {
	return cent.currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson ? RF_THIRD_PERSON : 0;
}

void ApplySharedPlayerRenderfx( int &renderfx, const bool hasShadow ) noexcept {
	if ( cg_shadows.integer == 3 && hasShadow ) {
		renderfx |= RF_SHADOW_PLANE;
	}
	renderfx |= RF_LIGHTING_ORIGIN;
}

void ApplyPlayerPartColor( refEntity_t &entity, const vec3_t color, const bool darken ) {
	if ( darken ) {
		entity.shaderRGBA[0] = kDarkenedPlayerShaderChannel;
		entity.shaderRGBA[1] = kDarkenedPlayerShaderChannel;
		entity.shaderRGBA[2] = kDarkenedPlayerShaderChannel;
	} else {
		entity.shaderRGBA[0] = static_cast<byte>( color[0] * 255.0f );
		entity.shaderRGBA[1] = static_cast<byte>( color[1] * 255.0f );
		entity.shaderRGBA[2] = static_cast<byte>( color[2] * 255.0f );
	}
	entity.shaderRGBA[3] = kOpaqueShaderAlpha;
}

} // namespace


/*
================
CG_CustomSound

================
*/
sfxHandle_t	CG_CustomSound( int clientNum, const char *soundName ) {
	if ( soundName[0] != '*' ) {
		return trap_S_RegisterSound( soundName, qfalse );
	}

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		clientNum = 0;
	}
	clientInfo_t *ci = &cgs.clientinfo[ clientNum ];

	const auto soundNames = CustomSoundNameEntries();
	for ( std::size_t soundIndex = 0; soundIndex < soundNames.size(); ++soundIndex ) {
		if ( !strcmp( soundName, soundNames[soundIndex] ) ) {
			return ci->sounds[soundIndex];
		}
	}

	CG_Error( "Unknown custom sound: %s", soundName );
	return 0;
}



/*
=============================================================================

CLIENT INFO

=============================================================================
*/

/*
======================
CG_ParseAnimationFile

Read a configuration file containing animation counts and rates
models/players/visor/animation.cfg, etc
======================
*/
static qboolean	CG_ParseAnimationFile( const char *filename, clientInfo_t *ci ) {
	char		*text_p;
	int			len;
	int			i;
	float		fps;
	int			skip;
	std::array<char, 20000> text{};
	fileHandle_t	f;
	animation_t *animations;

	animations = ci->animations;

	// load the file
	len = trap_FS_FOpenFile( filename, &f, FS_READ );
	if ( f == FS_INVALID_HANDLE ) {
		return qfalse;
	}
	if ( len <= 0 ) {
		trap_FS_FCloseFile( f );
		return qfalse;
	}
	if ( len >= static_cast<int>( text.size() ) - 1 ) {
		CG_Printf( "File %s too long\n", filename );
		return qfalse;
	}
	trap_FS_Read( text.data(), len, f );
	text[len] = '\0';
	trap_FS_FCloseFile( f );

	// parse the text
	text_p = text.data();
	skip = 0;	// quite the compiler warning

	ResetAnimationDefaults( *ci );

	// read optional parameters
	while ( 1 ) {
		char *previousCursor = text_p;	// so we can unget
		const char *token = NextAnimationToken( &text_p );
		if ( !token[0] ) {
			break;
		}

		const AnimationDirectiveParseResult directiveResult = TryParseOptionalAnimationDirective( token, &text_p, *ci, filename );
		if ( directiveResult == AnimationDirectiveParseResult::handled ) {
			continue;
		}
		if ( directiveResult == AnimationDirectiveParseResult::incomplete ) {
			break;
		}

		// if it is a number, start parsing animations
		if ( IsAnimationFrameToken( token ) ) {
			text_p = previousCursor;	// unget the token
			break;
		}
		Com_Printf( "unknown token '%s' in %s\n", token, filename );
	}

	// read information for each frame
	for ( i = 0 ; i < MAX_ANIMATIONS ; i++ ) {
		const char *token = NextAnimationToken( &text_p );
		if ( !token[0] ) {
			if( i >= TORSO_GETFLAG && i <= TORSO_NEGATIVE ) {
				CopyGestureAnimation( animations[i], animations[TORSO_GESTURE] );
				continue;
			}
			break;
		}
		animations[i].firstFrame = atoi( token );
		// leg only frames are adjusted to not count the upper body only frames
		if ( i == LEGS_WALKCR ) {
			skip = animations[LEGS_WALKCR].firstFrame - animations[TORSO_GESTURE].firstFrame;
		}
		if ( i >= LEGS_WALKCR && i<TORSO_GETFLAG) {
			animations[i].firstFrame -= skip;
		}

		if ( !TryReadAnimationInt( &text_p, animations[i].numFrames ) ) {
			break;
		}

		animations[i].reversed = qfalse;
		animations[i].flipflop = qfalse;
		// if numFrames is negative the animation is reversed
		if (animations[i].numFrames < 0) {
			animations[i].numFrames = -animations[i].numFrames;
			animations[i].reversed = qtrue;
		}

		if ( !TryReadAnimationInt( &text_p, animations[i].loopFrames ) ) {
			break;
		}

		if ( !TryReadAnimationFloat( &text_p, fps ) ) {
			break;
		}
		ApplyAnimationTiming( animations[i], fps );
	}

	if ( i != MAX_ANIMATIONS ) {
		CG_Printf( "Error parsing animation file: %s\n", filename );
		return qfalse;
	}

	// crouch backward animation
	animations[LEGS_BACKCR] = animations[LEGS_WALKCR];
	animations[LEGS_BACKCR].reversed = qtrue;
	// walk backward animation
	animations[LEGS_BACKWALK] = animations[LEGS_WALK];
	animations[LEGS_BACKWALK].reversed = qtrue;
	// flag moving fast
	ConfigureFlagAnimation( animations[FLAG_RUN], 0, 16, 16, 15.0f, qfalse );
	// flag not moving or moving slowly
	ConfigureFlagAnimation( animations[FLAG_STAND], 16, 5, 0, 20.0f, qfalse );
	// flag speeding up
	ConfigureFlagAnimation( animations[FLAG_STAND2RUN], 16, 5, 1, 15.0f, qtrue );
	//
	// new anims changes
	//
//	animations[TORSO_GETFLAG].flipflop = qtrue;
//	animations[TORSO_GUARDBASE].flipflop = qtrue;
//	animations[TORSO_PATROL].flipflop = qtrue;
//	animations[TORSO_AFFIRMATIVE].flipflop = qtrue;
//	animations[TORSO_NEGATIVE].flipflop = qtrue;
	//
	return qtrue;
}


/*
==========================
CG_FileExists
==========================
*/
static qboolean	CG_FileExists( const char *filename ) {
	int len;
	fileHandle_t	f;

	len = trap_FS_FOpenFile( filename, &f, FS_READ );

	if ( f != FS_INVALID_HANDLE ) {
		trap_FS_FCloseFile( f );
	}

	if ( len > 0 ) {
		return qtrue;
	}

	return qfalse;
}


/*
==========================
CG_FindClientModelFile
==========================
*/
static qboolean	CG_FindClientModelFile( char *filename, int length, clientInfo_t *ci, const char *teamName, const char *modelName, const char *skinName, const char *base, const char *ext ) {
	const char *team = ModelLookupTeamName( *ci );
	const bool hasTeamName = HasText( teamName );
	const auto charactersFolders = std::to_array<const char *>( { "", "characters/" } );
	const auto tryCandidate = [&]( const char *format, auto... args ) -> qboolean {
		Com_sprintf( filename, length, format, args... );
		return CG_FileExists( filename );
	};

	for ( const char *charactersFolder : charactersFolders ) {
		if ( hasTeamName ) {
			if ( tryCandidate( "models/players/%s%s/%s%s_%s_%s.%s", charactersFolder, modelName, teamName, base, skinName, team, ext ) ) {
				return qtrue;
			}

			if ( cgs.gametype >= GT_TEAM ) {
				if ( tryCandidate( "models/players/%s%s/%s%s_%s.%s", charactersFolder, modelName, teamName, base, team, ext ) ) {
					return qtrue;
				}
			} else if ( tryCandidate( "models/players/%s%s/%s%s_%s.%s", charactersFolder, modelName, teamName, base, skinName, ext ) ) {
				return qtrue;
			}
		}

		if ( tryCandidate( "models/players/%s%s/%s_%s_%s.%s", charactersFolder, modelName, base, skinName, team, ext ) ) {
			return qtrue;
		}

		if ( cgs.gametype >= GT_TEAM ) {
			if ( tryCandidate( "models/players/%s%s/%s_%s.%s", charactersFolder, modelName, base, team, ext ) ) {
				return qtrue;
			}
		} else if ( tryCandidate( "models/players/%s%s/%s_%s.%s", charactersFolder, modelName, base, skinName, ext ) ) {
			return qtrue;
		}
	}

	return qfalse;
}


/*
==========================
CG_FindClientHeadFile
==========================
*/
static qboolean	CG_FindClientHeadFile( char *filename, int length, clientInfo_t *ci, const char *teamName, const char *headModelName, const char *headSkinName, const char *base, const char *ext ) {
	const char *team = HeadLookupTeamName( *ci );
	const bool hasTeamName = HasText( teamName );
	const bool headsOnly = headModelName[0] == '*';
	const char *resolvedHeadModelName = headsOnly ? headModelName + 1 : headModelName;
	const auto headsFolders = std::to_array<const char *>( { "", "heads/" } );
	const auto tryCandidate = [&]( const char *format, auto... args ) -> qboolean {
		Com_sprintf( filename, length, format, args... );
		return CG_FileExists( filename );
	};

	for ( const char *headsFolder : headsFolders ) {
		if ( headsOnly && headsFolder[0] == '\0' ) {
			continue;
		}

		if ( hasTeamName ) {
			if ( tryCandidate( "models/players/%s%s/%s/%s%s_%s.%s", headsFolder, resolvedHeadModelName, headSkinName, teamName, base, team, ext ) ) {
				return qtrue;
			}

			if ( cgs.gametype >= GT_TEAM ) {
				if ( tryCandidate( "models/players/%s%s/%s%s_%s.%s", headsFolder, resolvedHeadModelName, teamName, base, team, ext ) ) {
					return qtrue;
				}
			} else if ( tryCandidate( "models/players/%s%s/%s%s_%s.%s", headsFolder, resolvedHeadModelName, teamName, base, headSkinName, ext ) ) {
				return qtrue;
			}
		}

		if ( tryCandidate( "models/players/%s%s/%s/%s_%s.%s", headsFolder, resolvedHeadModelName, headSkinName, base, team, ext ) ) {
			return qtrue;
		}

		if ( cgs.gametype >= GT_TEAM ) {
			if ( tryCandidate( "models/players/%s%s/%s_%s.%s", headsFolder, resolvedHeadModelName, base, team, ext ) ) {
				return qtrue;
			}
		} else if ( tryCandidate( "models/players/%s%s/%s_%s.%s", headsFolder, resolvedHeadModelName, base, headSkinName, ext ) ) {
			return qtrue;
		}
	}

	return qfalse;
}


/*
==========================
CG_RegisterClientSkin
==========================
*/
static qboolean	CG_RegisterClientSkin( clientInfo_t *ci, const char *teamName, const char *modelName, const char *skinName, const char *headModelName, const char *headSkinName ) {
	std::array<char, MAX_QPATH> filename{};

	/*
	Com_sprintf( filename, sizeof( filename ), "models/players/%s/%slower_%s.skin", modelName, teamName, skinName );
	ci->legsSkin = trap_R_RegisterSkin( filename );
	if (!ci->legsSkin) {
		Com_sprintf( filename, sizeof( filename ), "models/players/characters/%s/%slower_%s.skin", modelName, teamName, skinName );
		ci->legsSkin = trap_R_RegisterSkin( filename );
		if (!ci->legsSkin) {
			Com_Printf( "Leg skin load failure: %s\n", filename );
		}
	}


	Com_sprintf( filename, sizeof( filename ), "models/players/%s/%supper_%s.skin", modelName, teamName, skinName );
	ci->torsoSkin = trap_R_RegisterSkin( filename );
	if (!ci->torsoSkin) {
		Com_sprintf( filename, sizeof( filename ), "models/players/characters/%s/%supper_%s.skin", modelName, teamName, skinName );
		ci->torsoSkin = trap_R_RegisterSkin( filename );
		if (!ci->torsoSkin) {
			Com_Printf( "Torso skin load failure: %s\n", filename );
		}
	}
	*/
	if ( CG_FindClientModelFile( filename.data(), filename.size(), ci, teamName, modelName, skinName, "lower", "skin" ) ) {
		ci->legsSkin = trap_R_RegisterSkin( filename.data() );
	}
	if (!ci->legsSkin) {
		Com_Printf( "Leg skin load failure: %s\n", filename.data() );
	}

	if ( CG_FindClientModelFile( filename.data(), filename.size(), ci, teamName, modelName, skinName, "upper", "skin" ) ) {
		ci->torsoSkin = trap_R_RegisterSkin( filename.data() );
	}
	if (!ci->torsoSkin) {
		Com_Printf( "Torso skin load failure: %s\n", filename.data() );
	}

	if ( CG_FindClientHeadFile( filename.data(), filename.size(), ci, teamName, headModelName, headSkinName, "head", "skin" ) ) {
		ci->headSkin = trap_R_RegisterSkin( filename.data() );
	}
	if (!ci->headSkin) {
		Com_Printf( "Head skin load failure: %s\n", filename.data() );
	}

	// if any skins failed to load
	if ( !ci->legsSkin || !ci->torsoSkin || !ci->headSkin ) {
		return qfalse;
	}
	return qtrue;
}

namespace {

[[nodiscard]] const char *ResolvedClientHeadName( const char *headModelName, const char *modelName ) {
	return headModelName[0] == '\0' ? modelName : headModelName;
}

[[nodiscard]] qhandle_t RegisterClientBodyModel( std::array<char, MAX_QPATH> &filename, const char *modelName, const char *partName ) {
	Com_sprintf( filename.data(), filename.size(), "models/players/%s/%s.md3", modelName, partName );
	if ( const qhandle_t model = trap_R_RegisterModel( filename.data() ); model != 0 ) {
		return model;
	}

	Com_sprintf( filename.data(), filename.size(), "models/players/characters/%s/%s.md3", modelName, partName );
	return trap_R_RegisterModel( filename.data() );
}

[[nodiscard]] qhandle_t RegisterClientHeadModel( std::array<char, MAX_QPATH> &filename, const char *headName, const char *headModelName ) {
	if ( headName[0] == '*' ) {
		Com_sprintf( filename.data(), filename.size(), "models/players/heads/%s/%s.md3", &headModelName[1], &headModelName[1] );
	} else {
		Com_sprintf( filename.data(), filename.size(), "models/players/%s/head.md3", headName );
	}

	if ( qhandle_t model = trap_R_RegisterModel( filename.data() ); model != 0 ) {
		return model;
	}

	if ( headName[0] == '*' ) {
		return 0;
	}

	Com_sprintf( filename.data(), filename.size(), "models/players/heads/%s/%s.md3", headModelName, headModelName );
	return trap_R_RegisterModel( filename.data() );
}

[[nodiscard]] bool TryRegisterClientSkins( clientInfo_t *ci, const char *teamName, const char *modelName, const char *skinName, const char *headName, const char *headSkinName ) {
	if ( CG_RegisterClientSkin( ci, teamName, modelName, skinName, headName, headSkinName ) ) {
		return true;
	}

	if ( HasText( teamName ) ) {
		std::array<char, MAX_QPATH> fallbackTeamName{};
		Com_Printf( "Failed to load skin file: %s : %s : %s, %s : %s\n", teamName, modelName, skinName, headName, headSkinName );
		Com_sprintf( fallbackTeamName.data(), fallbackTeamName.size(), "%s/", DefaultTeamName( ci->team ) );
		if ( CG_RegisterClientSkin( ci, fallbackTeamName.data(), modelName, skinName, headName, headSkinName ) ) {
			return true;
		}

		Com_Printf( "Failed to load skin file: %s : %s : %s, %s : %s\n", fallbackTeamName.data(), modelName, skinName, headName, headSkinName );
		return false;
	}

	Com_Printf( "Failed to load skin file: %s : %s, %s : %s\n", modelName, skinName, headName, headSkinName );
	return false;
}

[[nodiscard]] bool TryRegisterClientAnimationConfig( std::array<char, MAX_QPATH> &filename, const char *modelName, clientInfo_t *ci ) {
	Com_sprintf( filename.data(), filename.size(), "models/players/%s/animation.cfg", modelName );
	if ( CG_ParseAnimationFile( filename.data(), ci ) ) {
		return true;
	}

	Com_sprintf( filename.data(), filename.size(), "models/players/characters/%s/animation.cfg", modelName );
	return CG_ParseAnimationFile( filename.data(), ci );
}

[[nodiscard]] qhandle_t RegisterClientModelIcon( std::array<char, MAX_QPATH> &filename, clientInfo_t *ci, const char *teamName, const char *headName, const char *headSkinName ) {
	const auto extensions = std::to_array<const char *>( { "skin", "tga" } );
	for ( const char *extension : extensions ) {
		if ( CG_FindClientHeadFile( filename.data(), filename.size(), ci, teamName, headName, headSkinName, "icon", extension ) ) {
			return trap_R_RegisterShaderNoMip( filename.data() );
		}
	}

	return 0;
}

} // namespace


/*
==========================
CG_RegisterClientModelname
==========================
*/
static qboolean CG_RegisterClientModelname( clientInfo_t *ci, const char *modelName, const char *skinName, const char *headModelName, const char *headSkinName, const char *teamName ) {
	std::array<char, MAX_QPATH> filename{};
	const char *headName = ResolvedClientHeadName( headModelName, modelName );

	ci->legsModel = RegisterClientBodyModel( filename, modelName, "lower" );
	if ( !ci->legsModel ) {
		Com_Printf( "Failed to load model file %s\n", filename.data() );
		return qfalse;
	}

	ci->torsoModel = RegisterClientBodyModel( filename, modelName, "upper" );
	if ( !ci->torsoModel ) {
		Com_Printf( "Failed to load model file %s\n", filename.data() );
		return qfalse;
	}

	ci->headModel = RegisterClientHeadModel( filename, headName, headModelName );
	if ( !ci->headModel ) {
		Com_Printf( "Failed to load model file %s\n", filename.data() );
		return qfalse;
	}

	// if any skins failed to load, return failure
	if ( !TryRegisterClientSkins( ci, teamName, modelName, skinName, headName, headSkinName ) ) {
		return qfalse;
	}

	// load the animations
	if ( !TryRegisterClientAnimationConfig( filename, modelName, ci ) ) {
		Com_Printf( "Failed to load animation file %s\n", filename.data() );
		return qfalse;
	}

	ci->modelIcon = RegisterClientModelIcon( filename, ci, teamName, headName, headSkinName );
	if ( !ci->modelIcon ) {
		return qfalse;
	}

	return qtrue;
}

namespace {

constexpr float kFullColorChannel = 1.0f;

void SetColorToWhite( vec3_t color ) noexcept {
	VectorSet( color, kFullColorChannel, kFullColorChannel, kFullColorChannel );
}

[[nodiscard]] char TeamColorReplacementCharacter( const team_t team ) noexcept {
	switch ( team ) {
		case TEAM_RED:
			return '1';
		case TEAM_BLUE:
			return '4';
		case TEAM_FREE:
			return '7';
		default:
			return '\0';
	}
}

void ResetPlayerLerpState( lerpFrame_t &frame, const float yawAngle, const float pitchAngle ) noexcept {
	frame = lerpFrame_t{};
	frame.yawAngle = yawAngle;
	frame.yawing = qfalse;
	frame.pitchAngle = pitchAngle;
	frame.pitching = qfalse;
}

} // namespace


/*
====================
CG_ColorFromString
====================
*/
static void CG_ColorFromChar( char v, vec3_t color ) {
	const int val = v - '0';

	if ( val < 1 || val > 7 ) {
		SetColorToWhite( color );
	} else {
		VectorClear( color );
		if ( val & 1 ) {
			color[0] = kFullColorChannel;
		}
		if ( val & 2 ) {
			color[1] = kFullColorChannel;
		}
		if ( val & 4 ) {
			color[2] = kFullColorChannel;
		}
	}
}


static void CG_SetColorInfo( const char *color, clientInfo_t *info ) {
	struct ClientColorTarget {
		std::size_t colorIndex;
		vec3_t *target;
	};

	const auto applyColorTarget = [color]( const ClientColorTarget &target ) {
		if ( !color[target.colorIndex] ) {
			return false;
		}
		CG_ColorFromChar( color[target.colorIndex], *target.target );
		return true;
	};

	SetColorToWhite( info->headColor );
	SetColorToWhite( info->bodyColor );
	SetColorToWhite( info->legsColor );

	const auto playerColors = std::to_array<ClientColorTarget>( {
		{ 0, &info->headColor },
		{ 1, &info->bodyColor },
		{ 2, &info->legsColor },
	} );
	for ( const ClientColorTarget &target : playerColors ) {
		if ( !applyColorTarget( target ) ) {
			return;
		}
	}

	const auto overrideColors = std::to_array<ClientColorTarget>( {
		{ 3, &info->color1 },
		{ 4, &info->color2 },
	} );
	for ( const ClientColorTarget &target : overrideColors ) {
		if ( !applyColorTarget( target ) ) {
			return;
		}
	}
}


static const char *CG_GetTeamColors( const char *color, team_t team ) {
	static std::array<char, 6> str{};

	Q_strncpyz( str.data(), color, str.size() );

	if ( const char replacement = TeamColorReplacementCharacter( team ); replacement != '\0' ) {
		replace1( '?', replacement, str.data() );
	}

	return str.data();
}


namespace {

constexpr int DeferredPlayerMemoryFloor = 4'000'000;

struct ClientModelSelectionContext {
	int clientNum;
	int myClientNum;
	team_t myTeam;
	qboolean allowNativeModel;
};

[[nodiscard]] int ConfigStringIntValue( const char *configstring, const char *key ) {
	return atoi( Info_ValueForKey( configstring, key ) );
}

[[nodiscard]] team_t ConfigStringTeamValue( const char *configstring ) {
	int parsedTeam = ConfigStringIntValue( configstring, "t" );
	if ( static_cast<unsigned>( parsedTeam ) >= TEAM_NUM_TEAMS ) {
		parsedTeam = TEAM_SPECTATOR;
	}
	return static_cast<team_t>( parsedTeam );
}

void ResetClientBodyColors( clientInfo_t &clientInfo ) {
	SetColorToWhite( clientInfo.headColor );
	SetColorToWhite( clientInfo.bodyColor );
	SetColorToWhite( clientInfo.legsColor );
}

void ApplyClientBaseColors( const char *configstring, clientInfo_t &clientInfo ) {
	CG_ColorFromChar( Info_ValueForKey( configstring, "c1" )[0], clientInfo.color1 );
	CG_ColorFromChar( Info_ValueForKey( configstring, "c2" )[0], clientInfo.color2 );
	ResetClientBodyColors( clientInfo );
}

[[nodiscard]] ClientModelSelectionContext BuildClientModelSelectionContext( const int clientNum ) {
	ClientModelSelectionContext context{};
	context.clientNum = clientNum;

	if ( cg.snap ) {
		context.myClientNum = cg.snap->ps.clientNum;
		context.myTeam = cgs.clientinfo[context.myClientNum].team;
	} else {
		context.myClientNum = cg.clientNum;
		context.myTeam = TEAM_SPECTATOR;
	}

	if ( context.myTeam == TEAM_SPECTATOR && cg.snap ) {
		context.myTeam = static_cast<team_t>( cg.snap->ps.persistant[PERS_TEAM] );
	}

	context.allowNativeModel = qfalse;
	if ( cgs.gametype < GT_TEAM ) {
		const bool isOwnFreePlayerView = cg.snap == nullptr
			|| ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_FREE && cg.snap->ps.clientNum == clientNum );
		if ( isOwnFreePlayerView && ( cg.demoPlayback || ( cg.snap && cg.snap->ps.pm_flags & PMF_FOLLOW ) ) ) {
			context.allowNativeModel = qtrue;
		}
	}

	return context;
}

[[nodiscard]] bool ShouldApplyConfiguredTeamColors( const ClientModelSelectionContext &context, const team_t team ) {
	return context.allowNativeModel
		|| ( ( team == TEAM_RED || team == TEAM_BLUE )
			&& team == context.myTeam
			&& ( context.clientNum != context.myClientNum || cg.demoPlayback ) );
}

void ApplyConfiguredTeamColors( clientInfo_t &clientInfo, const ClientModelSelectionContext &context ) {
	if ( !cg_teamColors.string[0] || clientInfo.team == TEAM_SPECTATOR ) {
		return;
	}

	if ( !ShouldApplyConfiguredTeamColors( context, clientInfo.team ) ) {
		return;
	}

	const char *colors = CG_GetTeamColors( cg_teamColors.string, clientInfo.team );
	const std::size_t colorCount = strlen( colors );
	if ( colorCount >= 4 ) {
		CG_ColorFromChar( colors[3], clientInfo.color1 );
	}
	if ( colorCount >= 5 ) {
		CG_ColorFromChar( colors[4], clientInfo.color2 );
	}
}

[[nodiscard]] bool CanDeferClientLoad( const ClientModelSelectionContext &context, const team_t team ) {
	return cg_deferPlayers.integer == 2
		|| ( cg_deferPlayers.integer == 1 && context.myTeam != TEAM_SPECTATOR && team == TEAM_SPECTATOR );
}

[[nodiscard]] bool IsLowMemoryForClientLoad() {
	return trap_MemoryRemaining() < DeferredPlayerMemoryFloor;
}

[[nodiscard]] bool VertexLightWasEnabled( const std::array<char, MAX_CVAR_VALUE_STRING> &vertexLightSetting ) {
	return vertexLightSetting[0] != '\0' && vertexLightSetting[0] != '0';
}

[[nodiscard]] std::array<char, MAX_QPATH> BuildClientTeamNamePrefix( const clientInfo_t &clientInfo ) {
	std::array<char, MAX_QPATH> teamName{};

#ifdef MISSIONPACK
	if ( cgs.gametype >= GT_TEAM ) {
		if ( clientInfo.team == TEAM_BLUE ) {
			Q_strncpyz( teamName.data(), cg_blueTeamName.string, teamName.size() );
		} else {
			Q_strncpyz( teamName.data(), cg_redTeamName.string, teamName.size() );
		}
	}
	if ( teamName[0] ) {
		Q_strcat( teamName.data(), teamName.size(), "/" );
	}
#endif

	return teamName;
}

[[nodiscard]] bool RegisterClientAssetsWithFallback( clientInfo_t &clientInfo, std::array<char, MAX_QPATH> &teamName ) {
	if ( CG_RegisterClientModelname( &clientInfo, clientInfo.modelName, clientInfo.skinName, clientInfo.headModelName, clientInfo.headSkinName, teamName.data() ) ) {
		return true;
	}

	if ( cg_buildScript.integer ) {
		CG_Error( "CG_RegisterClientModelname( %s, %s, %s, %s %s ) failed", clientInfo.modelName, clientInfo.skinName, clientInfo.headModelName, clientInfo.headSkinName, teamName.data() );
	}

	if ( cgs.gametype >= GT_TEAM ) {
		Q_strncpyz( teamName.data(), DefaultTeamName( clientInfo.team ), teamName.size() );
		if ( !CG_RegisterClientModelname( &clientInfo, DEFAULT_MODEL, clientInfo.skinName, DEFAULT_MODEL, clientInfo.skinName, teamName.data() ) ) {
			CG_Error( "DEFAULT_TEAM_MODEL / skin (%s/%s) failed to register", DEFAULT_MODEL, clientInfo.skinName );
		}
	} else if ( !CG_RegisterClientModelname( &clientInfo, DEFAULT_MODEL, "default", DEFAULT_MODEL, "default", teamName.data() ) ) {
		CG_Error( "DEFAULT_MODEL (%s) failed to register", DEFAULT_MODEL );
	}

	return false;
}

void UpdateClientAnimationSupport( clientInfo_t &clientInfo ) {
	clientInfo.newAnims = qfalse;
	if ( !clientInfo.torsoModel ) {
		return;
	}

	orientation_t tag{};
	if ( trap_R_LerpTag( &tag, clientInfo.torsoModel, 0, 0, 1, "tag_flag" ) ) {
		clientInfo.newAnims = qtrue;
	}
}

void RegisterClientSounds( clientInfo_t &clientInfo, const bool modelLoaded ) {
	const char *soundDirectory = clientInfo.modelName;
	const auto soundNames = CustomSoundNameEntries();
	for ( std::size_t soundIndex = 0; soundIndex < soundNames.size(); ++soundIndex ) {
		const char *soundName = soundNames[soundIndex];
		clientInfo.sounds[soundIndex] = 0;
		if ( modelLoaded ) {
			clientInfo.sounds[soundIndex] = trap_S_RegisterSound( va( "sound/player/%s/%s", soundDirectory, soundName + 1 ), qfalse );
		}
		if ( !clientInfo.sounds[soundIndex] ) {
			clientInfo.sounds[soundIndex] = trap_S_RegisterSound( va( "sound/player/%s/%s", DEFAULT_MODEL, soundName + 1 ), qfalse );
		}
	}
}

void ResetPlayerEntitiesForClient( const int clientNum ) {
	for ( centity_t &entity : GameEntityEntries() ) {
		if ( entity.currentState.clientNum == clientNum
			&& entity.currentState.eType == ET_PLAYER ) {
			CG_ResetPlayerEntity( &entity );
		}
	}
}

[[nodiscard]] int AnimationNumberWithoutToggle( const int animationNumber ) noexcept {
	return animationNumber & ~ANIM_TOGGLEBIT;
}

[[nodiscard]] int EffectiveAnimationFrameCount( const animation_t &animation ) noexcept {
	return animation.flipflop ? animation.numFrames * 2 : animation.numFrames;
}

[[nodiscard]] int ResolveAnimationFrame( const animation_t &animation, const int frameOffset ) noexcept {
	if ( animation.reversed ) {
		return animation.firstFrame + animation.numFrames - 1 - frameOffset;
	}

	if ( animation.flipflop && frameOffset >= animation.numFrames ) {
		return animation.firstFrame + animation.numFrames - 1 - ( frameOffset % animation.numFrames );
	}

	return animation.firstFrame + frameOffset;
}

[[nodiscard]] float PlayerAnimationSpeedScale( const centity_t &cent ) noexcept {
	return HasPowerup( cent.currentState.powerups, PW_HASTE ) ? 1.5f : 1.0f;
}

[[nodiscard]] int LegsAnimationForCurrentState( const centity_t &cent ) noexcept {
	return cent.pe.legs.yawing && AnimationNumberWithoutToggle( cent.currentState.legsAnim ) == LEGS_IDLE
		? LEGS_TURN
		: cent.currentState.legsAnim;
}

[[nodiscard]] bool IsStandingTorsoAnimation( const int animationNumber ) noexcept {
	const int baseAnimation = AnimationNumberWithoutToggle( animationNumber );
	return baseAnimation == TORSO_STAND || baseAnimation == TORSO_STAND2;
}

[[nodiscard]] bool ShouldCenterPlayerAngles( const entityState_t &state ) noexcept {
	return AnimationNumberWithoutToggle( state.legsAnim ) != LEGS_IDLE
		|| !IsStandingTorsoAnimation( state.torsoAnim );
}

[[nodiscard]] int CurrentMovementDirection( const centity_t &cent ) {
	if ( cent.currentState.eFlags & EF_DEAD ) {
		return 0;
	}

	const int direction = cent.currentState.angles2[YAW];
	if ( direction < 0 || direction > 7 ) {
		CG_Error( "Bad player movement angle" );
	}

	return direction;
}

[[nodiscard]] float SwingScaleForDelta( const float swing, const float swingTolerance ) noexcept {
	const float magnitude = fabs( swing );
	if ( magnitude < swingTolerance * 0.5f ) {
		return 0.5f;
	}
	if ( magnitude < swingTolerance ) {
		return 1.0f;
	}
	return 2.0f;
}

[[nodiscard]] float PainTwitchFactor( const centity_t &cent ) noexcept {
	const int elapsed = cg.time - cent.pe.painTime;
	if ( elapsed >= PAIN_TWITCH_TIME ) {
		return 0.0f;
	}

	return 1.0f - static_cast<float>( elapsed ) / PAIN_TWITCH_TIME;
}

void AdvanceEffectTimestamp( int &nextTime, const int interval ) noexcept {
	nextTime += interval;
	if ( nextTime < cg.time ) {
		nextTime = cg.time;
	}
}

[[nodiscard]] bool IsRunLegAnimation( const int animationNumber ) noexcept {
	return animationNumber == LEGS_RUN || animationNumber == LEGS_BACK;
}

#ifdef MISSIONPACK
[[nodiscard]] bool IsLandingLegAnimation( const int animationNumber ) noexcept {
	return animationNumber == LEGS_LANDB || animationNumber == LEGS_LAND;
}

[[nodiscard]] bool CanEmitBreathPuff( const centity_t &cent, const refEntity_t &head, const clientInfo_t &clientInfo ) {
	if ( !cg_enableBreath.integer ) {
		return false;
	}
	if ( cent.currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		return false;
	}
	if ( cent.currentState.eFlags & EF_DEAD ) {
		return false;
	}
	if ( CG_PointContents( head.origin, 0 ) & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		return false;
	}
	return clientInfo.breathPuffTime <= cg.time;
}

void BuildBreathPuffVectors( const refEntity_t &head, vec3_t up, vec3_t origin ) {
	VectorSet( up, 0, 0, 8 );
	VectorMA( head.origin, 8, head.axis[0], origin );
	VectorMA( origin, -4, head.axis[2], origin );
}
#endif

void ExportLerpFrameState( const lerpFrame_t &lerpFrame, int *oldFrame, int *frame, float *backlerp ) {
	*oldFrame = lerpFrame.oldFrame;
	*frame = lerpFrame.frame;
	*backlerp = lerpFrame.backlerp;
}

} // namespace


/*
===================
CG_LoadClientInfo

Load it now, taking the disk hits.
This will usually be deferred to a safe time
===================
*/
static void CG_LoadClientInfo( clientInfo_t *ci ) {
	std::array<char, MAX_QPATH> teamName = BuildClientTeamNamePrefix( *ci );
	std::array<char, MAX_CVAR_VALUE_STRING> vertexLight{};

	// disable vertexlight for colored skins
	trap_Cvar_VariableStringBuffer( "r_vertexlight", vertexLight.data(), vertexLight.size() );
	if ( VertexLightWasEnabled( vertexLight ) ) {
		trap_Cvar_Set( "r_vertexlight", "0" );
	}

	const bool modelLoaded = RegisterClientAssetsWithFallback( *ci, teamName );
	UpdateClientAnimationSupport( *ci );
	RegisterClientSounds( *ci, modelLoaded );

	ci->deferred = qfalse;

	// reset any existing players and bodies, because they might be in bad
	// frames for this new model
	ResetPlayerEntitiesForClient( static_cast<int>( ci - cgs.clientinfo ) );

	// restore vertexlight mode
	if ( VertexLightWasEnabled( vertexLight ) ) {
		trap_Cvar_Set( "r_vertexlight", vertexLight.data() );
	}
}


/*
======================
CG_CopyClientInfoModel
======================
*/
static void CG_CopyClientInfoModel( const clientInfo_t &from, clientInfo_t &to ) {
	VectorCopy( from.headOffset, to.headOffset );
	to.footsteps = from.footsteps;
	to.gender = from.gender;

	to.legsModel = from.legsModel;
	to.legsSkin = from.legsSkin;
	to.torsoModel = from.torsoModel;
	to.torsoSkin = from.torsoSkin;
	to.headModel = from.headModel;
	to.headSkin = from.headSkin;
	to.modelIcon = from.modelIcon;

	to.newAnims = from.newAnims;
	to.coloredSkin = from.coloredSkin;

	std::copy_n( from.animations, MAX_ANIMATIONS, to.animations );
	std::copy_n( from.sounds, MAX_CUSTOM_SOUNDS, to.sounds );
}

namespace {

void ApplyCopiedClientInfoModel( const clientInfo_t &match, clientInfo_t &clientInfo, const qboolean deferred ) {
	clientInfo.deferred = deferred;
	CG_CopyClientInfoModel( match, clientInfo );
}

void LoadClientInfoImmediately( clientInfo_t &clientInfo ) {
	CG_LoadClientInfo( &clientInfo );
}

[[nodiscard]] bool TryReuseExactClientInfo( clientInfo_t &clientInfo ) {
	if ( const clientInfo_t *match = FindLoadedClientInfo( clientInfo ); match != nullptr ) {
		ApplyCopiedClientInfoModel( *match, clientInfo, qfalse );
		return true;
	}

	return false;
}

[[nodiscard]] bool TryReuseCompatibleDeferredClientInfo( clientInfo_t &clientInfo ) {
	if ( const clientInfo_t *match = FindLoadedClientWithSameSkin( clientInfo ); match != nullptr ) {
		ApplyCopiedClientInfoModel( *match, clientInfo, qtrue );
		return true;
	}

	return false;
}

[[nodiscard]] bool TryReuseFirstValidClientInfo( clientInfo_t &clientInfo ) {
	if ( const clientInfo_t *match = FindFirstValidClientInfo(); match != nullptr ) {
		ApplyCopiedClientInfoModel( *match, clientInfo, qtrue );
		return true;
	}

	return false;
}

[[nodiscard]] bool ShouldLoadDeferredClientInfo( const clientInfo_t &clientInfo ) noexcept {
	return clientInfo.infoValid && clientInfo.deferred;
}

} // namespace


/*
======================
CG_ScanForExistingClientInfo
======================
*/
static qboolean CG_ScanForExistingClientInfo( clientInfo_t &clientInfo ) {
	return TryReuseExactClientInfo( clientInfo ) ? qtrue : qfalse;
}


/*
======================
CG_SetDeferredClientInfo

We aren't going to load it now, so grab some other
client's info to use until we have some spare time.
======================
*/
static void CG_SetDeferredClientInfo( clientInfo_t &clientInfo ) {
	// if someone else is already the same models and skins we
	// can just load the client info
	if ( FindLoadedClientWithSameModel( clientInfo ) != nullptr ) {
		// just load the real info cause it uses the same models and skins
		LoadClientInfoImmediately( clientInfo );
		return;
	}

	// if we are in teamplay, only grab a model if the skin is correct
	if ( cgs.gametype >= GT_TEAM ) {
		if ( TryReuseCompatibleDeferredClientInfo( clientInfo ) ) {
			return;
		}
		// load the full model, because we don't ever want to show
		// an improper team skin.  This will cause a hitch for the first
		// player, when the second enters.  Combat shouldn't be going on
		// yet, so it shouldn't matter
		LoadClientInfoImmediately( clientInfo );
		return;
	}

	// find the first valid clientinfo and grab its stuff
	if ( TryReuseFirstValidClientInfo( clientInfo ) ) {
		return;
	}

	// we should never get here...
	CG_Printf( "CG_SetDeferredClientInfo: no valid clients!\n" );

	LoadClientInfoImmediately( clientInfo );
}

namespace {

void ResolveClientInfoLoad( clientInfo_t &clientInfo, const bool canDefer ) {
	if ( CG_ScanForExistingClientInfo( clientInfo ) ) {
		return;
	}

	const bool forceDefer = IsLowMemoryForClientLoad();
	if ( forceDefer || ( canDefer && !cg_buildScript.integer && !cg.loading ) ) {
		CG_SetDeferredClientInfo( clientInfo );
		if ( forceDefer ) {
			CG_Printf( "Memory is low. Using deferred model.\n" );
			clientInfo.deferred = qfalse;
		}
		return;
	}

	LoadClientInfoImmediately( clientInfo );
}

} // namespace


static void CG_SetSkinAndModel( clientInfo_t *newInfo,
		const char *infomodel,
		const ClientModelSelectionContext &selectionContext,
		qboolean setColor,
		char *modelName, int modelNameSize,
		char *skinName, int skinNameSize ) 
{
	std::array<char, MAX_QPATH> modelStr{};
	qboolean	pm_model;
	const qboolean allowNativeModel = selectionContext.allowNativeModel;
	const int clientNum = selectionContext.clientNum;
	const int myClientNum = selectionContext.myClientNum;
	const team_t myTeam = selectionContext.myTeam;
	const team_t	team = newInfo->team;
	const char	*colors;
	
	pm_model = ( Q_stricmp( cg_enemyModel.string, PM_SKIN ) == 0 ) ? qtrue : qfalse;

	if ( cg_forceModel.integer || cg_enemyModel.string[0] || cg_teamModel.string[0] )
	{
		if ( cgs.gametype >= GT_TEAM )
		{
			// enemy model
			if ( cg_enemyModel.string[0] && team != myTeam && team != TEAM_SPECTATOR ) {
				if ( pm_model ) {
					CopyPmModelSelection( infomodel, modelName, modelNameSize, skinName, skinNameSize );
				} else {
					Q_strncpyz( modelName, cg_enemyModel.string, modelNameSize );
					if ( char *skin = strchr( modelName, '/' ); skin != nullptr ) {
						*skin = '\0';
					}
					Q_strncpyz( skinName, PM_SKIN, skinNameSize );
				}

				if ( setColor ) {
					if ( cg_enemyColors.string[0] && myTeam != TEAM_SPECTATOR ) // free-fly?
						colors = CG_GetTeamColors( cg_enemyColors.string, newInfo->team );
					else
						colors = CG_GetTeamColors( "???", newInfo->team );

					CG_SetColorInfo( colors, newInfo );
					newInfo->coloredSkin = qtrue;
				}

			} else if ( cg_teamModel.string[0] && team == myTeam && team != TEAM_SPECTATOR && clientNum != myClientNum ) {
				// teammodel
				pm_model = ( Q_stricmp( cg_teamModel.string, PM_SKIN ) == 0 ) ? qtrue : qfalse;

				if ( pm_model ) {
					CopyPmModelSelection( infomodel, modelName, modelNameSize, skinName, skinNameSize );
				} else {
					Q_strncpyz( modelName, cg_teamModel.string, modelNameSize );
					if ( char *skin = strchr( modelName, '/' ); skin != nullptr ) {
						*skin = '\0';
					}
					Q_strncpyz( skinName, PM_SKIN, skinNameSize );
				}

				if ( setColor ) {
					if ( cg_teamColors.string[0] && myTeam != TEAM_SPECTATOR ) // free-fly?
						colors = CG_GetTeamColors( cg_teamColors.string, newInfo->team );
					else
						colors = CG_GetTeamColors( "???", newInfo->team );

					CG_SetColorInfo( colors, newInfo );
					newInfo->coloredSkin = qtrue;
				}

			} else {
				// forcemodel etc.
				if ( cg_forceModel.integer ) {

					trap_Cvar_VariableStringBuffer( "model", modelStr.data(), modelStr.size() );
					CopyModelAndSkin( modelStr.data(), "default", modelName, modelNameSize, skinName, skinNameSize );

				} else {
					CopyModelAndSkin( infomodel, "default", modelName, modelNameSize, skinName, skinNameSize );
				}
			}
		} else { // not team game

			if ( pm_model && myClientNum != clientNum && cgs.gametype != GT_SINGLE_PLAYER ) {
				CopyPmModelSelection( infomodel, modelName, modelNameSize, skinName, skinNameSize );

				if ( setColor ) {
					colors = CG_GetTeamColors( cg_enemyColors.string, newInfo->team );
					CG_SetColorInfo( colors, newInfo );
					newInfo->coloredSkin = qtrue;
				}

			} else if ( cg_enemyModel.string[0] && myClientNum != clientNum && !allowNativeModel && cgs.gametype != GT_SINGLE_PLAYER ) {

				CopyModelAndSkin( cg_enemyModel.string, PM_SKIN, modelName, modelNameSize, skinName, skinNameSize );

				if ( setColor ) {
					colors = CG_GetTeamColors( cg_enemyColors.string, newInfo->team );
					CG_SetColorInfo( colors, newInfo );
					newInfo->coloredSkin = qtrue;
				}
			} else { // forcemodel, etc.
				if ( cg_forceModel.integer ) {

					trap_Cvar_VariableStringBuffer( "model", modelStr.data(), modelStr.size() );
					CopyModelAndSkin( modelStr.data(), "default", modelName, modelNameSize, skinName, skinNameSize );
				} else {
					CopyModelAndSkin( infomodel, "default", modelName, modelNameSize, skinName, skinNameSize );
				}
			}
		}
	}
	else // !cg_forcemodel && !cg_enemyModel && !cg_teamModel
	{
		CopyModelAndSkin( infomodel, "default", modelName, modelNameSize, skinName, skinNameSize );
	}
}


/*
======================
CG_NewClientInfo
======================
*/
void CG_NewClientInfo( int clientNum ) {
	clientInfo_t *ci;
	clientInfo_t newInfo{};
	const char	*configstring;

	ci = &cgs.clientinfo[clientNum];

	configstring = CG_ConfigString( clientNum + CS_PLAYERS );
	if ( !configstring[0] ) {
		*ci = clientInfo_t{};
		return;	// player just left
	}

	const ClientModelSelectionContext selectionContext = BuildClientModelSelectionContext( clientNum );

	// build into a temp buffer so the defer checks can use
	// the old value
	// isolate the player's name
	Q_strncpyz( newInfo.name, Info_ValueForKey( configstring, "n" ), sizeof( newInfo.name ) );

	// team
	newInfo.team = ConfigStringTeamValue( configstring );

	// colors
	ApplyClientBaseColors( configstring, newInfo );

	// bot skill
	newInfo.botSkill = ConfigStringIntValue( configstring, "skill" );

	// handicap
	newInfo.handicap = ConfigStringIntValue( configstring, "hc" );

	// wins
	newInfo.wins = ConfigStringIntValue( configstring, "w" );

	// losses
	newInfo.losses = ConfigStringIntValue( configstring, "l" );

	// always apply team colors [4] and [5] if specified, this will work in non-team games too
	ApplyConfiguredTeamColors( newInfo, selectionContext );

	// team task
	newInfo.teamTask = ConfigStringIntValue( configstring, "tt" );

	// team leader
	newInfo.teamLeader = ConfigStringIntValue( configstring, "tl" );

	// model
	CG_SetSkinAndModel( &newInfo, Info_ValueForKey( configstring, "model" ), selectionContext, qtrue, 
		newInfo.modelName, sizeof( newInfo.modelName ),	newInfo.skinName, sizeof( newInfo.skinName ) );

	// head model
	CG_SetSkinAndModel( &newInfo, Info_ValueForKey( configstring, "hmodel" ), selectionContext, qfalse, 
		newInfo.headModelName, sizeof( newInfo.headModelName ),	newInfo.headSkinName, sizeof( newInfo.headSkinName ) );

	ResolveClientInfoLoad( newInfo, CanDeferClientLoad( selectionContext, newInfo.team ) );

	// replace whatever was there with the new one
	newInfo.infoValid = qtrue;
	*ci = newInfo;
}


/*
======================
CG_LoadDeferredPlayers

Called each frame when a player is dead
and the scoreboard is up
so deferred players can be loaded
======================
*/
void CG_LoadDeferredPlayers( void ) {
	// scan for a deferred player to load
	for ( clientInfo_t &clientInfo : MutableClientInfoEntries() ) {
		if ( ShouldLoadDeferredClientInfo( clientInfo ) ) {
			// if we are low on memory, leave it deferred
			if ( IsLowMemoryForClientLoad() ) {
				CG_Printf( "Memory is low.  Using deferred model.\n" );
				clientInfo.deferred = qfalse;
				continue;
			}
			LoadClientInfoImmediately( clientInfo );
//			break;
		}
	}
}

/*
=============================================================================

PLAYER ANIMATION

=============================================================================
*/


/*
===============
CG_SetLerpFrameAnimation

may include ANIM_TOGGLEBIT
===============
*/
static void CG_SetLerpFrameAnimation( clientInfo_t *ci, lerpFrame_t *lf, int newAnimation ) {
	lf->animationNumber = newAnimation;
	const int animationNumber = AnimationNumberWithoutToggle( newAnimation );

	if ( animationNumber < 0 || animationNumber >= MAX_TOTALANIMATIONS ) {
		CG_Error( "Bad animation number: %i", animationNumber );
	}

	animation_t &animation = ci->animations[animationNumber];
	lf->animation = &animation;
	lf->animationTime = lf->frameTime + animation.initialLerp;

	if ( cg_debugAnim.integer ) {
		CG_Printf( "Anim: %i\n", animationNumber );
	}
}


/*
===============
CG_RunLerpFrame

Sets cg.snap, cg.oldFrame, and cg.backlerp
cg.time should be between oldFrameTime and frameTime after exit
===============
*/
static void CG_RunLerpFrame( clientInfo_t *ci, lerpFrame_t *lf, int newAnimation, float speedScale ) {
	// debugging tool to get no animations
	if ( cg_animSpeed.integer == 0 ) {
		lf->oldFrame = 0;
		lf->frame = 0;
		lf->backlerp = 0.0f;
		return;
	}

	// see if the animation sequence is switching
	if ( newAnimation != lf->animationNumber || !lf->animation ) {
		CG_SetLerpFrameAnimation( ci, lf, newAnimation );
	}

	// if we have passed the current frame, move it to
	// oldFrame and calculate a new frame
	if ( cg.time >= lf->frameTime ) {
		lf->oldFrame = lf->frame;
		lf->oldFrameTime = lf->frameTime;

		// get the next frame based on the animation
		const animation_t &animation = *lf->animation;
		if ( !animation.frameLerp ) {
			return;		// shouldn't happen
		}
		if ( cg.time < lf->animationTime ) {
			lf->frameTime = lf->animationTime;		// initial lerp
		} else {
			lf->frameTime = lf->oldFrameTime + animation.frameLerp;
		}
		int frameOffset = ( lf->frameTime - lf->animationTime ) / animation.frameLerp;
		frameOffset = static_cast<int>( frameOffset * speedScale );		// adjust for haste, etc

		const int numFrames = EffectiveAnimationFrameCount( animation );
		if ( frameOffset >= numFrames ) {
			frameOffset -= numFrames;
			if ( animation.loopFrames ) {
				frameOffset %= animation.loopFrames;
				frameOffset += animation.numFrames - animation.loopFrames;
			} else {
				frameOffset = numFrames - 1;
				// the animation is stuck at the end, so it
				// can immediately transition to another sequence
				lf->frameTime = cg.time;
			}
		}
		lf->frame = ResolveAnimationFrame( animation, frameOffset );
		if ( cg.time > lf->frameTime ) {
			lf->frameTime = cg.time;
			if ( cg_debugAnim.integer ) {
				CG_Printf( "Clamp lf->frameTime\n");
			}
		}
	}

	if ( lf->frameTime > cg.time + 200 ) {
		lf->frameTime = cg.time;
	}

	if ( lf->oldFrameTime > cg.time ) {
		lf->oldFrameTime = cg.time;
	}
	// calculate current lerp value
	if ( lf->frameTime == lf->oldFrameTime ) {
		lf->backlerp = 0.0f;
	} else {
		lf->backlerp = 1.0f - static_cast<float>( cg.time - lf->oldFrameTime ) / ( lf->frameTime - lf->oldFrameTime );
	}
}


/*
===============
CG_ClearLerpFrame
===============
*/
static void CG_ClearLerpFrame( clientInfo_t *ci, lerpFrame_t *lf, int animationNumber ) {
	lf->frameTime = lf->oldFrameTime = cg.time;
	CG_SetLerpFrameAnimation( ci, lf, animationNumber );
	lf->oldFrame = lf->animation->firstFrame;
	lf->frame = lf->animation->firstFrame;
}


/*
===============
CG_PlayerAnimation
===============
*/
static void CG_PlayerAnimation( centity_t *cent, int *legsOld, int *legs, float *legsBackLerp,
						int *torsoOld, int *torso, float *torsoBackLerp ) {
	const int clientNum = cent->currentState.clientNum;

	if ( cg_noPlayerAnims.integer ) {
		*legsOld = 0;
		*legs = 0;
		*torsoOld = 0;
		*torso = 0;
		*legsBackLerp = 0.0f;
		*torsoBackLerp = 0.0f;
		return;
	}

	const float speedScale = PlayerAnimationSpeedScale( *cent );
	clientInfo_t *ci = &cgs.clientinfo[clientNum];

	// do the shuffle turn frames locally
	CG_RunLerpFrame( ci, &cent->pe.legs, LegsAnimationForCurrentState( *cent ), speedScale );
	ExportLerpFrameState( cent->pe.legs, legsOld, legs, legsBackLerp );

	CG_RunLerpFrame( ci, &cent->pe.torso, cent->currentState.torsoAnim, speedScale );
	ExportLerpFrameState( cent->pe.torso, torsoOld, torso, torsoBackLerp );
}

/*
=============================================================================

PLAYER ANGLES

=============================================================================
*/

/*
==================
CG_SwingAngles
==================
*/
static void CG_SwingAngles( float destination, float swingTolerance, float clampTolerance,
					float speed, float *angle, qboolean *swinging ) {
	float &currentAngle = *angle;
	qboolean &isSwinging = *swinging;

	if ( !isSwinging ) {
		// see if a swing should be started
		const float swing = AngleSubtract( currentAngle, destination );
		if ( swing > swingTolerance || swing < -swingTolerance ) {
			isSwinging = qtrue;
		}
	}

	if ( !isSwinging ) {
		return;
	}
	
	// modify the speed depending on the delta
	// so it doesn't seem so linear
	const float swing = AngleSubtract( destination, currentAngle );
	const float scale = SwingScaleForDelta( swing, swingTolerance );

	// swing towards the destination angle
	if ( swing >= 0 ) {
		float move = cg.frametime * scale * speed;
		if ( move >= swing ) {
			move = swing;
			isSwinging = qfalse;
		}
		currentAngle = AngleMod( currentAngle + move );
	} else {
		float move = cg.frametime * scale * -speed;
		if ( move <= swing ) {
			move = swing;
			isSwinging = qfalse;
		}
		currentAngle = AngleMod( currentAngle + move );
	}

	// clamp to no more than tolerance
	const float clampedSwing = AngleSubtract( destination, currentAngle );
	if ( clampedSwing > clampTolerance ) {
		currentAngle = AngleMod( destination - ( clampTolerance - 1.0f ) );
	} else if ( clampedSwing < -clampTolerance ) {
		currentAngle = AngleMod( destination + ( clampTolerance - 1.0f ) );
	}
}


/*
=================
CG_AddPainTwitch
=================
*/
static void CG_AddPainTwitch( const centity_t *cent, vec3_t torsoAngles ) {
	const float factor = PainTwitchFactor( *cent );
	if ( factor == 0.0f ) {
		return;
	}

	if ( cent->pe.painDirection ) {
		torsoAngles[ROLL] += 20.0f * factor;
	} else {
		torsoAngles[ROLL] -= 20.0f * factor;
	}
}


/*
===============
CG_PlayerAngles

Handles seperate torso motion

  legs pivot based on direction of movement

  head always looks exactly at cent->lerpAngles

  if motion < 20 degrees, show in head only
  if < 45 degrees, also show in torso
===============
*/
static void CG_PlayerAngles( centity_t *cent, vec3_t legs[3], vec3_t torso[3], vec3_t head[3] ) {
	vec3_t		legsAngles, torsoAngles, headAngles;
	float		dest;
	static constexpr auto movementOffsets = std::to_array<int>( { 0, 22, 45, -22, 0, 22, -45, -22 } );
	vec3_t		velocity;
	float		speed;

	VectorCopy( cent->lerpAngles, headAngles );
	headAngles[YAW] = AngleMod( headAngles[YAW] );
	VectorClear( legsAngles );
	VectorClear( torsoAngles );

	// --------- yaw -------------

	// allow yaw to drift a bit
	if ( ShouldCenterPlayerAngles( cent->currentState ) ) {
		// if not standing still, always point all in the same direction
		cent->pe.torso.yawing = qtrue;	// always center
		cent->pe.torso.pitching = qtrue;	// always center
		cent->pe.legs.yawing = qtrue;	// always center
	}

	// adjust legs for movement dir
	const int dir = CurrentMovementDirection( *cent );
	legsAngles[YAW] = headAngles[YAW] + movementOffsets[ dir ];
	torsoAngles[YAW] = headAngles[YAW] + 0.25 * movementOffsets[ dir ];

	// torso
	CG_SwingAngles( torsoAngles[YAW], 25, 90, cg_swingSpeed.value, &cent->pe.torso.yawAngle, &cent->pe.torso.yawing );
	CG_SwingAngles( legsAngles[YAW], 40, 90, cg_swingSpeed.value, &cent->pe.legs.yawAngle, &cent->pe.legs.yawing );

	torsoAngles[YAW] = cent->pe.torso.yawAngle;
	legsAngles[YAW] = cent->pe.legs.yawAngle;


	// --------- pitch -------------

	// only show a fraction of the pitch angle in the torso
	if ( headAngles[PITCH] > 180 ) {
		dest = (-360 + headAngles[PITCH]) * 0.75f;
	} else {
		dest = headAngles[PITCH] * 0.75f;
	}
	CG_SwingAngles( dest, 15, 30, 0.1f, &cent->pe.torso.pitchAngle, &cent->pe.torso.pitching );
	torsoAngles[PITCH] = cent->pe.torso.pitchAngle;

	//
	const int clientNum = cent->currentState.clientNum;
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		const clientInfo_t *ci = &cgs.clientinfo[ clientNum ];
		if ( ci->fixedtorso ) {
			torsoAngles[PITCH] = 0.0f;
		}
	}

	// --------- roll -------------


	// lean towards the direction of travel
	VectorCopy( cent->currentState.pos.trDelta, velocity );
	speed = VectorNormalize( velocity );
	if ( speed ) {
		vec3_t	axis[3];
		float	side;

		speed *= 0.05f;

		AnglesToAxis( legsAngles, axis );
		side = speed * DotProduct( velocity, axis[1] );
		legsAngles[ROLL] -= side;

		side = speed * DotProduct( velocity, axis[0] );
		legsAngles[PITCH] += side;
	}

	//
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		const clientInfo_t *ci = &cgs.clientinfo[ clientNum ];
		if ( ci->fixedlegs ) {
			legsAngles[YAW] = torsoAngles[YAW];
			legsAngles[PITCH] = 0.0f;
			legsAngles[ROLL] = 0.0f;
		}
	}

	// pain twitch
	CG_AddPainTwitch( cent, torsoAngles );

	// pull the angles back out of the hierarchial chain
	AnglesSubtract( headAngles, torsoAngles, headAngles );
	AnglesSubtract( torsoAngles, legsAngles, torsoAngles );
	AnglesToAxis( legsAngles, legs );
	AnglesToAxis( torsoAngles, torso );
	AnglesToAxis( headAngles, head );
}


//==========================================================================

/*
===============
CG_HasteTrail
===============
*/
static void CG_HasteTrail( centity_t *cent ) {
	localEntity_t	*smoke;
	vec3_t			origin;

	if ( cent->trailTime > cg.time ) {
		return;
	}
	const int anim = AnimationNumberWithoutToggle( cent->pe.legs.animationNumber );
	if ( !IsRunLegAnimation( anim ) ) {
		return;
	}

	AdvanceEffectTimestamp( cent->trailTime, 100 );

	VectorCopy( cent->lerpOrigin, origin );
	origin[2] -= 16;

	smoke = CG_SmokePuff( origin, vec3_origin, 
				  8, 
				  1, 1, 1, 1,
				  500, 
				  cg.time,
				  0,
				  0,
				  cgs.media.hastePuffShader );

	// use the optimized local entity add
	smoke->leType = LE_SCALE_FADE;
}


#ifdef MISSIONPACK
/*
===============
CG_BreathPuffs
===============
*/
static void CG_BreathPuffs( const centity_t *cent, const refEntity_t *head ) {
	clientInfo_t *ci = &cgs.clientinfo[cent->currentState.number];
	vec3_t up, origin;

	if ( !CanEmitBreathPuff( *cent, *head, *ci ) ) {
		return;
	}

	BuildBreathPuffVectors( *head, up, origin );
	CG_SmokePuff( origin, up, 16, 1, 1, 1, 0.66f, 1500, cg.time, cg.time + 400, LEF_PUFF_DONT_SCALE, cgs.media.shotgunSmokePuffShader );
	ci->breathPuffTime = cg.time + 2000;
}

/*
===============
CG_DustTrail
===============
*/
static void CG_DustTrail( centity_t *cent ) {
	vec3_t end, vel;
	trace_t tr;

	if ( !cg_enableDust.integer ) {
		return;
	}

	if ( cent->dustTrailTime > cg.time ) {
		return;
	}

	const int anim = AnimationNumberWithoutToggle( cent->pe.legs.animationNumber );
	if ( !IsLandingLegAnimation( anim ) ) {
		return;
	}

	AdvanceEffectTimestamp( cent->dustTrailTime, 40 );

	VectorCopy( cent->currentState.pos.trBase, end );
	end[2] -= 64;
	CG_Trace( &tr, cent->currentState.pos.trBase, nullptr, nullptr, end, cent->currentState.number, MASK_PLAYERSOLID );

	if ( !( tr.surfaceFlags & SURF_DUST ) ) {
		return;
	}

	VectorCopy( cent->currentState.pos.trBase, end );
	end[2] -= 16;

	VectorSet( vel, 0, 0, -30 );
	CG_SmokePuff( end, vel,
				  24,
				  .8f, .8f, 0.7f, 0.33f,
				  500,
				  cg.time,
				  0,
				  0,
				  cgs.media.dustPuffShader );
}
#endif


/*
===============
CG_TrailItem
===============
*/
static void CG_TrailItem( const centity_t *cent, qhandle_t hModel ) {
	refEntity_t		ent{};
	vec3_t			angles;
	vec3_t			axis[3];

	VectorCopy( cent->lerpAngles, angles );
	angles[PITCH] = 0;
	angles[ROLL] = 0;
	AnglesToAxis( angles, axis );

	VectorMA( cent->lerpOrigin, -16, axis[0], ent.origin );
	ent.origin[2] += 16;
	angles[YAW] += 90;
	AnglesToAxis( angles, ent.axis );

	ent.hModel = hModel;
	trap_R_AddRefEntityToScene( &ent );
}

namespace {

struct FlagAnimationSelection {
	int animation;
	bool updateYaw;
};

[[nodiscard]] FlagAnimationSelection DeterminePlayerFlagAnimation( const int legsAnimation ) noexcept {
	const int animation = AnimationNumberWithoutToggle( legsAnimation );
	if ( animation == LEGS_IDLE || animation == LEGS_IDLECR ) {
		return { FLAG_STAND, false };
	}
	if ( animation == LEGS_WALK || animation == LEGS_WALKCR ) {
		return { FLAG_STAND, true };
	}
	return { FLAG_RUN, true };
}

void UpdatePlayerFlagYaw( centity_t &cent, const refEntity_t &pole ) {
	vec3_t dir;
	VectorCopy( cent.currentState.pos.trDelta, dir );
	dir[2] += 100.0f;
	VectorNormalize( dir );

	const float poleAlignment = DotProduct( pole.axis[2], dir );
	if ( fabs( poleAlignment ) >= 0.9f ) {
		return;
	}

	const float forwardDot = std::clamp( DotProduct( pole.axis[0], dir ), -1.0f, 1.0f );
	float yaw = static_cast<float>( acos( forwardDot ) * 180.0 / M_PI );
	if ( DotProduct( pole.axis[1], dir ) < 0.0f ) {
		yaw = 360.0f - yaw;
	}

	CG_SwingAngles( AngleMod( yaw ), 25, 90, 0.15f, &cent.pe.flag.yawAngle, &cent.pe.flag.yawing );
}

void ApplyLerpFrameToEntity( const lerpFrame_t &lerpFrame, refEntity_t &entity ) noexcept {
	entity.oldframe = lerpFrame.oldFrame;
	entity.frame = lerpFrame.frame;
	entity.backlerp = lerpFrame.backlerp;
}

} // namespace


/*
===============
CG_PlayerFlag
===============
*/
static void CG_PlayerFlag( centity_t *cent, qhandle_t hSkin, refEntity_t *torso ) {
	refEntity_t	pole{};
	refEntity_t	flag{};
	vec3_t		angles;

	// show the flag pole model
	pole.hModel = cgs.media.flagPoleModel;
	VectorCopy( torso->lightingOrigin, pole.lightingOrigin );
	pole.shadowPlane = torso->shadowPlane;
	pole.renderfx = torso->renderfx;
	CG_PositionEntityOnTag( &pole, torso, torso->hModel, "tag_flag" );
	trap_R_AddRefEntityToScene( &pole );

	// show the flag model
	flag.hModel = cgs.media.flagFlapModel;
	flag.customSkin = hSkin;
	VectorCopy( torso->lightingOrigin, flag.lightingOrigin );
	flag.shadowPlane = torso->shadowPlane;
	flag.renderfx = torso->renderfx;

	const FlagAnimationSelection flagAnimation = DeterminePlayerFlagAnimation( cent->currentState.legsAnim );
	if ( flagAnimation.updateYaw ) {
		UpdatePlayerFlagYaw( *cent, pole );
	}

	// set the yaw angle
	VectorClear( angles );
	angles[YAW] = cent->pe.flag.yawAngle;
	// lerp the flag animation frames
	CG_RunLerpFrame( &cgs.clientinfo[cent->currentState.clientNum], &cent->pe.flag, flagAnimation.animation, 1.0f );
	ApplyLerpFrameToEntity( cent->pe.flag, flag );

	AnglesToAxis( angles, flag.axis );
	CG_PositionRotatedEntityOnTag( &flag, &pole, pole.hModel, "tag_flag" );

	trap_R_AddRefEntityToScene( &flag );
}


#ifdef MISSIONPACK // bk001204
/*
===============
CG_PlayerTokens
===============
*/
static void CG_PlayerTokens( centity_t *cent, int renderfx ) {
	refEntity_t	ent{};
	vec3_t		origin;
	skulltrail_t &trail = PlayerSkullTrail( *cent );
	const int tokens = ClampedSkullTokenCount( *cent );
	if ( !tokens ) {
		trail.numpositions = 0;
		return;
	}

	ExtendSkullTrail( trail, cent->lerpOrigin, tokens );
	AdvanceSkullTrail( trail, cent->lerpOrigin );

	ent.hModel = PlayerTokenModel( *cent );
	ent.renderfx = renderfx;

	VectorCopy( cent->lerpOrigin, origin );
	for ( int trailIndex = 0; trailIndex < trail.numpositions; ++trailIndex ) {
		PreparePlayerTokenEntity( ent, origin, trail.positions[trailIndex], trailIndex );
		trap_R_AddRefEntityToScene( &ent );
		VectorCopy( trail.positions[trailIndex], origin );
	}
}
#endif


/*
===============
CG_PlayerPowerups
===============
*/
static void CG_PlayerPowerups( centity_t *cent, refEntity_t *torso ) {
	const int powerups = cent->currentState.powerups;

	if ( !powerups ) {
		return;
	}

	// quad gives a dlight
	if ( HasPowerup( powerups, PW_QUAD ) ) {
		if ( ClientTeam( *cent ) == TEAM_RED ) {
			AddPowerupLight( cent->lerpOrigin, 1.0f, 0.2f, 0.2f );
		} else {
			AddPowerupLight( cent->lerpOrigin, 0.2f, 0.2f, 1.0f );
		}
	}

	// flight plays a looped sound
	if ( HasPowerup( powerups, PW_FLIGHT ) ) {
		trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, cgs.media.flightSound );
	}

	// redflag
	if ( HasPowerup( powerups, PW_REDFLAG ) ) {
		AddFlagPowerupVisual( cent, torso, { cgs.media.redFlagFlapSkin, cgs.media.redFlagModel, 1.0f, 0.2f, 0.2f } );
	}

	// blueflag
	if ( HasPowerup( powerups, PW_BLUEFLAG ) ) {
		AddFlagPowerupVisual( cent, torso, { cgs.media.blueFlagFlapSkin, cgs.media.blueFlagModel, 0.2f, 0.2f, 1.0f } );
	}

	// neutralflag
	if ( HasPowerup( powerups, PW_NEUTRALFLAG ) ) {
		AddFlagPowerupVisual( cent, torso, { cgs.media.neutralFlagFlapSkin, cgs.media.neutralFlagModel, 1.0f, 1.0f, 1.0f } );
	}

	// haste leaves smoke trails
	if ( HasPowerup( powerups, PW_HASTE ) ) {
		CG_HasteTrail( cent );
	}
}


/*
===============
CG_PlayerFloatSprite

Float a sprite over the player's head
===============
*/
static void CG_PlayerFloatSprite( const centity_t *cent, qhandle_t shader ) {
	int				rf;
	refEntity_t		ent{};

	if ( cent->currentState.number == cg.snap->ps.clientNum && !cg.renderingThirdPerson ) {
		rf = RF_THIRD_PERSON;		// only show in mirrors
	} else {
		rf = 0;
	}

	VectorCopy( cent->lerpOrigin, ent.origin );
	ent.origin[2] += 48;
	ent.reType = RT_SPRITE;
	ent.customShader = shader;
	ent.radius = 10;
	ent.renderfx = rf;
	ent.shaderRGBA[0] = 255;
	ent.shaderRGBA[1] = 255;
	ent.shaderRGBA[2] = 255;
	ent.shaderRGBA[3] = 255;
	trap_R_AddRefEntityToScene( &ent );
}


/*
===============
CG_PlayerSprites

Float sprites over the player's head
===============
*/
static void CG_PlayerSprites( centity_t *cent ) {
	if ( const qhandle_t shader = PlayerStatusSpriteShader( cent->currentState.eFlags ); shader != 0 ) {
		CG_PlayerFloatSprite( cent, shader );
		return;
	}

	const team_t team = ClientTeam( *cent );
	if ( !(cent->currentState.eFlags & EF_DEAD) && 
		cg.snap->ps.persistant[PERS_TEAM] == team &&
		cgs.gametype >= GT_TEAM) {
		if (cg_drawFriend.integer) {
			CG_PlayerFloatSprite( cent, cgs.media.friendShader );
		}
		return;
	}
}


/*
===============
CG_PlayerShadow

Returns the Z component of the surface being shadowed

  should it return a full plane instead of a Z?
===============
*/
#define	SHADOW_DISTANCE		128
static qboolean CG_PlayerShadow( centity_t *cent, float *shadowPlane ) {
	vec3_t		end;
	const vec3_t	mins = { -15, -15, 0 };
	const vec3_t	maxs = { 15, 15, 2 };
	trace_t		trace;

	*shadowPlane = 0;

	if ( cg_shadows.integer == 0 ) {
		return qfalse;
	}

	// no shadows when invisible
	if ( HasPowerup( cent->currentState.powerups, PW_INVIS ) ) {
		return qfalse;
	}

	// send a trace down from the player to the ground
	VectorCopy( cent->lerpOrigin, end );
	end[2] -= SHADOW_DISTANCE;

	trap_CM_BoxTrace( &trace, cent->lerpOrigin, end, mins, maxs, 0, MASK_PLAYERSOLID );

	// no shadow if too high
	if ( trace.fraction == 1.0 || trace.startsolid || trace.allsolid ) {
		return qfalse;
	}

	*shadowPlane = trace.endpos[2] + 1;

	if ( cg_shadows.integer != 1 ) {	// no mark for stencil or projection shadows
		return qtrue;
	}

	// fade the shadow out with height
	const float alpha = 1.0f - trace.fraction;

	// bk0101022 - hack / FPE - bogus planes?
	//assert( DotProduct( trace.plane.normal, trace.plane.normal ) != 0.0f ) 

	// add the mark as a temporary, so it goes directly to the renderer
	// without taking a spot in the cg_marks array
	CG_ImpactMark( cgs.media.shadowMarkShader, trace.endpos, trace.plane.normal, 
		cent->pe.legs.yawAngle, alpha,alpha,alpha,1, qfalse, 24, qtrue );

	return qtrue;
}


/*
===============
CG_PlayerSplash

Draw a mark at the water surface
===============
*/
static void CG_PlayerSplash( const centity_t *cent ) {
	vec3_t		start, end;
	trace_t		trace;
	std::array<polyVert_t, 4> verts{};

	if ( !cg_shadows.integer ) {
		return;
	}

	VectorCopy( cent->lerpOrigin, end );
	end[2] -= 24;

	// if the feet aren't in liquid, don't make a mark
	// this won't handle moving water brushes, but they wouldn't draw right anyway...
	int contents = CG_PointContents( end, 0 );
	if ( !( contents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) ) {
		return;
	}

	VectorCopy( cent->lerpOrigin, start );
	start[2] += 32;

	// if the head isn't out of liquid, don't make a mark
	contents = CG_PointContents( start, 0 );
	if ( contents & ( CONTENTS_SOLID | CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		return;
	}

	// trace down to find the surface
	trap_CM_BoxTrace( &trace, start, end, nullptr, nullptr, 0, ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) );

	if ( trace.fraction == 1.0 ) {
		return;
	}

	// create a mark polygon
	SetSplashVertex( verts[0], trace.endpos, -32.0f, -32.0f, 0.0f, 0.0f );
	SetSplashVertex( verts[1], trace.endpos, -32.0f, 32.0f, 0.0f, 1.0f );
	SetSplashVertex( verts[2], trace.endpos, 32.0f, 32.0f, 1.0f, 1.0f );
	SetSplashVertex( verts[3], trace.endpos, 32.0f, -32.0f, 1.0f, 0.0f );

	trap_R_AddPolyToScene( cgs.media.wakeMarkShader, static_cast<int>( verts.size() ), verts.data() );
}


/*
===============
CG_AddRefEntityWithPowerups

Adds a piece with modifications or duplications for powerups
Also called by CG_Missile for quad rockets, but nobody can tell...
===============
*/
void CG_AddRefEntityWithPowerups( refEntity_t *ent, entityState_t *state, int team ) {
	const int powerups = state->powerups;

	if ( HasPowerup( powerups, PW_INVIS ) ) {
		AddPowerupShaderEntity( ent, cgs.media.invisShader );
		return;
	}

	/*
	if ( state->eFlags & EF_KAMIKAZE ) {
		if (team == TEAM_BLUE)
			ent->customShader = cgs.media.blueKamikazeShader;
		else
			ent->customShader = cgs.media.redKamikazeShader;
		trap_R_AddRefEntityToScene( ent );
	}
	else {*/
	trap_R_AddRefEntityToScene( ent );
	//}

	AddStandardEntityPowerupShaders( *ent, powerups, team );
}


/*
=================
CG_LightVerts
=================
*/
int CG_LightVerts( vec3_t normal, int numVerts, polyVert_t *verts ) {
	vec3_t			ambientLight;
	vec3_t			lightDir;
	vec3_t			directedLight;

	const auto vertexSpan = std::span{ verts, static_cast<std::size_t>( numVerts ) };
	trap_R_LightForPoint( vertexSpan.front().xyz, ambientLight, directedLight, lightDir );

	const float incoming = DotProduct( normal, lightDir );
	for ( polyVert_t &vertex : vertexSpan ) {
		if ( incoming <= 0 ) {
			SetVertexAmbientLighting( vertex, ambientLight );
			continue;
		}
		SetVertexDirectedLighting( vertex, ambientLight, directedLight, incoming );
	}
	return qtrue;
}


/*
===============
CG_Player
===============
*/
void CG_Player( centity_t *cent ) {
	clientInfo_t	*ci;
	refEntity_t		legs{};
	refEntity_t		torso{};
	refEntity_t		head{};
	int				clientNum;
	int				renderfx;
	qboolean		shadow;
	float			shadowPlane;

	// the client number is stored in clientNum.  It can't be derived
	// from the entity number, because a single client may have
	// multiple corpses on the level using the same clientinfo
	clientNum = cent->currentState.clientNum;
	if ( (unsigned) clientNum >= MAX_CLIENTS ) {
		CG_Error( "Bad clientNum on player entity" );
	}
	ci = &cgs.clientinfo[ clientNum ];

	// it is possible to see corpses from disconnected players that may
	// not have valid clientinfo
	if ( !ci->infoValid ) {
		return;
	}

	if ( ShouldSkipLocalPlayerModel( *cent ) ) {
		return;
	}
	renderfx = BasePlayerRenderfx( *cent );

	const bool darken = cg_deadBodyDarken.integer && ( cent->currentState.eFlags & EF_DEAD );

	// get the rotation information
	CG_PlayerAngles( cent, legs.axis, torso.axis, head.axis );
	
	// get the animation state (after rotation, to allow feet shuffle)
	CG_PlayerAnimation( cent, &legs.oldframe, &legs.frame, &legs.backlerp,
		 &torso.oldframe, &torso.frame, &torso.backlerp );

	// add the talk baloon or disconnect icon
	CG_PlayerSprites( cent );

	// add the shadow
	shadow = CG_PlayerShadow( cent, &shadowPlane );

	// add a water splash if partially in and out of water
	CG_PlayerSplash( cent );

	ApplySharedPlayerRenderfx( renderfx, shadow );
#ifdef MISSIONPACK
	if( cgs.gametype == GT_HARVESTER ) {
		CG_PlayerTokens( cent, renderfx );
	}
#endif
	//
	// add the legs
	//
	legs.hModel = ci->legsModel;
	legs.customSkin = ci->legsSkin;

	VectorCopy( cent->lerpOrigin, legs.origin );

	VectorCopy( cent->lerpOrigin, legs.lightingOrigin );
	legs.shadowPlane = shadowPlane;
	legs.renderfx = renderfx;
	VectorCopy (legs.origin, legs.oldorigin);	// don't positionally lerp at all

	ApplyPlayerPartColor( legs, ci->legsColor, darken );

	CG_AddRefEntityWithPowerups( &legs, &cent->currentState, ci->team );

	// if the model failed, allow the default nullmodel to be displayed
	if (!legs.hModel) {
		return;
	}

	//
	// add the torso
	//
	torso.hModel = ci->torsoModel;
	if (!torso.hModel) {
		return;
	}

	torso.customSkin = ci->torsoSkin;

	VectorCopy( cent->lerpOrigin, torso.lightingOrigin );

	CG_PositionRotatedEntityOnTag( &torso, &legs, ci->legsModel, "tag_torso");

	torso.shadowPlane = shadowPlane;
	torso.renderfx = renderfx;

	ApplyPlayerPartColor( torso, ci->bodyColor, darken );

	CG_AddRefEntityWithPowerups( &torso, &cent->currentState, ci->team );

#ifdef MISSIONPACK
	AddMissionpackTorsoEffects( *cent, torso, *ci, shadowPlane, renderfx );
#endif // MISSIONPACK

	//
	// add the head
	//
	head.hModel = ci->headModel;
	if (!head.hModel) {
		return;
	}
	head.customSkin = ci->headSkin;

	VectorCopy( cent->lerpOrigin, head.lightingOrigin );

	CG_PositionRotatedEntityOnTag( &head, &torso, ci->torsoModel, "tag_head");

	head.shadowPlane = shadowPlane;
	head.renderfx = renderfx;

	ApplyPlayerPartColor( head, ci->headColor, darken );
	
	CG_AddRefEntityWithPowerups( &head, &cent->currentState, ci->team );

#ifdef MISSIONPACK
	AddMissionpackHeadEffects( *cent, head );
#endif

	//
	// add the gun / barrel / flash
	//
	CG_AddPlayerWeapon( &torso, nullptr, cent, ci->team );

	// add powerups floating behind the player
	CG_PlayerPowerups( cent, &torso );
}


//=====================================================================

/*
===============
CG_ResetPlayerEntity

A player just came into view or teleported, so reset all animation info
===============
*/
void CG_ResetPlayerEntity( centity_t *cent ) {
	cent->errorTime = -99999;		// guarantee no error decay added
	cent->extrapolated = qfalse;	

	CG_ClearLerpFrame( &cgs.clientinfo[ cent->currentState.clientNum ], &cent->pe.legs, cent->currentState.legsAnim );
	CG_ClearLerpFrame( &cgs.clientinfo[ cent->currentState.clientNum ], &cent->pe.torso, cent->currentState.torsoAnim );

	BG_EvaluateTrajectory( &cent->currentState.pos, cg.time, cent->lerpOrigin );
	BG_EvaluateTrajectory( &cent->currentState.apos, cg.time, cent->lerpAngles );

	VectorCopy( cent->lerpOrigin, cent->rawOrigin );
	VectorCopy( cent->lerpAngles, cent->rawAngles );

	ResetPlayerLerpState( cent->pe.legs, cent->rawAngles[YAW], 0.0f );
	ResetPlayerLerpState( cent->pe.torso, cent->rawAngles[YAW], cent->rawAngles[PITCH] );

	if ( cg_debugPosition.integer ) {
		CG_Printf("%i ResetPlayerEntity yaw=%f\n", cent->currentState.number, cent->pe.torso.yawAngle );
	}
}
