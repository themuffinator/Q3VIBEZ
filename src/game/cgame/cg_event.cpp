// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_event.c -- handle entity events at snapshot or playerstate transitions

#include "cg_local.h"

#include <array>

// for the voice chats
#ifdef MISSIONPACK // bk001205
#include "../../ui/menudef.h"
#endif
//==========================================================================

static void CG_ItemPickup( int itemNum );
static int CG_WaterLevel( const centity_t *cent );

namespace {

struct ObituaryMessage {
	const char *message = nullptr;
	const char *suffix = "";
};

void StartEntitySound( const entityState_t &entityState, const int channel, const sfxHandle_t sound ) {
	trap_S_StartSound( nullptr, entityState.number, channel, sound );
}

void StartCustomEntitySound( const entityState_t &entityState, const int channel, const char *soundName ) {
	StartEntitySound( entityState, channel, CG_CustomSound( entityState.number, soundName ) );
}

void MarkPainIgnoreWindow( centity_t &cent ) noexcept {
	cent.pe.painIgnore = qtrue;
	cent.pe.painTime = cg.time;
}

[[nodiscard]] const char *OrdinalSuffix( const int rank ) noexcept {
	const int lastTwoDigits = rank % 100;
	if ( lastTwoDigits >= 11 && lastTwoDigits <= 13 ) {
		return "th";
	}

	switch ( rank % 10 ) {
	case 1:
		return "st";
	case 2:
		return "nd";
	case 3:
		return "rd";
	default:
		return "th";
	}
}

[[nodiscard]] const char *SpecialPlaceString( const int rank ) noexcept {
	switch ( rank ) {
	case 1:
		return S_COLOR_BLUE "1st" S_COLOR_WHITE;
	case 2:
		return S_COLOR_RED "2nd" S_COLOR_WHITE;
	case 3:
		return S_COLOR_YELLOW "3rd" S_COLOR_WHITE;
	default:
		return nullptr;
	}
}

[[nodiscard]] bool CanFollowAttacker( const int attacker ) noexcept {
	return cg_followKiller.integer && !cg.followTime && attacker != cg.snap->ps.clientNum && attacker < MAX_CLIENTS;
}

void FollowKillerIfSpectating( const int attacker ) noexcept {
	if ( cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR || !CanFollowAttacker( attacker ) ) {
		return;
	}

	cg.followClient = attacker;
	cg.followTime = cg.time;
}

void FollowKillerIfFollowing( const int attacker, const bool following ) noexcept {
	if ( !following || !CanFollowAttacker( attacker ) ) {
		return;
	}

	cg.followClient = attacker;
	cg.followTime = cg.time + 1100;
}

template <size_t N>
void CopyPlayerName( std::array<char, N> &nameBuffer, const char *playerInfo, const bool cleanName = false ) {
	Q_strncpyz( nameBuffer.data(), Info_ValueForKey( playerInfo, "n" ), nameBuffer.size() );
	if ( cleanName ) {
		Q_CleanStr( nameBuffer.data() );
	}
	Q_strcat( nameBuffer.data(), nameBuffer.size(), S_COLOR_WHITE );
}

[[nodiscard]] const char *WorldObituaryMessage( const int mod ) noexcept {
	switch ( mod ) {
	case MOD_SUICIDE:
		return "suicides";
	case MOD_FALLING:
		return "cratered";
	case MOD_CRUSH:
		return "was squished";
	case MOD_WATER:
		return "sank like a rock";
	case MOD_SLIME:
		return "melted";
	case MOD_LAVA:
		return "does a back flip into the lava";
	case MOD_TARGET_LASER:
		return "saw the light";
	case MOD_TRIGGER_HURT:
		return "was in the wrong place";
	default:
		return nullptr;
	}
}

[[nodiscard]] const char *DefaultSelfObituaryMessage( const gender_t gender ) noexcept {
	switch ( gender ) {
	case GENDER_FEMALE:
		return "killed herself";
	case GENDER_NEUTER:
		return "killed itself";
	default:
		return "killed himself";
	}
}

[[nodiscard]] const char *SelfObituaryMessage( const int mod, const gender_t gender ) noexcept {
	switch ( mod ) {
#ifdef MISSIONPACK
	case MOD_KAMIKAZE:
		return "goes out with a bang";
#endif
	case MOD_GRENADE_SPLASH:
		switch ( gender ) {
		case GENDER_FEMALE:
			return "tripped on her own grenade";
		case GENDER_NEUTER:
			return "tripped on its own grenade";
		default:
			return "tripped on his own grenade";
		}
	case MOD_ROCKET_SPLASH:
		switch ( gender ) {
		case GENDER_FEMALE:
			return "blew herself up";
		case GENDER_NEUTER:
			return "blew itself up";
		default:
			return "blew himself up";
		}
	case MOD_PLASMA_SPLASH:
		switch ( gender ) {
		case GENDER_FEMALE:
			return "melted herself";
		case GENDER_NEUTER:
			return "melted itself";
		default:
			return "melted himself";
		}
	case MOD_BFG_SPLASH:
		return "should have used a smaller gun";
#ifdef MISSIONPACK
	case MOD_PROXIMITY_MINE:
		switch ( gender ) {
		case GENDER_FEMALE:
			return "found her prox mine";
		case GENDER_NEUTER:
			return "found its prox mine";
		default:
			return "found his prox mine";
		}
#endif
	default:
		return DefaultSelfObituaryMessage( gender );
	}
}

[[nodiscard]] ObituaryMessage KillObituaryMessage( const int mod ) noexcept {
	switch ( mod ) {
	case MOD_GRAPPLE:
		return { "was caught by" };
	case MOD_GAUNTLET:
		return { "was pummeled by" };
	case MOD_MACHINEGUN:
		return { "was machinegunned by" };
	case MOD_SHOTGUN:
		return { "was gunned down by" };
	case MOD_GRENADE:
		return { "ate", "'s grenade" };
	case MOD_GRENADE_SPLASH:
		return { "was shredded by", "'s shrapnel" };
	case MOD_ROCKET:
		return { "ate", "'s rocket" };
	case MOD_ROCKET_SPLASH:
		return { "almost dodged", "'s rocket" };
	case MOD_PLASMA:
	case MOD_PLASMA_SPLASH:
		return { "was melted by", "'s plasmagun" };
	case MOD_RAILGUN:
		return { "was railed by" };
	case MOD_LIGHTNING:
		return { "was electrocuted by" };
	case MOD_BFG:
	case MOD_BFG_SPLASH:
		return { "was blasted by", "'s BFG" };
#ifdef MISSIONPACK
	case MOD_NAIL:
		return { "was nailed by" };
	case MOD_CHAINGUN:
		return { "got lead poisoning from", "'s Chaingun" };
	case MOD_PROXIMITY_MINE:
		return { "was too close to", "'s Prox Mine" };
	case MOD_KAMIKAZE:
		return { "falls to", "'s Kamikaze blast" };
	case MOD_JUICED:
		return { "was juiced by" };
#endif
	case MOD_TELEFRAG:
		return { "tried to invade", "'s personal space" };
	default:
		return { "was killed by" };
	}
}

[[nodiscard]] int ViewheightForLegsAnimation( const int legsAnimation ) noexcept {
	const int baseAnimation = legsAnimation & ~ANIM_TOGGLEBIT;
	return ( baseAnimation == LEGS_WALKCR || baseAnimation == LEGS_IDLECR ) ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT;
}

[[nodiscard]] const char *PainSoundPath( const int health ) noexcept {
	if ( health < 25 ) {
		return "*pain25_1.wav";
	}
	if ( health < 50 ) {
		return "*pain50_1.wav";
	}
	if ( health < 75 ) {
		return "*pain75_1.wav";
	}
	return "*pain100_1.wav";
}

[[nodiscard]] const char *RandomGurpSoundPath() noexcept {
	return ( rand() & 1 ) != 0 ? "sound/player/gurp1.wav" : "sound/player/gurp2.wav";
}

[[nodiscard]] sfxHandle_t FootstepSoundForEvent( const clientInfo_t &clientInfo, const int event ) noexcept {
	int footstepType = clientInfo.footsteps;
	switch ( event ) {
	case EV_FOOTSTEP_METAL:
		footstepType = FOOTSTEP_METAL;
		break;
	case EV_FOOTSPLASH:
	case EV_FOOTWADE:
	case EV_SWIM:
		footstepType = FOOTSTEP_SPLASH;
		break;
	default:
		break;
	}

	return cgs.media.footsteps[footstepType][rand() & 3];
}

void PlayFootstepEvent( const entityState_t &state, const clientInfo_t &clientInfo, const int event ) {
	if ( !cg_footsteps.integer ) {
		return;
	}

	trap_S_StartSound( nullptr, state.number, CHAN_BODY, FootstepSoundForEvent( clientInfo, event ) );
}

void ApplyLandingFeedback( const int clientNum, const int landingOffset ) noexcept {
	if ( clientNum != cg.predictedPlayerState.clientNum ) {
		return;
	}

	cg.landChange = landingOffset;
	cg.landTime = cg.time;
}

[[nodiscard]] gitem_t *PickupItemForIndex( const int itemIndex ) noexcept {
	if ( itemIndex < 1 || itemIndex >= bg_numItems ) {
		return nullptr;
	}
	return &bg_itemlist[itemIndex];
}

[[nodiscard]] centity_t *PickupEventEntity( const int entityNum ) noexcept {
	return entityNum >= 0 ? cg_entities + entityNum : nullptr;
}

[[nodiscard]] bool PickupEventDelayed( const centity_t *pickupEntity ) noexcept {
	return pickupEntity != nullptr && pickupEntity->delaySpawn > cg.time && pickupEntity->delaySpawnPlayed;
}

void MarkPickupEventHandled( centity_t *pickupEntity ) noexcept {
	if ( pickupEntity != nullptr ) {
		pickupEntity->delaySpawnPlayed = qtrue;
	}
}

[[nodiscard]] sfxHandle_t PersistantPowerupPickupSound( const int giTag ) noexcept {
#ifdef MISSIONPACK
	switch ( giTag ) {
	case PW_SCOUT:
		return cgs.media.scoutSound;
	case PW_GUARD:
		return cgs.media.guardSound;
	case PW_DOUBLER:
		return cgs.media.doublerSound;
	case PW_AMMOREGEN:
		return cgs.media.ammoregenSound;
	default:
		break;
	}
#else
	(void)giTag;
#endif
	return 0;
}

[[nodiscard]] sfxHandle_t LocalItemPickupSound( const gitem_t &item ) {
	if ( item.giType == IT_POWERUP || item.giType == IT_TEAM ) {
		return cgs.media.n_healthSound;
	}
	if ( item.giType == IT_PERSISTANT_POWERUP ) {
		return PersistantPowerupPickupSound( item.giTag );
	}
	return item.pickup_sound ? trap_S_RegisterSound( item.pickup_sound, qfalse ) : 0;
}

void HandleItemPickupEvent( const entityState_t &state, const int entityNum, const bool globalSound ) {
	const int itemIndex = state.eventParm;
	gitem_t *item = PickupItemForIndex( itemIndex );
	if ( item == nullptr ) {
		return;
	}

	centity_t *pickupEntity = PickupEventEntity( entityNum );
	if ( PickupEventDelayed( pickupEntity ) ) {
		return;
	}

	if ( globalSound ) {
		if ( item->pickup_sound ) {
			trap_S_StartSound( nullptr, cg.snap->ps.clientNum, CHAN_AUTO, trap_S_RegisterSound( item->pickup_sound, qfalse ) );
		}
	} else if ( const sfxHandle_t pickupSound = LocalItemPickupSound( *item ) ) {
		trap_S_StartSound( nullptr, state.number, CHAN_AUTO, pickupSound );
	}

	if ( state.number == cg.snap->ps.clientNum ) {
		CG_ItemPickup( itemIndex );
	}

	MarkPickupEventHandled( pickupEntity );
}

void PlayConfiguredEventSound( const entityState_t &state, const int listenerEntityNum, const int channel ) {
	if ( cgs.gameSounds[state.eventParm] ) {
		trap_S_StartSound( nullptr, listenerEntityNum, channel, cgs.gameSounds[state.eventParm] );
		return;
	}

	const char *soundName = CG_ConfigString( CS_SOUNDS + state.eventParm );
	trap_S_StartSound( nullptr, listenerEntityNum, channel, CG_CustomSound( state.number, soundName ) );
}

[[nodiscard]] bool LocalPlayerOnTeam( const team_t team ) noexcept {
	return cg.snap->ps.persistant[PERS_TEAM] == team;
}

[[nodiscard]] bool LocalPlayerHasPowerup( const powerup_t powerup ) noexcept {
	return cg.snap->ps.powerups[powerup] != 0;
}

void AddTeamAwareBufferedSound( const team_t team, const sfxHandle_t sameTeamSound, const sfxHandle_t otherTeamSound ) {
	CG_AddBufferedSound( LocalPlayerOnTeam( team ) ? sameTeamSound : otherTeamSound );
}

void HandleTeamCaptureSound( const team_t scoringTeam ) {
	AddTeamAwareBufferedSound( scoringTeam, cgs.media.captureYourTeamSound, cgs.media.captureOpponentSound );
}

void HandleTeamReturnSound( const team_t returningTeam, const sfxHandle_t returnedFlagSound ) {
	AddTeamAwareBufferedSound( returningTeam, cgs.media.returnYourTeamSound, cgs.media.returnOpponentSound );
	CG_AddBufferedSound( returnedFlagSound );
}

#ifdef MISSIONPACK
[[nodiscard]] bool IsOneFlagCtf() noexcept {
	return cgs.gametype == GT_1FCTF;
}
#endif

[[nodiscard]] bool ShouldSuppressFlagTakenAnnouncement( const powerup_t teamFlag, const powerup_t neutralFlag ) noexcept {
	return LocalPlayerHasPowerup( teamFlag ) || LocalPlayerHasPowerup( neutralFlag );
}

void HandleRedTakenSound() {
	if ( ShouldSuppressFlagTakenAnnouncement( PW_BLUEFLAG, PW_NEUTRALFLAG ) ) {
		return;
	}

	if ( LocalPlayerOnTeam( TEAM_BLUE ) ) {
#ifdef MISSIONPACK
		CG_AddBufferedSound( IsOneFlagCtf() ? cgs.media.yourTeamTookTheFlagSound : cgs.media.enemyTookYourFlagSound );
#else
		CG_AddBufferedSound( cgs.media.enemyTookYourFlagSound );
#endif
		return;
	}

	if ( LocalPlayerOnTeam( TEAM_RED ) ) {
#ifdef MISSIONPACK
		CG_AddBufferedSound( IsOneFlagCtf() ? cgs.media.enemyTookTheFlagSound : cgs.media.yourTeamTookEnemyFlagSound );
#else
		CG_AddBufferedSound( cgs.media.yourTeamTookEnemyFlagSound );
#endif
	}
}

void HandleBlueTakenSound() {
	if ( ShouldSuppressFlagTakenAnnouncement( PW_REDFLAG, PW_NEUTRALFLAG ) ) {
		return;
	}

	if ( LocalPlayerOnTeam( TEAM_RED ) ) {
#ifdef MISSIONPACK
		CG_AddBufferedSound( IsOneFlagCtf() ? cgs.media.yourTeamTookTheFlagSound : cgs.media.enemyTookYourFlagSound );
#else
		CG_AddBufferedSound( cgs.media.enemyTookYourFlagSound );
#endif
		return;
	}

	if ( LocalPlayerOnTeam( TEAM_BLUE ) ) {
#ifdef MISSIONPACK
		CG_AddBufferedSound( IsOneFlagCtf() ? cgs.media.enemyTookTheFlagSound : cgs.media.yourTeamTookEnemyFlagSound );
#else
		CG_AddBufferedSound( cgs.media.yourTeamTookEnemyFlagSound );
#endif
	}
}

#ifdef MISSIONPACK
void HandleObeliskAttackedSound( const team_t defendingTeam ) {
	if ( LocalPlayerOnTeam( defendingTeam ) ) {
		CG_AddBufferedSound( cgs.media.yourBaseIsUnderAttackSound );
	}
}
#endif

void HandleGlobalTeamSound( const int teamEvent ) {
	switch( teamEvent ) {
	case GTS_RED_CAPTURE:
		HandleTeamCaptureSound( TEAM_RED );
		break;
	case GTS_BLUE_CAPTURE:
		HandleTeamCaptureSound( TEAM_BLUE );
		break;
	case GTS_RED_RETURN:
		HandleTeamReturnSound( TEAM_RED, cgs.media.blueFlagReturnedSound );
		break;
	case GTS_BLUE_RETURN:
		HandleTeamReturnSound( TEAM_BLUE, cgs.media.redFlagReturnedSound );
		break;
	case GTS_RED_TAKEN:
		HandleRedTakenSound();
		break;
	case GTS_BLUE_TAKEN:
		HandleBlueTakenSound();
		break;
#ifdef MISSIONPACK
	case GTS_REDOBELISK_ATTACKED:
		HandleObeliskAttackedSound( TEAM_RED );
		break;
	case GTS_BLUEOBELISK_ATTACKED:
		HandleObeliskAttackedSound( TEAM_BLUE );
		break;
#endif
	case GTS_REDTEAM_SCORED:
		CG_AddBufferedSound( cgs.media.redScoredSound );
		break;
	case GTS_BLUETEAM_SCORED:
		CG_AddBufferedSound( cgs.media.blueScoredSound );
		break;
	case GTS_REDTEAM_TOOK_LEAD:
		CG_AddBufferedSound( cgs.media.redLeadsSound );
		break;
	case GTS_BLUETEAM_TOOK_LEAD:
		CG_AddBufferedSound( cgs.media.blueLeadsSound );
		break;
	case GTS_TEAMS_ARE_TIED:
		CG_AddBufferedSound( cgs.media.teamsTiedSound );
		break;
#ifdef MISSIONPACK
	case GTS_KAMIKAZE:
		trap_S_StartLocalSound( cgs.media.kamikazeFarSound, CHAN_ANNOUNCER );
		break;
#endif
	default:
		break;
	}
}

[[nodiscard]] const char *DeathSoundPath( const int event ) {
	return va( "*death%i.wav", event - EV_DEATH1 + 1 );
}

void PlayDeathEventSound( const centity_t &cent, const entityState_t &state, const int event ) {
	const char *deathSound = CG_WaterLevel( &cent ) == 3 ? "*drown.wav" : DeathSoundPath( event );
	trap_S_StartSound( nullptr, state.number, CHAN_VOICE, CG_CustomSound( state.number, deathSound ) );
}

void ActivatePowerupEvent( const entityState_t &state, const powerup_t powerup, const sfxHandle_t sound ) {
	if ( state.number == cg.snap->ps.clientNum ) {
		cg.powerupActive = powerup;
		cg.powerupTime = cg.time;
	}

	trap_S_StartSound( nullptr, state.number, CHAN_ITEM, sound );
}

#ifdef MISSIONPACK
[[nodiscard]] bool ShouldPlayGibSound( const entityState_t &state ) noexcept {
	return ( state.eFlags & EF_KAMIKAZE ) == 0;
}
#endif

void HandleGibPlayerEvent( const entityState_t &state, const vec3_t origin ) {
#ifdef MISSIONPACK
	if ( ShouldPlayGibSound( state ) ) {
		trap_S_StartSound( nullptr, state.number, CHAN_BODY, cgs.media.gibSound );
	}
#else
	trap_S_StartSound( nullptr, state.number, CHAN_BODY, cgs.media.gibSound );
#endif
	CG_GibPlayer( origin );
}

#ifdef MISSIONPACK
void PlayProximityMineStickSound( const entityState_t &state ) {
	if ( state.eventParm & SURF_FLESH ) {
		trap_S_StartSound( nullptr, state.number, CHAN_AUTO, cgs.media.wstbimplSound );
		return;
	}
	if ( state.eventParm & SURF_METALSTEPS ) {
		trap_S_StartSound( nullptr, state.number, CHAN_AUTO, cgs.media.wstbimpmSound );
		return;
	}
	trap_S_StartSound( nullptr, state.number, CHAN_AUTO, cgs.media.wstbimpdSound );
}
#endif

} // namespace

