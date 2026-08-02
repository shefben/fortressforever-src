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
#include "nav_area.h"
#include "Player/NextBotPlayerBody.h"
#include "NextBotVisionInterface.h"
#include "NextBotIntentionInterface.h"
#include "tier1/utlvector.h"

//-----------------------------------------------------------------------------
// CFFBotLocomotion — the locomotion interface, and deliberately almost empty.
//
// It exists because movement WAS being done in the behaviour layer, and that
// is the wrong place for it. Compare CTFBotLocomotion, which is 130 lines and
// whose entire contribution to movement is "crouch while airborne": everything
// else — obstacle avoidance, ledge climbing, gap jumping, ladders — belongs to
// PathFollower and PlayerLocomotion, which already do all of it, correctly,
// with knowledge of the path the bot is actually on.
//
// Every time a per-tick behaviour reached in and pressed a movement button of
// its own, it was competing with those systems rather than helping them: two
// independent opinions about when to jump produce a bot that jumps twice, and
// a goal position written straight into Approach() skips the obstacle steering
// that would have taken it round the corner it is now walking into.
//-----------------------------------------------------------------------------
class CFFBotLocomotion : public PlayerLocomotion
{
public:
	DECLARE_CLASS( CFFBotLocomotion, PlayerLocomotion );

	CFFBotLocomotion( INextBot *bot ) : PlayerLocomotion( bot ) {}
	virtual ~CFFBotLocomotion() {}

	// Crouch-jump, always. Mirrors CTFBotLocomotion::Update.
	//
	// The extra 18 units of a duck-jump is not a bonus, it is the height
	// PathFollower's climb and gap logic ASSUMES the bot has. Without it a bot
	// that decides a ledge is reachable jumps, falls short, lands, decides it
	// is reachable, and jumps again — which is what "hopping around randomly"
	// in front of a ledge looks like from outside.
	virtual void Update( void ) OVERRIDE;

