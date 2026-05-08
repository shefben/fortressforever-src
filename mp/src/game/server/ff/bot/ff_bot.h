//========= Fortress Forever Bot =============================================//
//
// CFFBot — NextBot-based fake-client bot.
// Phase 3: spawns, ticks, and walks to random nav areas via PathFollower.
//
//===========================================================================//

#ifndef FF_BOT_H
#define FF_BOT_H
#ifdef _WIN32
#pragma once
#endif

#include "ff_player.h"
#include "Player/NextBotPlayer.h"
#include "Player/NextBotPlayerLocomotion.h"
#include "Player/NextBotPlayerBody.h"
#include "NextBotVisionInterface.h"
#include "NextBotIntentionInterface.h"
#include "tier1/utlvector.h"

class CFFBot : public NextBotPlayer< CFFPlayer >
{
public:
	DECLARE_CLASS( CFFBot, NextBotPlayer< CFFPlayer > );

	CFFBot();
	virtual ~CFFBot();

	// Required by NextBotCreatePlayerBot<T>; uses CBasePlayer::s_PlayerEdict
	// which we can access since CFFBot inherits from CBasePlayer.
	static CBasePlayer *AllocatePlayerEntity( edict_t *pEdict, const char *playerName );

	virtual void			Spawn( void ) OVERRIDE;

	// Required so the bot's behavior tree updates while dead — we need this
	// for the dead → alive transition logic in CFFBotCtfObjective::Update,
	// which sets m_needsAngleSnap so the bot's view aligns with the path on
	// respawn. Default IsDormantWhenDead = true means Update doesn't run
	// while dead, so the transition never fires and bots respawn aimed at
	// whatever angle the spawn entity had — usually the wrong way out the
	// door. Matches TFBot's pattern (CTFBot::IsDormantWhenDead returns
	// false in tf_bot.cpp:2381).
	virtual bool			IsDormantWhenDead( void ) const OVERRIDE { return false; }

	// Set in Spawn() to remember the desired exit direction and origin.
	// The main action keeps the body aimed there briefly so
	// PlayerLocomotion::Approach reads view-aligned-with-path instead of the
	// mapper-authored spawn angles.
	Vector			m_spawnExitDir;
	Vector			m_spawnExitStartPos;
	CountdownTimer	m_spawnExitForceTimer;

	// Class-specialty per-life state (consumed by ff_bot_class.cpp).
	bool			m_bClassDidSpawnInit;
	CountdownTimer	m_classBuildTimer;		// engineer / demoman: throttle build attempts
	CountdownTimer	m_classDisguiseTimer;	// spy: throttle re-disguise attempts
	CountdownTimer	m_classCloakTimer;		// spy: throttle cloak/uncloak decisions
	CountdownTimer	m_calloutTimer;			// any class: SaveMe / EngyMe spam control
	CountdownTimer	m_weaponSwitchTimer;	// throttle weapon-switch attempts
	CountdownTimer	m_retreatTimer;			// re-eval low-HP retreat throttle
	CountdownTimer	m_resupplyCheckTimer;	// throttle GetAmmo/GetHealth IsPossible scans

	// Grenade priming state. m_grenadePrimeStart > 0 means we've started a
	// secondary-grenade prime and should throw at curtime - start >= COOK.
	float			m_grenadePrimeStart;
	CountdownTimer	m_grenadeCooldownTimer;	// minimum gap between grenade throws

	// Last-known-threat memory: held for ~8s after losing sight so the bot
	// keeps watching the spot where the enemy was instead of staring at the
	// floor. m_lastThreatTime == 0 means "never seen a threat this life".
	float			m_lastThreatTime;
	Vector			m_lastThreatPos;

	// Mobility: bunny-hop cadence + path-recovery escalation.
	CountdownTimer	m_bunnyHopTimer;	// next jump-on-the-run pulse
	float			m_lastUnstuckTime;	// last time we were verified moving — used for "stuck for too long" teleport

	// Stuck-recovery state. When severely stuck:
	//   m_pathInhibitTimer suspends the contained action's path follower
	//     so HandleStuckState's direct button presses aren't immediately
	//     overwritten by locomotor.Approach() pushing forward.
	//   m_recentStuckPos / m_recentStuckExpireTime are read by the bot's
	//     own CFFBotPathCost: areas within 256u of the stuck spot get a
	//     flat penalty so the next path routes around.
	//   m_lookAroundUntil drives a head-scan when stuck so the bot turns
	//     to find an exit instead of staring at the wall it's wedged on.
	CountdownTimer	m_pathInhibitTimer;
	Vector			m_recentStuckPos;
	float			m_recentStuckExpireTime;
	float			m_lookAroundUntil;