/*
===================
CG_PlaceString

Also called by scoreboard drawing
===================
*/
const char	*CG_PlaceString( int rank ) {
	static std::array<char, 64> placeString{};

	const char *prefix = "";
	if ( rank & RANK_TIED_FLAG ) {
		rank &= ~RANK_TIED_FLAG;
		prefix = "Tied for ";
	}

	if ( const char *specialPlace = SpecialPlaceString( rank ) ) {
		Com_sprintf( placeString.data(), placeString.size(), "%s%s", prefix, specialPlace );
	} else {
		Com_sprintf( placeString.data(), placeString.size(), "%s%i%s", prefix, rank, OrdinalSuffix( rank ) );
	}

	return placeString.data();
}


/*
=============
CG_Obituary
=============
*/
static void CG_Obituary( entityState_t *ent ) {
	int			mod;
	int			target, attacker;
	const char	*message;
	const char	*message2;
	const char	*targetInfo;
	const char	*attackerInfo;
	std::array<char, 32> targetName{};
	std::array<char, 32> attackerName{};
	gender_t	gender;
	clientInfo_t	*ci;
	qboolean	following;

	target = ent->otherEntityNum;
	attacker = ent->otherEntityNum2;
	mod = ent->eventParm;

	if ( target < 0 || target >= MAX_CLIENTS ) {
		CG_Error( "CG_Obituary: target out of range" );
	}
	ci = &cgs.clientinfo[target];

	if ( attacker < 0 || attacker >= MAX_CLIENTS ) {
		attacker = ENTITYNUM_WORLD;
		attackerInfo = nullptr;
	} else {
		attackerInfo = CG_ConfigString( CS_PLAYERS + attacker );
	}

	targetInfo = CG_ConfigString( CS_PLAYERS + target );
	if ( !targetInfo[0] )
	{
		return;
	}
	CopyPlayerName( targetName, targetInfo );

	following = cg.snap->ps.pm_flags & PMF_FOLLOW;

	message2 = "";

	// check for single client messages

	message = WorldObituaryMessage( mod );

	if (attacker == target) {
		gender = ci->gender;
		message = SelfObituaryMessage( mod, gender );
	}

	if ( message ) {
		CG_Printf( "%s %s.\n", targetName.data(), message );
		FollowKillerIfSpectating( attacker );
		return;
	}

	// check for kill messages from the current clientNum
	if ( attacker == cg.snap->ps.clientNum ) {
		char	*s;

		if ( cgs.gametype < GT_TEAM ) {
			s = va("You fragged %s\n%s place with %i", targetName.data(), 
				CG_PlaceString( cg.snap->ps.persistant[PERS_RANK] + 1 ),
				cg.snap->ps.persistant[PERS_SCORE] );
		} else {
			s = va("You fragged %s", targetName.data() );
		}
#ifdef MISSIONPACK
		if (!(cg_singlePlayerActive.integer && cg_cameraOrbit.integer)) {
			CG_CenterPrint( s, SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH );
		} 
#else
		CG_CenterPrint( s, SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH );
#endif

		// print the text message as well
	}

	// check for double client messages
	if ( !attackerInfo ) {
		attacker = ENTITYNUM_WORLD;
		Q_strncpyz( attackerName.data(), "noname", attackerName.size() );
	} else {
		CopyPlayerName( attackerName, attackerInfo, true );
		// check for kill messages about the current clientNum
		if ( target == cg.snap->ps.clientNum ) {
			Q_strncpyz( cg.killerName, attackerName.data(), sizeof( cg.killerName ) );
			FollowKillerIfFollowing( attacker, following );
		}
	}

	if ( attacker != ENTITYNUM_WORLD ) {
		const ObituaryMessage killMessage = KillObituaryMessage( mod );
		message = killMessage.message;
		message2 = killMessage.suffix;

		if ( message ) {
			CG_Printf( "%s %s %s%s\n", targetName.data(), message, attackerName.data(), message2 );
			FollowKillerIfSpectating( attacker );
			return;
		}
	}

	// we don't know what it was
	CG_Printf( "%s "S_COLOR_STRIP"died.\n", targetName.data() );
}
//==========================================================================