	// Blocked areas and enemy spawn rooms, and nothing else. Same test as
	// CTFBotLocomotion::IsAreaTraversable. Kept in step with CFFBotPathCost,
	// which calls this before applying any cost of its own.
	virtual bool IsAreaTraversable( const CNavArea *baseArea ) const OVERRIDE;
};


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

	// ---- Squad role ------------------------------------------------------
	//
	// FFBotRoleType (offense / defense / support), assigned by the per-team
	// quota in FFBotGameMode::AssignRoles rather than chosen by the bot.
	//
	// It has to be a quota. Left to individual preference every bot picks the
	// same answer — go and take the thing — and a team of eight attackers loses
	// every attack/defend map regardless of how good its navigation is. The
	// quota comes from the detected game mode; who fills it comes from class
	// affinity and proximity to whatever is being defended.
	//
	// m_roleAssignTime gates reassignment (FFBOT_ROLE_MIN_HOLD) so a death on
	// the far side of the map doesn't reshuffle the whole team.
	unsigned char	m_botRole;
	float			m_roleAssignTime;

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

	// How long to stand still and let a teammate through. Randomised on start
	// so two bots facing each other in a doorway don't resume in lockstep.
	CountdownTimer	m_yieldTimer;
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

	// ---- Movement arbiter — SINGLE MOVEMENT AUTHORITY --------------------
	//
	// PlayerLocomotion::Approach decomposes (goal - feet) into *view-relative*
	// button presses, and NextBotPlayer::Upkeep collapses IN_FORWARD+IN_BACK
	// to IN_FORWARD (see NextBotPlayer.h — it's an `else if`). Consequence:
	// any recovery that presses IN_BACK while an action's PathFollower is
	// still calling Approach() in the same tick is silently cancelled, and
	// the bot pushes *harder* into whatever it is wedged on.
	//
	// So recovery no longer presses movement buttons at all. It publishes a
	// WORLD-SPACE point here. While an override is live:
	//   - every action's PathFollower is suppressed (FFBotHelpers::CanDrivePath)
	//   - CFFBotMainAction::DriveMovementArbiter issues exactly ONE
	//     locomotor->Approach() at the override point
	// One Approach per tick means nothing can cancel anything.
	Vector			m_moveOverridePos;
	CountdownTimer	m_moveOverrideTimer;
	const char *	m_moveOverrideReason;	// static string literal; for bot_show_path

	// Tick on which an action's PathFollower already issued its Approach().
	// The behavior tree updates children BEFORE parents, so by the time
	// CFFBotMainAction publishes an override the child may already have moved
	// this tick. The arbiter therefore skips its own Approach when this equals
	// the current tick, and the override takes effect on the next one. That
	// preserves the invariant that matters: exactly one Approach() per tick.
	int				m_pathDrivenTick;

	void			SetMoveOverride( const Vector &worldPos, float duration, const char *reason );
	void			ClearMoveOverride( void );
	bool			IsMoveOverrideActive( void ) const;
	const Vector &	GetMoveOverridePos( void ) const { return m_moveOverridePos; }
	const char *	GetMoveOverrideReason( void ) const { return m_moveOverrideReason; }

	// Last path goal published by whichever action owned the path this tick.
	// This is the ONLY source of "which way is my path going" available to the
	// aim driver: PathFollower::Update calls ILocomotion::FaceTowards, which is
	// an empty stub for player bots (only NextBotGroundLocomotion overrides it),
	// so nothing ever aimed these bots along their own route.
	Vector			m_pathGoalPos;
	float			m_pathGoalTime;
	void			NotePathGoal( const Vector &pos );
	bool			GetPathGoal( Vector *out ) const;	// false if stale (>0.5s)

	// Set by the path driver when the next path segment turns sharply or is a
	// climb/jump discontinuity. Gates bunny-hopping — hopping into a corner
	// with view-relative air control is how bots curve into walls.
	bool			m_pathTurnAhead;

	// Door interaction. When the path is blocked by an *openable* entity we
	// walk into it and hold +use, and suppress stuck recovery for that window.
	// The old recovery did the opposite: it pressed IN_BACK, which backs the
	// bot out of the door's own trigger volume, so the door never opened.
	EHANDLE			m_blockingDoor;
	CountdownTimer	m_doorPushTimer;

	// Stuck escalation ladder. Stages escalate at the *planner* level rather
	// than mashing more buttons. See CFFBotMainAction::HandleStuckState.
	//   0 = not stuck (let PathFollower::Avoid work)
	//   1 = penalize the area we're entering, force repath
	//   2 = back-track in world space to the last spot we had velocity
	//   3 = abandon the goal
	//   4 = teleport (floor)
	int				m_stuckStage;
	float			m_stuckStageTime;
	Vector			m_lastGoodPos;		// last position where we had real velocity
	float			m_lastGoodPosTime;

	// Set by stuck recovery, read by actions: "drop your goal and pick a new
	// one". Cleared by the action that consumes it.
	bool			m_abandonGoalRequest;

	// Per-bot debug overlays (bot_show_path / bot_show_threat).
	bool			m_debugShowPath;
	bool			m_debugShowThreat;

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

	// Reaction-time + aim-error tracking.
	//   m_currentThreatId       — entindex of latest primary threat (-1 = none)
	//   m_threatFirstSeenTime   — when we first acquired *any* threat in
	//                              the current engagement. Persists across
	//                              threat swaps within the engagement so a
	//                              multi-enemy scene doesn't reset the gate
	//                              every time vision picks a different
	//                              primary; only resets after a sustained
	//                              threatless window (m_lastThreatlessTime).
	//   m_lastThreatlessTime    — first frame we had no threat. Resets the
	//                              engagement clock once it exceeds 1.5s
	//                              (real disengage, not vision blip).
	int				m_currentThreatId;
	float			m_threatFirstSeenTime;
	float			m_lastThreatlessTime;

	// Difficulty (0..3) — read once per Spawn() from ff_bot_difficulty
	// cvar so a bot's difficulty stays stable across a life. Drives
	// reaction-time floor and aim-error magnitude.
	int				m_difficulty;

	// Per-bot deterministic jitter, applied on top of the difficulty
	// baseline so two bots at the same difficulty don't react identically.
	// Set once at construction; range [0, 1).
	float			m_reactionJitter;

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

	// Reaction-time floor in seconds — how long after first sighting
	// the bot is allowed to open fire on a fresh threat. Difficulty-
	// scaled; per-bot deterministic jitter on top.
	//   easy   ~400ms
	//   normal ~250ms
	//   hard   ~150ms
	//   expert ~80ms
	float			GetReactionTimeFloor( void ) const;

	// True if our reaction-time gate has elapsed for the given threat —
	// i.e., we're past GetReactionTimeFloor() seconds since we first
	// saw this enemy. Returns true if no threat or threat is stale
	// (always allow non-aim-blocking work to continue).
	bool			HasReactedToThreat( const class CKnownEntity *threat );

	// Apply Gaussian-ish aim error to a target world-space point. Noise
	// magnitude scales with difficulty (lower diff = wider error), range
	// (farther target = wider error), and hold-time (the longer we've
	// been on this threat, the tighter our aim). Pass 0 holdTime when
	// calling fresh.
	Vector			ApplyAimError( const Vector &targetPos, float holdTimeSec ) const;

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

// Keeps the last ff_nav_visualize view drawn. Lives in ff_bot_commands.cpp;
// declared here because FFBotManager_Tick is the only per-frame hook the bot
// subsystem has and everything periodic hangs off it.
void FFBotCommands_TickVisualization( void );

// Per-frame manager hook — called from Bot_RunAll. Drives periodic autobalance
// (every 30s, moves a bot from the largest team to the smallest if the gap is
// > 1) and any other future bot-side global concerns.
void FFBotManager_Tick( void );

#endif // FF_BOT_H