	// Route entropy seed — random per-bot value mixed into path cost to
	// give different bots different shortest paths even when going to the
	// same goal. Set once at construction; doesn't reset on respawn.
	unsigned int	m_routeSeed;

	// Coarse per-bot route preference. Set once at construction; combines
	// with m_routeSeed to push different bots through structurally
	// different routes (e.g. underwater sewers vs. main bridge on 2fort).
	//   0 = DRY     — penalize water areas
	//   1 = WATER   — discount water areas (so this bot takes the sewer
	//                  even when the bridge is shorter)
	//   2 = NEUTRAL — no water bias
	enum RouteFlavor
	{
		ROUTE_FLAVOR_DRY     = 0,
		ROUTE_FLAVOR_WATER   = 1,
		ROUTE_FLAVOR_NEUTRAL = 2,
		ROUTE_FLAVOR_COUNT   = 3,
	};
	unsigned char	m_routeFlavor;

	// Last route-key-area visited — bot remembers which choke they took
	// last time and biases away from it next time so the team spreads
	// across multiple lanes over time.
	unsigned int	m_lastRouteChokeID;

	// Sniper charge/release firing state. Sniper rifle fires on attack-
	// release after a charge period; the default per-tick fire-press never
	// releases. Lives on the bot rather than the action because both
	// MainAction (firing) and the class driver may want to read it.
	enum SniperFireState
	{
		SNIPER_FIRE_IDLE,
		SNIPER_FIRE_CHARGING,
		SNIPER_FIRE_COOLDOWN,
	};
	SniperFireState	m_sniperFireState;
	float			m_sniperFireStartTime;

	// INextBot interface — return our owned components.
	virtual PlayerLocomotion *GetLocomotionInterface( void ) const OVERRIDE { return m_locomotor; }
	virtual PlayerBody       *GetBodyInterface( void )       const OVERRIDE { return m_body; }
	virtual IVision          *GetVisionInterface( void )     const OVERRIDE { return m_vision; }
	virtual IIntention       *GetIntentionInterface( void )  const OVERRIDE { return m_intention; }

	// Combat helpers — class-aware engagement ranges. Mirrors TFBot's
	// GetDesiredAttackRange / GetMaxAttackRange. Used by Attack action to
	// decide when to stop chasing and when to consider a threat "in range".
	float			GetDesiredAttackRange( void ) const;
	float			GetMaxAttackRange( void ) const;

	// Path-follower lookahead. Larger bots / heavier classes need a longer
	// lookahead to avoid corner-cutting through walls.
	float			GetDesiredPathLookAheadRange( void ) const;

	// Equip the best weapon we have for the given threat. Wraps
	// FFBotWeapon::TrySwitchToPreferred with the threat's range.
	bool			EquipBestWeaponForThreat( const CKnownEntity *threat );

	// True if a hitscan from our eye reaches `where` without being blocked
	// by world geometry. Filters out friendlies and the bot itself.
	bool			IsLineOfFireClear( const Vector &where ) const;
	bool			IsLineOfFireClear( CBaseEntity *who ) const;
	bool			IsLineOfFireClear( const Vector &from, const Vector &to ) const;

	// Pseudo-random value [0,1] that stays consistent within a `period`-second
	// window but changes unpredictably each window. Used by behaviors that
	// want to occasionally pick a different action without thrashing —
	// e.g., circle-strafe direction. Mirrors CTFBot::TransientlyConsistentRandomValue.
	float			TransientlyConsistentRandomValue( float period = 10.0f, int seedValue = 0 ) const;

	// Ammo state — used by GetAmmo behavior and by path-cost heuristics.
	// IsAmmoLow: active weapon has empty clip with <20 reserve, OR engineer
	// with <50 cells (gates SG repair/upgrade).
	// IsAmmoFull: no ammo type has room to grow.
	bool			IsAmmoLow( void ) const;
	bool			IsAmmoFull( void ) const;

private:
	PlayerLocomotion *m_locomotor;
	PlayerBody       *m_body;
	IVision          *m_vision;
	IIntention       *m_intention;
};

// Factory entry point used by ff_bot_temp.cpp's BotPutInServer.
// Returns the new bot's CBasePlayer*, or NULL on failure.
CBasePlayer *CreateFFBot( bool bFrozen, int iTeam, int iClass, const char *pszCustomName );

// Per-frame manager hook — called from Bot_RunAll. Drives periodic autobalance
// (every 30s, moves a bot from the largest team to the smallest if the gap is
// > 1) and any other future bot-side global concerns.
void FFBotManager_Tick( void );

#endif // FF_BOT_H