/*
===============
CG_UseItem
===============
*/
static void CG_UseItem( centity_t *cent ) {
	clientInfo_t *ci;
	int			itemNum, clientNum;
	gitem_t		*item;
	entityState_t *es;

	es = &cent->currentState;
	
	itemNum = (es->event & ~EV_EVENT_BITS) - EV_USE_ITEM0;
	if ( itemNum < 0 || itemNum > HI_NUM_HOLDABLE ) {
		itemNum = 0;
	}

	// print a message if the local player
	if ( es->number == cg.snap->ps.clientNum ) {
		if ( !itemNum ) {
			CG_CenterPrint( "No item to use", SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH );
		} else {
			item = BG_FindItemForHoldable( itemNum );
			CG_CenterPrint( va("Use %s", item->pickup_name), SCREEN_HEIGHT * 0.30, BIGCHAR_WIDTH );
		}
	}

	switch ( itemNum ) {
	default:
	case HI_NONE:
		StartEntitySound( *es, CHAN_BODY, cgs.media.useNothingSound );
		break;

	case HI_TELEPORTER:
		break;

	case HI_MEDKIT:
		clientNum = cent->currentState.clientNum;
		if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
			ci = &cgs.clientinfo[ clientNum ];
			ci->medkitUsageTime = cg.time;
		}
		StartEntitySound( *es, CHAN_BODY, cgs.media.medkitSound );
		break;

#ifdef MISSIONPACK
	case HI_KAMIKAZE:
		break;

	case HI_PORTAL:
		break;
	case HI_INVULNERABILITY:
		StartEntitySound( *es, CHAN_BODY, cgs.media.useInvulnerabilitySound );
		break;
#endif
	}
}


/*
================
CG_ItemPickup

A new item was picked up this frame
================
*/
static void CG_ItemPickup( int itemNum ) {
	static int oldItem = -1;
	
	cg.itemPickup = itemNum;
	cg.itemPickupTime = cg.time;
	cg.itemPickupBlendTime = cg.time;

	if ( oldItem != itemNum )
		cg.itemPickupCount = 1;
	else
		cg.itemPickupCount++;

	oldItem = itemNum;
	
	// see if it should be the grabbed weapon
	if ( bg_itemlist[itemNum].giType == IT_WEAPON ) {
		// select it immediately
		if ( cg_autoswitch.integer && bg_itemlist[itemNum].giTag != WP_MACHINEGUN ) {
			cg.weaponSelectTime = cg.time;
			cg.weaponSelect = bg_itemlist[itemNum].giTag;
		}
	}

}


/*
================
CG_WaterLevel

Returns waterlevel for entity origin
================
*/
static int CG_WaterLevel( const centity_t *cent ) {
	vec3_t point;
	int contents, sample1, sample2, waterlevel;
	const int viewheight = ViewheightForLegsAnimation( cent->currentState.legsAnim );

	//
	// get waterlevel, accounting for ducking
	//
	waterlevel = 0;

	point[0] = cent->lerpOrigin[0];
	point[1] = cent->lerpOrigin[1];
	point[2] = cent->lerpOrigin[2] + MINS_Z + 1;
	contents = CG_PointContents(point, -1);

	if ( contents & MASK_WATER ) {
		sample2 = viewheight - MINS_Z;
		sample1 = sample2 / 2;
		waterlevel = 1;
		point[2] = cent->lerpOrigin[2] + MINS_Z + sample1;
		contents = CG_PointContents(point, -1);

		if (contents & MASK_WATER) {
			waterlevel = 2;
			point[2] = cent->lerpOrigin[2] + MINS_Z + sample2;
			contents = CG_PointContents(point, -1);

			if (contents & MASK_WATER) {
				waterlevel = 3;
			}
		}
	}

	return waterlevel;
}

/*
================
CG_PainEvent

Also called by playerstate transition
================
*/
void CG_PainEvent( centity_t *cent, int health ) {
	// don't do more than two pain sounds a second
	if ( cg.time - cent->pe.painTime < 500 ) {
		cent->pe.painIgnore = qfalse;
		return;
	}

	if ( cent->pe.painIgnore ) {
		cent->pe.painIgnore = qfalse;
		return;
	}

	// play a gurp sound instead of a normal pain sound
	if (CG_WaterLevel(cent) == 3) {
		StartCustomEntitySound( cent->currentState, CHAN_VOICE, RandomGurpSoundPath() );
	} else {
		StartCustomEntitySound( cent->currentState, CHAN_VOICE, PainSoundPath( health ) );
	}

	// save pain time for programitic twitch animation
	cent->pe.painTime = cg.time;
	cent->pe.painDirection ^= 1;
}



/*
==============
CG_EntityEvent

An entity has an event value
also called by CG_CheckPlayerstateEvents
==============
*/
void CG_EntityEvent( centity_t *cent, vec3_t position, int entityNum ) {
	entityState_t	*es;
	int				event;
	vec3_t			dir;
	int				clientNum;
	clientInfo_t	*ci;
	vec3_t			vec;
	float			fovOffset;

	es = &cent->currentState;
	event = es->event & ~EV_EVENT_BITS;

	if ( (unsigned) event >= EV_MAX ) {
		CG_Error( "Unknown event: %i", event );
		return;
	}

	if ( cg_debugEvents.integer ) {
		CG_Printf( "ent:%3i  event:%3i %s", es->number, event, eventnames[ event ] );
	}

	if ( !event ) {
		return;
	}

	clientNum = es->clientNum;
	if ( (unsigned) clientNum >= MAX_CLIENTS ) {
		clientNum = 0;
	}
	ci = &cgs.clientinfo[ clientNum ];

	switch ( event ) {
	//
	// movement generated events
	//
	case EV_FOOTSTEP:
	case EV_FOOTSTEP_METAL:
	case EV_FOOTSPLASH:
	case EV_FOOTWADE:
	case EV_SWIM:
		PlayFootstepEvent( *es, *ci, event );
		break;

	case EV_FALL_SHORT:
		StartEntitySound( *es, CHAN_AUTO, cgs.media.landSound );
		ApplyLandingFeedback( clientNum, -8 );
		break;

	case EV_FALL_MEDIUM:
		// use normal pain sound
		StartCustomEntitySound( *es, CHAN_VOICE, "*pain100_1.wav" );
		MarkPainIgnoreWindow( *cent );	// don't play a pain sound right after this
		ApplyLandingFeedback( clientNum, -16 );
		break;

	case EV_FALL_FAR:
		StartCustomEntitySound( *es, CHAN_AUTO, "*fall1.wav" );
		MarkPainIgnoreWindow( *cent );	// don't play a pain sound right after this
		ApplyLandingFeedback( clientNum, -24 );
		break;

	case EV_STEP_4:
	case EV_STEP_8:
	case EV_STEP_12:
	case EV_STEP_16:		// smooth out step up transitions
	{
		float	oldStep;
		int		delta;
		int		step;

		if ( clientNum != cg.predictedPlayerState.clientNum ) {
			break;
		}
		// if we are interpolating, we don't need to smooth steps
		if ( cg.demoPlayback || (cg.snap->ps.pm_flags & PMF_FOLLOW) ||
			cg_nopredict.integer || cgs.synchronousClients ) {
			break;
		}
		// check for stepping up before a previous step is completed
		delta = cg.time - cg.stepTime;
		if (delta < STEP_TIME) {
			oldStep = cg.stepChange * (STEP_TIME - delta) / STEP_TIME;
		} else {
			oldStep = 0;
		}

		// add this amount
		step = 4 * (event - EV_STEP_4 + 1 );
		cg.stepChange = oldStep + step;
		if ( cg.stepChange > MAX_STEP_CHANGE ) {
			cg.stepChange = MAX_STEP_CHANGE;
		}
		cg.stepTime = cg.time;
		break;
	}

	case EV_JUMP_PAD:
//		CG_Printf( "EV_JUMP_PAD w/effect #%i\n", es->eventParm );
		{
			vec3_t			up = {0, 0, 1};


			CG_SmokePuff( cent->lerpOrigin, up, 
						  32, 
						  1, 1, 1, 0.33f,
						  1000, 
						  cg.time, 0,
						  LEF_PUFF_DONT_SCALE, 
						  cgs.media.smokePuffShader );
		}

		// boing sound at origin, jump sound on player
		trap_S_StartSound ( cent->lerpOrigin, -1, CHAN_VOICE, cgs.media.jumpPadSound );
		trap_S_StartSound( nullptr, es->number, CHAN_VOICE, CG_CustomSound( es->number, "*jump1.wav" ) );
		break;

	case EV_JUMP:
		// pain event with fast sequential jump just creates sound distortion
		if ( cg.time - cent->pe.painTime > 50 )
			trap_S_StartSound( nullptr, es->number, CHAN_VOICE, CG_CustomSound( es->number, "*jump1.wav" ) );
		break;

	case EV_TAUNT:
		trap_S_StartSound( nullptr, es->number, CHAN_VOICE, CG_CustomSound( es->number, "*taunt.wav" ) );
		break;

#ifdef MISSIONPACK
	case EV_TAUNT_YES:
		CG_VoiceChatLocal(SAY_TEAM, qfalse, es->number, COLOR_CYAN, VOICECHAT_YES);
		break;

	case EV_TAUNT_NO:
		CG_VoiceChatLocal(SAY_TEAM, qfalse, es->number, COLOR_CYAN, VOICECHAT_NO);
		break;

	case EV_TAUNT_FOLLOWME:
		CG_VoiceChatLocal(SAY_TEAM, qfalse, es->number, COLOR_CYAN, VOICECHAT_FOLLOWME);
		break;

	case EV_TAUNT_GETFLAG:
		CG_VoiceChatLocal(SAY_TEAM, qfalse, es->number, COLOR_CYAN, VOICECHAT_ONGETFLAG);
		break;

	case EV_TAUNT_GUARDBASE:
		CG_VoiceChatLocal(SAY_TEAM, qfalse, es->number, COLOR_CYAN, VOICECHAT_ONDEFENSE);
		break;

	case EV_TAUNT_PATROL:
		CG_VoiceChatLocal(SAY_TEAM, qfalse, es->number, COLOR_CYAN, VOICECHAT_ONPATROL);
		break;
#endif
	case EV_WATER_TOUCH:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.watrInSound );
		break;

	case EV_WATER_LEAVE:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.watrOutSound );
		break;

	case EV_WATER_UNDER:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.watrUnSound );
		break;

	case EV_WATER_CLEAR:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, CG_CustomSound( es->number, "*gasp.wav" ) );
		break;

	case EV_ITEM_PICKUP:
		HandleItemPickupEvent( *es, entityNum, false );
		break;

	case EV_GLOBAL_ITEM_PICKUP:
		HandleItemPickupEvent( *es, entityNum, true );
		break;

	//
	// weapon events
	//
	case EV_NOAMMO:
//		trap_S_StartSound (NULL, es->number, CHAN_AUTO, cgs.media.noAmmoSound );
		if ( es->number == cg.snap->ps.clientNum ) {
			CG_OutOfAmmoChange();
		}
		break;

	case EV_CHANGE_WEAPON:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.selectSound );
		break;

	case EV_FIRE_WEAPON:
		CG_FireWeapon( cent );
		break;

	case EV_USE_ITEM0:
	case EV_USE_ITEM1:
	case EV_USE_ITEM2:
	case EV_USE_ITEM3:
	case EV_USE_ITEM4:
	case EV_USE_ITEM5:
	case EV_USE_ITEM6:
	case EV_USE_ITEM7:
	case EV_USE_ITEM8:
	case EV_USE_ITEM9:
	case EV_USE_ITEM10:
	case EV_USE_ITEM11:
	case EV_USE_ITEM12:
	case EV_USE_ITEM13:
	case EV_USE_ITEM14:
	case EV_USE_ITEM15:
		CG_UseItem( cent );
		break;

	//=================================================================

	//
	// other events
	//
	case EV_PLAYER_TELEPORT_IN:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.teleInSound );
		CG_SpawnEffect( position);
		break;

	case EV_PLAYER_TELEPORT_OUT:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.teleOutSound );
		CG_SpawnEffect(  position);
		break;

	case EV_ITEM_POP:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.respawnSound );
		break;

	case EV_ITEM_RESPAWN:
		cent->miscTime = cg.time;	// scale up from this
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.respawnSound );
		break;

	case EV_GRENADE_BOUNCE:
		if ( rand() & 1 ) {
			trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.hgrenb1aSound );
		} else {
			trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.hgrenb2aSound );
		}
		break;

#ifdef MISSIONPACK
	case EV_PROXIMITY_MINE_STICK:
		PlayProximityMineStickSound( *es );
		break;

	case EV_PROXIMITY_MINE_TRIGGER:
		trap_S_StartSound( nullptr, es->number, CHAN_AUTO, cgs.media.wstbactvSound );
		break;

	case EV_KAMIKAZE:
		CG_KamikazeEffect( cent->lerpOrigin );
		break;

	case EV_OBELISKEXPLODE:
		CG_ObeliskExplode( cent->lerpOrigin, es->eventParm );
		break;

	case EV_OBELISKPAIN:
		CG_ObeliskPain( cent->lerpOrigin );
		break;

	case EV_INVUL_IMPACT:
		CG_InvulnerabilityImpact( cent->lerpOrigin, cent->currentState.angles );
		break;

	case EV_JUICED:
		CG_InvulnerabilityJuiced( cent->lerpOrigin );
		break;

	case EV_LIGHTNINGBOLT:
		CG_LightningBoltBeam(es->origin2, es->pos.trBase);
		break;
#endif

	case EV_SCOREPLUM:
		CG_ScorePlum( cent->currentState.otherEntityNum, cent->lerpOrigin, cent->currentState.time );
		break;

	//
	// missile impacts
	//
	case EV_MISSILE_HIT:
		ByteToDir( es->eventParm, dir );
		CG_MissileHitPlayer( es->weapon, position, dir, es->otherEntityNum );
		break;

	case EV_MISSILE_MISS:
		ByteToDir( es->eventParm, dir );
		CG_MissileHitWall( es->weapon, 0, position, dir, IMPACTSOUND_DEFAULT );
		break;

	case EV_MISSILE_MISS_METAL:
		ByteToDir( es->eventParm, dir );
		CG_MissileHitWall( es->weapon, 0, position, dir, IMPACTSOUND_METAL );
		break;

	case EV_RAILTRAIL:
		cent->currentState.weapon = WP_RAILGUN;

		if ( cent->currentState.clientNum == cg.snap->ps.clientNum && !cg_thirdPerson.integer ) 
		{
			VectorCopy( cg.refdef.vieworg, vec );
			fovOffset = -0.2f * ( cgs.fov - 90.0f );

			// 13.5, -5.5, -6.0
			VectorMA( vec, cg_gun_x.value + 13.5f, cg.refdef.viewaxis[0], vec );
			VectorMA( vec, cg_gun_y.value - 5.5f, cg.refdef.viewaxis[1], vec );
			VectorMA( vec, cg_gun_z.value + fovOffset - 6.0f, cg.refdef.viewaxis[2], vec );
		}
		else
			VectorCopy( es->origin2, vec );

		// if the end was on a nomark surface, don't make an explosion
		CG_RailTrail( ci, vec, es->pos.trBase );

		if ( es->eventParm != 255 ) {
			ByteToDir( es->eventParm, dir );
			CG_MissileHitWall( es->weapon, es->clientNum, position, dir, IMPACTSOUND_DEFAULT );
		}
		break;

	case EV_BULLET_HIT_WALL:
		ByteToDir( es->eventParm, dir );
		CG_Bullet( es->pos.trBase, es->otherEntityNum, dir, qfalse, ENTITYNUM_WORLD );
		break;

	case EV_BULLET_HIT_FLESH:
		CG_Bullet( es->pos.trBase, es->otherEntityNum, dir, qtrue, es->eventParm );
		break;

	case EV_SHOTGUN:
		CG_ShotgunFire( es );
		break;

	case EV_GENERAL_SOUND:
		PlayConfiguredEventSound( *es, es->number, CHAN_VOICE );
		break;

	case EV_GLOBAL_SOUND:	// play from the player's head so it never diminishes
		PlayConfiguredEventSound( *es, cg.snap->ps.clientNum, CHAN_AUTO );
		break;

	case EV_GLOBAL_TEAM_SOUND:	// play from the player's head so it never diminishes
		HandleGlobalTeamSound( es->eventParm );
		break;

	case EV_PAIN:
		// local player sounds are triggered in CG_CheckLocalSounds,
		// so ignore events on the player
		if ( cent->currentState.number != cg.snap->ps.clientNum ) {
			CG_PainEvent( cent, es->eventParm );
		}
		break;

	case EV_DEATH1:
	case EV_DEATH2:
	case EV_DEATH3:
		PlayDeathEventSound( *cent, *es, event );
		break;

	case EV_OBITUARY:
		CG_Obituary( es );
		break;

	//
	// powerup events
	//
	case EV_POWERUP_QUAD:
		ActivatePowerupEvent( *es, PW_QUAD, cgs.media.quadSound );
		break;

	case EV_POWERUP_BATTLESUIT:
		ActivatePowerupEvent( *es, PW_BATTLESUIT, cgs.media.protectSound );
		break;

	case EV_POWERUP_REGEN:
		ActivatePowerupEvent( *es, PW_REGEN, cgs.media.regenSound );
		break;

	case EV_GIB_PLAYER:
		HandleGibPlayerEvent( *es, cent->lerpOrigin );
		break;

	case EV_STOPLOOPINGSOUND:
		trap_S_StopLoopingSound( es->number );
		es->loopSound = 0;
		break;

	case EV_DEBUG_LINE:
		CG_Beam( cent );
		break;

	case EV_PROXIMITY_MINE_STICK:
	case EV_PROXIMITY_MINE_TRIGGER:
		break;

	default:
		CG_Error( "Unknown event: %i", event );
		break;
	}
}


/*
==============
CG_CheckEvents

==============
*/
void CG_CheckEvents( centity_t *cent ) {
	// check for event-only entities
	if ( cent->currentState.eType > ET_EVENTS ) {
		if ( cent->previousEvent ) {
			return;	// already fired
		}
		// if this is a player event set the entity number of the client entity number
		if ( cent->currentState.eFlags & EF_PLAYER_EVENT ) {
			cent->currentState.number = cent->currentState.otherEntityNum;
		}

		cent->previousEvent = 1;

		cent->currentState.event = cent->currentState.eType - ET_EVENTS;
	} else {
		// check for events riding with another entity
		if ( cent->currentState.event == cent->previousEvent ) {
			return;
		}
		cent->previousEvent = cent->currentState.event;
		if ( ( cent->currentState.event & ~EV_EVENT_BITS ) == 0 ) {
			return;
		}
	}

	// calculate the position at exactly the frame time
	BG_EvaluateTrajectory( &cent->currentState.pos, cg.snap->serverTime, cent->lerpOrigin );
	CG_SetEntitySoundPosition( cent );

	CG_EntityEvent( cent, cent->lerpOrigin, -1 );
}
