//========= Fortress Forever Bot =============================================//
//
// CFFBot — TFBot-based fake-client adapter for FF.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_main_action.h"
#include "ff_bot_vision.h"
#include "ff_bot_body.h"
#include "ff_bot_helpers.h"
#include "ff_bot_intel.h"
#include "ff_bot_weapon.h"
#include "ff_bot_learned_links.h"
#include "ff_nav_builder.h"
#include "ff_bot_lua_objectives.h"
#include "ff_bot_gamemode.h"
#include "ff_bot_hazard.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_player.h"
#include "ff_team.h"
#include "ff_utils.h"
#include "ff_gamerules.h"
#include "ff_weapon_base.h"
#include "ammodef.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "NextBotKnownEntity.h"

#include <algorithm>
#include <math.h>

#include "NextBotManager.h"
#include "NextBotBehavior.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( ff_bot, CFFBot );


// Server-wide difficulty for all bots. Re-read on each respawn so admins
// can dial difficulty live without restarting.
ConVar ff_bot_difficulty( "ff_bot_difficulty", "1", FCVAR_NONE,
	"Bot difficulty: 0=easy, 1=normal, 2=hard, 3=expert. "
	"Affects reaction time, aim accuracy, and tactical aggression." );


//-----------------------------------------------------------------------------
// CFFBotIntention — owns the root Behavior. In Phase 3, the root action is
// a simple Wander; later phases will replace it with a class/mode-aware
// behavior selector.
//-----------------------------------------------------------------------------
class CFFBotIntention : public IIntention
{
public:
	CFFBotIntention( CFFBot *me ) : IIntention( me )
	{
		m_behavior = new Behavior< CFFBot >( new CFFBotMainAction, "FFBotIntention" );
	}

	virtual ~CFFBotIntention()
	{
		delete m_behavior;
	}

	virtual void Reset( void ) OVERRIDE
	{
		IIntention::Reset();
		delete m_behavior;
		m_behavior = new Behavior< CFFBot >( new CFFBotMainAction, "FFBotIntention" );
	}

	virtual void Update( void ) OVERRIDE
	{
		m_behavior->Update( static_cast< CFFBot * >( GetBot() ), GetUpdateInterval() );
	}

	virtual INextBotEventResponder *FirstContainedResponder( void ) const OVERRIDE { return m_behavior; }
	virtual INextBotEventResponder *NextContainedResponder( INextBotEventResponder *current ) const OVERRIDE { return NULL; }

private:
	Behavior< CFFBot > *m_behavior;
};


//-----------------------------------------------------------------------------
CFFBot::CFFBot()
{
	// Base NextBot components. We'll subclass these in later phases for class-aware
	// locomotion (scout speed, hwguy spinup), spy-disguise filtering, etc.
	m_locomotor = new PlayerLocomotion( this );
	m_body      = new CFFBotBody( this );
	m_vision    = new CFFBotVision( this );
	m_intention = new CFFBotIntention( this );

	m_bClassDidSpawnInit = false;
	m_botRole = FFROLE_OFFENSE;
	m_roleAssignTime = 0.0f;
	m_sniperFireState = SNIPER_FIRE_IDLE;
	m_sniperFireStartTime = 0.0f;
	m_routeSeed = (unsigned int)RandomInt( 1, 65535 );
	m_routeFlavor = (unsigned char)RandomInt( 0, ROUTE_FLAVOR_COUNT - 1 );
	m_lastRouteChokeID = 0;
	m_currentThreatId = -1;
	m_threatFirstSeenTime = 0.0f;
	m_lastThreatlessTime = 0.0f;
	m_difficulty = 1;
	m_reactionJitter = RandomFloat( 0.0f, 1.0f );
	m_spawnExitDir.Init();
	m_spawnExitStartPos.Init();
	m_spawnExitForceTimer.Invalidate();

	m_moveOverridePos.Init();
	m_moveOverrideTimer.Invalidate();
	m_moveOverrideReason = NULL;
	m_pathDrivenTick = -1;
	m_pathGoalPos.Init();
	m_pathGoalTime = 0.0f;
	m_pathTurnAhead = false;
	m_blockingDoor = NULL;
	m_doorPushTimer.Invalidate();
	m_stuckStage = 0;
	m_stuckStageTime = 0.0f;
	m_lastGoodPos.Init();
	m_lastGoodPosTime = 0.0f;
	m_abandonGoalRequest = false;
	m_debugShowPath = false;
	m_debugShowThreat = false;
}

//-----------------------------------------------------------------------------
// Movement arbiter. See the long comment in ff_bot.h — the whole point is
// that exactly one Approach() runs per tick, so a recovery push cannot be
// cancelled by a PathFollower re-pressing IN_FORWARD.
//-----------------------------------------------------------------------------
void CFFBot::SetMoveOverride( const Vector &worldPos, float duration, const char *reason )
{
	m_moveOverridePos = worldPos;
	m_moveOverrideTimer.Start( duration );
	m_moveOverrideReason = reason;
}

void CFFBot::ClearMoveOverride( void )
{
	m_moveOverrideTimer.Invalidate();
	m_moveOverrideReason = NULL;
}

bool CFFBot::IsMoveOverrideActive( void ) const
{
	return m_moveOverrideTimer.HasStarted() && !m_moveOverrideTimer.IsElapsed();
}

//-----------------------------------------------------------------------------
void CFFBot::NotePathGoal( const Vector &pos )
{
	m_pathGoalPos = pos;
	m_pathGoalTime = gpGlobals->curtime;
}

bool CFFBot::GetPathGoal( Vector *out ) const
{
	// Half a second of tolerance: long enough to survive a throttled behavior
	// update, short enough that we never aim at a goal from a dead path.
	if ( m_pathGoalTime <= 0.0f || ( gpGlobals->curtime - m_pathGoalTime ) > 0.5f )
		return false;
	if ( out )
		*out = m_pathGoalPos;
	return true;
}

//-----------------------------------------------------------------------------
CFFBot::~CFFBot()
{
	if ( m_intention ) { delete m_intention; m_intention = NULL; }
	if ( m_locomotor ) { delete m_locomotor; m_locomotor = NULL; }
	if ( m_body )      { delete m_body;      m_body      = NULL; }
	if ( m_vision )    { delete m_vision;    m_vision    = NULL; }
}

//-----------------------------------------------------------------------------
void CFFBot::Spawn( void )
{
	// Adaptive role reassignment: before the player Spawn() picks our
	// loadout, swap our class if the team has lost a key role (no engy
	// alive, no medic alive). PickRespawnClass() returns currentClass
	// when no swap is warranted.
	const int currentClass = GetClassSlot();
	if ( currentClass >= CLASS_SCOUT && currentClass <= CLASS_CIVILIAN )
	{
		const int suggested = FFBotIntel::PickRespawnClass( this, currentClass );
		if ( suggested != currentClass && suggested > 0 )
		{
			const char *className = Class_IntToString( suggested );
			if ( className && className[ 0 ] )
			{
				ChangeClass( className );
				Msg( "[CFFBot] Adaptive respawn: '%s' switching from class %d to %d (team need).\n",
					GetPlayerName() ? GetPlayerName() : "?", currentClass, suggested );
			}
		}
	}

	BaseClass::Spawn();

	// Class-specialty per-life state — wiped here so spy re-disguises, engineer
	// re-tries to build, etc. on every respawn.
	m_bClassDidSpawnInit = false;
	m_classBuildTimer.Invalidate();
	m_classDisguiseTimer.Invalidate();
	m_classCloakTimer.Invalidate();
	m_calloutTimer.Invalidate();
	m_weaponSwitchTimer.Invalidate();
	m_retreatTimer.Invalidate();
	m_resupplyCheckTimer.Invalidate();
	m_grenadePrimeStart = 0.0f;
	m_grenadeCooldownTimer.Invalidate();
	m_sniperFireState = SNIPER_FIRE_IDLE;
	m_sniperFireStartTime = 0.0f;
	m_lastThreatTime = 0.0f;
	m_lastThreatPos.Init();
	m_bunnyHopTimer.Invalidate();
	m_lastUnstuckTime = gpGlobals->curtime;
	m_pathInhibitTimer.Invalidate();
	m_recentStuckPos.Init();
	m_recentStuckExpireTime = 0.0f;
	m_lookAroundUntil = 0.0f;
	ClearMoveOverride();
	m_moveOverridePos.Init();
	m_pathDrivenTick = -1;
	m_pathGoalPos.Init();
	m_pathGoalTime = 0.0f;
	m_pathTurnAhead = false;
	m_blockingDoor = NULL;
	m_doorPushTimer.Invalidate();
	m_stuckStage = 0;
	m_stuckStageTime = gpGlobals->curtime;
	m_lastGoodPos = GetAbsOrigin();
	m_lastGoodPosTime = gpGlobals->curtime;
	m_abandonGoalRequest = false;
	m_currentThreatId = -1;
	m_threatFirstSeenTime = 0.0f;
	m_lastThreatlessTime = 0.0f;

	// A new life gets a clean read of the map. An objective we gave up on last
	// life is very likely reachable now — the door the keycard opens, the phase
	// gate that has since fallen, the lift that was at the other floor. Keeping
	// the blacklist across a death is how a bot ends up permanently refusing to
	// go somewhere for a reason that stopped being true minutes ago.
	FFBotGameMode::ClearObjectiveBlacklist( this );
	FFBotHazard::Reset( this );

	// Difficulty re-reads each spawn so live cvar tweaks pick up. Per-bot
	// jitter stays the same across lives — that's a "personality" trait.
	m_difficulty = clamp( ff_bot_difficulty.GetInt(), 0, 3 );

	// Spawn-aim override.
	//
	// Root cause: CGameRules::GetPlayerSpawnSpot calls SnapEyeAngles with
	// the spawn entity's facing during BaseClass::Spawn(). Mappers don't
	// always angle spawns toward the playable region, so PlayerLocomotion
	// ::Approach can read view-vs-goal and press IN_BACK, walking the bot
	// into whatever is behind their facing.
	//
	// Approach (TFBot-style):
	//   1. Use the bot's nav area's enemy-invasion vector — neighbors with
	//      LOWER enemy-team incursion distance are the doorways out of
	//      our spawn toward the fight. Picks the closest one.
	//   2. If invasion vector is empty (interior area with no gradient,
	//      e.g., area entirely surrounded by spawn-room neighbors), fall
	//      back to the closest spawn-room threshold area for our team.
	//   3. Last resort: closest enemy flag/cap area as a directional hint.
	//
	// All three depend only on level-init nav state, not intra-room
	// position, so two bots in the same spawn room get the same direction.
	m_spawnExitDir.Init();
	m_spawnExitForceTimer.Invalidate();

	const Vector myPos = GetAbsOrigin();
	m_spawnExitStartPos = myPos;

	CBaseEntity *spawnSpot = GetLastSpawnPoint();
	Vector targetPos = vec3_origin;
	const char *tier = "none";
	bool gotTarget = false;
	CNavArea *botArea = NULL;
	CFFNavMesh *ffMesh = TheFFNavMesh();

	if ( TheNavMesh && TheNavMesh->IsLoaded() )
	{
		botArea = TheNavMesh->GetNearestNavArea(
			myPos, false, 1024.0f, false, true, TEAM_ANY );

		// Tier 1: invasion vector from our nav area.
		if ( !gotTarget && botArea )
		{
			CFFNavArea *here = static_cast< CFFNavArea * >( botArea );
			const CUtlVector< CFFNavArea * > &invasion =
				here->GetEnemyInvasionAreaVector( GetTeamNumber() );
			if ( invasion.Count() > 0 )
			{
				CFFNavArea *best = NULL;
				float bestDistSq = FLT_MAX;
				for ( int i = 0; i < invasion.Count(); ++i )
				{
					CFFNavArea *cand = invasion[ i ];
					if ( !cand )
						continue;
					Vector delta = cand->GetCenter() - myPos;
					delta.z = 0.0f;
					const float dSq = delta.LengthSqr();
					if ( dSq < 1.0f )
						continue;
					if ( dSq < bestDistSq )
					{
						bestDistSq = dSq;
						best = cand;
					}
				}
				if ( best )
				{
					targetPos = best->GetCenter();
					tier = "invasion";
					gotTarget = true;
				}
			}
		}

		// Tier 2: closest spawn-room threshold area for our team. Mirrors
		// CTFNavMesh::CollectSpawnRoomThresholdAreas — the largest non-spawn
		// neighbor of each spawn-room exit, i.e., the playable region we
		// step into when leaving spawn.
		if ( !gotTarget && ffMesh )
		{
			CUtlVector< CFFNavArea * > thresholds;
			ffMesh->CollectSpawnRoomThresholdAreas( GetTeamNumber(), &thresholds );
			CFFNavArea *best = NULL;
			float bestDistSq = FLT_MAX;
			for ( int i = 0; i < thresholds.Count(); ++i )
			{
				const float dSq = ( thresholds[ i ]->GetCenter() - myPos ).LengthSqr();
				if ( dSq < bestDistSq )
				{
					bestDistSq = dSq;
					best = thresholds[ i ];
				}
			}
			if ( best )
			{
				targetPos = best->GetCenter();
				tier = "threshold";
				gotTarget = true;
			}
		}
	}

	// Tier 3: closest enemy flag / cap area as a directional hint.
	if ( !gotTarget && ffMesh )
	{
		Vector best = vec3_origin;
		float bestDistSq = FLT_MAX;
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			if ( t == GetTeamNumber() )
				continue;
			const CUtlVector< CFFNavArea * > *flags = ffMesh->GetFlagAreas( t );
			if ( flags )
			{
				for ( int i = 0; i < flags->Count(); ++i )
				{
					const float dSq = ( ( *flags )[ i ]->GetCenter() - myPos ).LengthSqr();
					if ( dSq < bestDistSq )
					{
						bestDistSq = dSq;
						best = ( *flags )[ i ]->GetCenter();
					}
				}
			}
			const CUtlVector< CFFNavArea * > *caps = ffMesh->GetCapAreas( t );
			if ( caps )
			{
				for ( int i = 0; i < caps->Count(); ++i )
				{
					const float dSq = ( ( *caps )[ i ]->GetCenter() - myPos ).LengthSqr();
					if ( dSq < bestDistSq )
					{
						bestDistSq = dSq;
						best = ( *caps )[ i ]->GetCenter();
					}
				}
			}
		}
		if ( bestDistSq < FLT_MAX )
		{
			targetPos = best;
			tier = "objective";
			gotTarget = true;
		}
	}

	// Diagnostics — bot name/team/class/pos/spawn entity/nav area, plus
	// which tier picked the direction. Helps correlate "bot stuck"
	// reports with whether the tagger fired correctly.
	const char *diagName = GetPlayerName() ? GetPlayerName() : "?";
	const char *diagSpawnClass = spawnSpot ? spawnSpot->GetClassname() : "none";
	const char *diagSpawnName = spawnSpot ? STRING( spawnSpot->GetEntityName() ) : "";
	if ( !diagSpawnName || !diagSpawnName[ 0 ] )
		diagSpawnName = "-";
	int diagAreaId = -1;
	unsigned int diagAreaAttrs = 0;
	if ( botArea )
	{
		diagAreaId = botArea->GetID();
		diagAreaAttrs = static_cast< CFFNavArea * >( botArea )->GetAttributesFF();
	}

	if ( gotTarget )
	{
		Vector dir = targetPos - myPos;
		dir.z = 0.0f;
		if ( dir.NormalizeInPlace() > 0.1f )
		{
			m_spawnExitDir = dir;
			m_spawnExitForceTimer.Start( 1.5f );

			QAngle desiredAngles;
			VectorAngles( dir, desiredAngles );
			SnapEyeAngles( desiredAngles );

			IBody *body = GetBodyInterface();
			if ( body )
			{
				Vector lookAt = EyePosition() + dir * 200.0f;
				body->AimHeadTowards( lookAt, IBody::IMPORTANT, 1.0f, NULL, "Spawn exit" );
			}

			Msg( "[CFFBot] spawn-aim '%s' team=%d class=%d pos=(%.0f,%.0f,%.0f) "
				"spawn=%s:%s navArea=%d attrs=0x%08x tier=%s yaw=%.0f\n",
				diagName, GetTeamNumber(), GetClassSlot(),
				myPos.x, myPos.y, myPos.z,
				diagSpawnClass, diagSpawnName, diagAreaId, diagAreaAttrs,
				tier, desiredAngles.y );
		}
	}
	else
	{
		Msg( "[CFFBot] spawn-aim '%s' team=%d class=%d navArea=%d attrs=0x%08x — "
			"no tier matched (no nav, no threshold, no objective). "
			"Bot keeps spawn entity's angles.\n",
			diagName, GetTeamNumber(), GetClassSlot(), diagAreaId, diagAreaAttrs );
	}
}

//-----------------------------------------------------------------------------
// Combat range table — per-class engagement ranges. Indexed by class slot
// (CLASS_SCOUT..CLASS_CIVILIAN). `desired` is "stop chasing, hold and shoot";
// `maxAttack` is "still effective from here". Roughly mirrors TFBot per-weapon
// values, mapped to FF class loadouts.
//-----------------------------------------------------------------------------
struct FFBotCombatRange
{
	float desired;
	float maxAttack;
};

static const FFBotCombatRange kCombatRanges[ 11 ] = {
	/* 0  unassigned */ { 600.0f,  600.0f },
	/* 1  Scout    */   { 250.0f,  600.0f },		// nailgun / supershotgun
	/* 2  Sniper   */   { 2000.0f, 3000.0f },		// sniper rifle
	/* 3  Soldier  */   { 750.0f,  1500.0f },		// RPG (splash)
	/* 4  Demoman  */   { 600.0f,  1100.0f },		// grenade launcher (arc)
	/* 5  Medic    */   { 350.0f,  750.0f },		// super nailgun
	/* 6  HWGuy    */   { 600.0f,  1200.0f },		// assault cannon
	/* 7  Pyro     */   { 200.0f,  1000.0f },		// flamer / IC
	/* 8  Spy      */   { 500.0f,  1000.0f },		// tranq
	/* 9  Engineer */   { 500.0f,  1000.0f },		// railgun
	/* 10 Civilian */   { 60.0f,   80.0f },			// umbrella melee
};


//-----------------------------------------------------------------------------
float CFFBot::GetDesiredAttackRange( void ) const
{
	const int slot = GetClassSlot();
	if ( slot < 0 || slot > CLASS_CIVILIAN )
		return 600.0f;
	return kCombatRanges[ slot ].desired;
}


//-----------------------------------------------------------------------------
float CFFBot::GetMaxAttackRange( void ) const
{
	const int slot = GetClassSlot();
	if ( slot < 0 || slot > CLASS_CIVILIAN )
		return 1200.0f;
	return kCombatRanges[ slot ].maxAttack;
}


//-----------------------------------------------------------------------------
float CFFBot::GetDesiredPathLookAheadRange( void ) const
{
	// Default 300u — enough to clear most corners without overshooting the
	// turn. HWGuy is slow but still uses 300; we don't currently need
	// per-class variation here.
	return 300.0f;
}


//-----------------------------------------------------------------------------
bool CFFBot::EquipBestWeaponForThreat( const CKnownEntity *threat )
{
	float threatRange = -1.0f;
	if ( threat && threat->GetEntity() )
	{
		threatRange = ( threat->GetLastKnownPosition() - GetAbsOrigin() ).Length();
	}
	return FFBotWeapon::TrySwitchToPreferred( const_cast< CFFBot * >( this ), threatRange );
}


//-----------------------------------------------------------------------------
bool CFFBot::IsLineOfFireClear( const Vector &where ) const
{
	return IsLineOfFireClear( const_cast< CFFBot * >( this )->EyePosition(), where );
}

bool CFFBot::IsLineOfFireClear( CBaseEntity *who ) const
{
	if ( !who )
		return false;
	return IsLineOfFireClear( const_cast< CFFBot * >( this )->EyePosition(), who->WorldSpaceCenter() );
}

bool CFFBot::IsLineOfFireClear( const Vector &from, const Vector &to ) const
{
	trace_t tr;
	CTraceFilterSimple filter( this, COLLISION_GROUP_NONE );
	UTIL_TraceLine( from, to, MASK_SHOT, &filter, &tr );
	return tr.fraction >= 1.0f && !tr.startsolid;
}


//-----------------------------------------------------------------------------
float CFFBot::TransientlyConsistentRandomValue( float period, int seedValue ) const
{
	if ( period <= 0.0f )
		period = 10.0f;
	const int bucket = (int)( gpGlobals->curtime / period );
	const unsigned int h0 = ( m_routeSeed ^ (unsigned int)( seedValue * 2654435769U ) ) + (unsigned int)bucket * 22695477U;
	const unsigned int h1 = h0 ^ ( h0 >> 13 );
	return ( h1 % 1001 ) * 0.001f;
}


//-----------------------------------------------------------------------------
bool CFFBot::IsAmmoLow( void ) const
{
	CFFWeaponBase *active = const_cast< CFFBot * >( this )->GetActiveFFWeapon();
	if ( active )
	{
		const int ammoType = active->GetPrimaryAmmoType();
		if ( ammoType >= 0 )
		{
			const int reserve = const_cast< CFFBot * >( this )->GetAmmoCount( ammoType );
			const int clip = active->Clip1();
			if ( reserve < 20 && clip <= 0 )
				return true;
		}
	}
	if ( GetClassSlot() == CLASS_ENGINEER )
	{
		const int cellsAmmoType = GetAmmoDef()->Index( AMMO_CELLS );
		if ( cellsAmmoType >= 0 && const_cast< CFFBot * >( this )->GetAmmoCount( cellsAmmoType ) < 50 )
			return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
bool CFFBot::IsAmmoFull( void ) const
{
	// Walk all primary ammo types we have a max for; if any has room to
	// grow, we're not full.
	CFFBot *self = const_cast< CFFBot * >( this );
	for ( int i = 0; i < MAX_AMMO_TYPES; ++i )
	{
		const int maxCount = m_iMaxAmmo[ i ];
		if ( maxCount <= 0 )
			continue;	// class doesn't carry this type
		if ( self->GetAmmoCount( i ) < maxCount )
			return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Reaction-time floor — how long after first sighting until we'll fire.
//
// Difficulty schedule (matches the ff_bot_difficulty cvar tiers):
//   0 easy   — 0.40s base + up to 0.20s jitter (≤ 0.60s total)
//   1 normal — 0.25s base + up to 0.15s jitter (≤ 0.40s)
//   2 hard   — 0.15s base + up to 0.10s jitter (≤ 0.25s)
//   3 expert — 0.08s base + up to 0.07s jitter (≤ 0.15s)
//
// Jitter is per-bot deterministic so the same bot reacts consistently within
// a life — gives each bot a slightly different feel without thrashing.
//-----------------------------------------------------------------------------
float CFFBot::GetReactionTimeFloor( void ) const
{
	static const float kBase[ 4 ]   = { 0.40f, 0.25f, 0.15f, 0.08f };
	static const float kJitter[ 4 ] = { 0.20f, 0.15f, 0.10f, 0.07f };
	const int d = clamp( m_difficulty, 0, 3 );
	return kBase[ d ] + m_reactionJitter * kJitter[ d ];
}


//-----------------------------------------------------------------------------
// Reaction-time gate: returns true once we've held *some* threat long enough
// to react. The clock is per-engagement, NOT per-threat — a multi-enemy
// scene where vision oscillates between visible primaries (e.g., two
// equidistant enemies) doesn't keep resetting the timer. Only a sustained
// threatless period (>1.5s) ends the engagement and resets.
//-----------------------------------------------------------------------------
bool CFFBot::HasReactedToThreat( const CKnownEntity *threat )
{
	const float now = gpGlobals->curtime;

	if ( !threat || !threat->GetEntity() )
	{
		// No threat right now. Mark the threatless start; only fully
		// reset the engagement clock once we've been threatless long
		// enough that this is a real disengage, not a vision blip.
		if ( m_threatFirstSeenTime > 0.0f && m_lastThreatlessTime == 0.0f )
			m_lastThreatlessTime = now;
		if ( m_lastThreatlessTime > 0.0f && ( now - m_lastThreatlessTime ) > 1.5f )
		{
			m_currentThreatId = -1;
			m_threatFirstSeenTime = 0.0f;
			m_lastThreatlessTime = 0.0f;
		}
		return true;	// no threat = nothing to gate
	}

	// Have a threat. Reset the threatless clock — we're back in the fight.
	m_lastThreatlessTime = 0.0f;

	if ( m_threatFirstSeenTime <= 0.0f )
	{
		// Fresh engagement — start the reaction clock.
		m_threatFirstSeenTime = now;
	}
	// Update which entity we're tracking, but do NOT reset the clock if
	// vision swaps primaries within the same engagement. That swap-reset
	// behavior was preventing fire entirely when 2+ enemies were visible.
	m_currentThreatId = threat->GetEntity()->entindex();

	return ( now - m_threatFirstSeenTime ) >= GetReactionTimeFloor();
}


//-----------------------------------------------------------------------------
// Apply Gaussian-ish aim error to a target world-space point.
//
// Three multipliers compose:
//   - Difficulty: easy is much wider than expert.
//   - Range:      a 4° error at 200u is a hit, at 2000u it's a clean miss.
//                 We scale error proportional to range so absolute miss-
//                 distance stays roughly comparable.
//   - Hold-time:  early shots at a fresh threat are wider; the aim
//                 narrows over the first ~1s of tracking. Mirrors human
//                 settling onto a target.
//
// Gaussian-ish is summed-uniforms (cheap, no log/sqrt). Scale-factor 1.5
// makes it close to a true gaussian for our purposes.
//-----------------------------------------------------------------------------
Vector CFFBot::ApplyAimError( const Vector &targetPos, float holdTimeSec ) const
{
	static const float kBaseSpread[ 4 ] = { 0.075f, 0.040f, 0.022f, 0.012f };	// radians
	const int d = clamp( m_difficulty, 0, 3 );

	// Hold-time tightening: 1.0 at hold=0, decays to ~0.25 by hold=1s.
	float holdFactor = 1.0f / ( 1.0f + 3.0f * holdTimeSec );
	if ( holdFactor < 0.25f )
		holdFactor = 0.25f;

	const float spreadRad = kBaseSpread[ d ] * holdFactor;
	if ( spreadRad <= 0.0f )
		return targetPos;

	// Build a noise vector orthogonal to the line of fire so the noise
	// shows up as left/right + up/down at the target plane, not in/out.
	// EyePosition() isn't const so cast — pure read; we don't mutate state.
	Vector toTarget = targetPos - const_cast< CFFBot * >( this )->EyePosition();
	const float range = toTarget.NormalizeInPlace();
	if ( range < 1.0f )
		return targetPos;

	// Pick any vector not parallel to toTarget; cross to get a basis.
	Vector up( 0, 0, 1 );
	if ( fabsf( toTarget.Dot( up ) ) > 0.95f )
		up = Vector( 0, 1, 0 );

	Vector right = CrossProduct( toTarget, up );
	right.NormalizeInPlace();
	Vector trueUp = CrossProduct( right, toTarget );
	trueUp.NormalizeInPlace();

	// Time-bucketed deterministic offsets (4× per second). Per-tick
	// re-rolling thrashed the body's slew so the bot's view never
	// settled onto the actual target — body lag + new offset every
	// frame meant the canFireNow alignment test always failed.
	// Holding the offset stable for ~250ms gives the slew time to
	// reach the (offset) point so fires land. The two seed values
	// (X / Y) decorrelate the horizontal and vertical axes.
	const float jx = ( TransientlyConsistentRandomValue( 0.25f, 31 ) - 0.5f ) * 2.0f;	// [-1, 1]
	const float jy = ( TransientlyConsistentRandomValue( 0.25f, 73 ) - 0.5f ) * 2.0f;

	// Convert angular spread to lateral offset at the target's range.
	const float lateral = tanf( spreadRad ) * range;
	const float dx = jx * lateral;
	const float dy = jy * lateral;

	return targetPos + right * dx + trueUp * dy;
}


//-----------------------------------------------------------------------------
// Static factory used by ClientPutInServerOverride during fake-client creation.
// CFFBot inherits from CBasePlayer (via NextBotPlayer<CFFPlayer>) and so has
// access to the protected static s_PlayerEdict.
//-----------------------------------------------------------------------------
CBasePlayer *CFFBot::AllocatePlayerEntity( edict_t *pEdict, const char *playerName )
{
	CBasePlayer::s_PlayerEdict = pEdict;
	CFFBot *pBot = static_cast< CFFBot * >( CreateEntityByName( "ff_bot" ) );
	if ( pBot )
	{
		pBot->SetPlayerName( playerName );
	}
	return pBot;
}

namespace
{
	int g_FFBotCurrentNumber = 1;
}

//-----------------------------------------------------------------------------
// Build a bitmask of teams that have at least one team-specific
// info_ff_teamspawn on the current map. A spawn with TEAM_UNASSIGNED is
// "generic" (DM / conc jump) and contributes nothing to the mask.
// Returns 0 when the map has *only* generic spawns — caller should treat that
// as "any team is allowed".
//-----------------------------------------------------------------------------
static int FFBot_GetMapSpawnTeamMask( void )
{
	int mask = 0;
	CBaseEntity *pSpawn = NULL;
	while ( ( pSpawn = gEntList.FindEntityByClassname( pSpawn, "info_ff_teamspawn" ) ) != NULL )
	{
		int spawnTeam = pSpawn->GetTeamNumber();
		if ( spawnTeam >= TEAM_BLUE && spawnTeam <= TEAM_GREEN )
			mask |= ( 1 << spawnTeam );
	}
	return mask;
}

//-----------------------------------------------------------------------------
// True if 'team' is allowed on the current map. Two independent checks:
//   1) FF Lua sets m_iMaxPlayers = -1 to *disable* a team (base_ctf.lua does
//      this for kYellow/kGreen on 2-team CTF maps). Reject those outright.
//   2) The team must have a team-specific info_ff_teamspawn — unless the map
//      has only generic spawns (DM / conc-jump), in which case all enabled
//      teams are allowed.
//-----------------------------------------------------------------------------
static bool FFBot_IsTeamAvailableOnMap( int team )
{
	if ( team < TEAM_BLUE || team > TEAM_GREEN )
		return false;

	CFFTeam *pTeam = GetGlobalFFTeam( team );
	if ( !pTeam )
		return false;

	// FF convention: SetPlayerLimit(team, -1) disables a team for the map.
	if ( pTeam->GetTeamLimits() == -1 )
		return false;

	int mask = FFBot_GetMapSpawnTeamMask();
	if ( mask == 0 )
		return true;	// generic-spawn-only map — all enabled teams allowed
	return ( mask & ( 1 << team ) ) != 0;
}

//-----------------------------------------------------------------------------
// Pick the smallest enabled game team that the current map actually supports.
// "Enabled" = at least one class is allowed (GetClassLimit != -1) AND the map
// has a team-specific spawn for that team (or is generic-spawn-only).
// On a tie, prefers the lower team number. Falls back to TEAM_BLUE.
//-----------------------------------------------------------------------------
static int FFBot_PickAutoTeam( void )
{
	int spawnMask = FFBot_GetMapSpawnTeamMask();
	bool restrictBySpawnMask = ( spawnMask != 0 );

	int bestTeam = -1;
	int bestCount = INT_MAX;

	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( restrictBySpawnMask && !( spawnMask & ( 1 << t ) ) )
			continue;	// no team-specific spawn for this team on this map

		CFFTeam *pTeam = GetGlobalFFTeam( t );
		if ( !pTeam )
			continue;

		// FF convention: SetPlayerLimit(team, -1) disables a team on this map.
		if ( pTeam->GetTeamLimits() == -1 )
			continue;

		bool anyClassEnabled = false;
		for ( int c = CLASS_SCOUT; c <= CLASS_CIVILIAN; ++c )
		{
			if ( pTeam->GetClassLimit( c ) != -1 )
			{
				anyClassEnabled = true;
				break;
			}
		}
		if ( !anyClassEnabled )
			continue;

		int count = pTeam->GetNumPlayers();
		if ( count < bestCount )
		{
			bestCount = count;
			bestTeam = t;
		}
	}

	return ( bestTeam == -1 ) ? TEAM_BLUE : bestTeam;
}

//-----------------------------------------------------------------------------
// Count how many players on `team` are currently a given class. Counts both
// human and bot CFFPlayer instances.
//-----------------------------------------------------------------------------
static int FFBot_CountClassOnTeam( int team, int classSlot )
{
	int n = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( pp && pp->GetTeamNumber() == team && pp->GetClassSlot() == classSlot )
			++n;
	}
	return n;
}

//-----------------------------------------------------------------------------
// Pick a class that's allowed on the given team and not at its cap.
//
// First ask FFBotGameMode, which knows what kind of map this is and whether the
// team still owes the defense quota a body. That is the answer that matters:
// the old logic below biased towards having one engineer and one medic and was
// otherwise uniform, which on an attack/defend map produced a defending team of
// mostly scouts and soldiers with nothing holding the point.
//
// The old logic stays as the fallback for the cases the mode layer declines —
// no CFFTeam, no detected mode, every class capped. It prevents 8-bot teams
// from ending up with 8 soldiers, which is still worth having.
//-----------------------------------------------------------------------------
static int FFBot_PickAutoClass( int team )
{
	CFFTeam *pTeam = GetGlobalFFTeam( team );
	if ( !pTeam )
		return CLASS_SCOUT;

	const int modePick = FFBotGameMode::PickClassForTeamNeed( team );
	if ( modePick >= CLASS_SCOUT && modePick <= CLASS_CIVILIAN )
		return modePick;

	int validClasses[ 10 ];
	int numValid = 0;

	for ( int c = CLASS_SCOUT; c <= CLASS_CIVILIAN; ++c )
	{
		int limit = pTeam->GetClassLimit( c );
		if ( limit == -1 )
			continue;	// disabled on this team

		if ( limit > 0 )
		{
			// Cap is set — count how many on the team are already this class.
			const int currCount = FFBot_CountClassOnTeam( team, c );
			if ( currCount >= limit )
				continue;	// full
		}

		validClasses[ numValid++ ] = c;
	}

	if ( numValid == 0 )
		return CLASS_SCOUT;

	// Role-coverage bias. If the team is missing an engineer or medic and
	// that class is in the valid list, pick it deterministically.
	const bool engineerValid = std::find( validClasses, validClasses + numValid, (int)CLASS_ENGINEER ) != validClasses + numValid;
	const bool medicValid    = std::find( validClasses, validClasses + numValid, (int)CLASS_MEDIC )    != validClasses + numValid;

	if ( engineerValid && FFBot_CountClassOnTeam( team, CLASS_ENGINEER ) == 0 )
		return CLASS_ENGINEER;
	if ( medicValid && FFBot_CountClassOnTeam( team, CLASS_MEDIC ) == 0 )
		return CLASS_MEDIC;

	// Soft-bias: if there's only 1 of either, prefer adding another so we
	// have redundancy when one dies. Otherwise random offense.
	if ( engineerValid && FFBot_CountClassOnTeam( team, CLASS_ENGINEER ) == 1 && RandomInt( 0, 3 ) == 0 )
		return CLASS_ENGINEER;
	if ( medicValid && FFBot_CountClassOnTeam( team, CLASS_MEDIC ) == 1 && RandomInt( 0, 3 ) == 0 )
		return CLASS_MEDIC;

	return validClasses[ RandomInt( 0, numValid - 1 ) ];
}

//-----------------------------------------------------------------------------
CBasePlayer *CreateFFBot( bool bFrozen, int iTeam, int iClass, const char *pszCustomName )
{
	char botname[ 64 ];
	if ( pszCustomName && pszCustomName[0] )
	{
		V_strcpy_safe( botname, pszCustomName );
	}
	else
	{
		Q_snprintf( botname, sizeof( botname ), "Bot%02i", g_FFBotCurrentNumber );
	}

	ClientPutInServerOverride( &CFFBot::AllocatePlayerEntity );
	edict_t *pEdict = engine->CreateFakeClient( botname );
	ClientPutInServerOverride( NULL );

	if ( !pEdict )
	{
		Msg( "Failed to create FF bot.\n" );
		return NULL;
	}

	CFFBot *pBot = static_cast< CFFBot * >( CBaseEntity::Instance( pEdict ) );

	pBot->ClearFlags();
	pBot->AddFlag( FL_CLIENT | FL_FAKECLIENT );

	if ( bFrozen )
		pBot->AddEFlags( EFL_BOT_FROZEN );

	// If caller didn't pick a real game team, auto-balance to the smallest
	// enabled team — otherwise the bot ends up on TEAM_UNASSIGNED and
	// CFFPlayer::ChangeClass returns early because GetGlobalFFTeam() is NULL.
	if ( iTeam < FIRST_GAME_TEAM || iTeam > TEAM_GREEN )
	{
		iTeam = FFBot_PickAutoTeam();
	}
	else if ( !FFBot_IsTeamAvailableOnMap( iTeam ) )
	{
		// User asked for a team the map doesn't support (no team-specific
		// spawn for it). Fall back to auto-pick rather than dead-spawn.
		Msg( "[CFFBot] Requested team %d has no spawn on the current map; auto-picking instead.\n", iTeam );
		iTeam = FFBot_PickAutoTeam();
	}

	pBot->ChangeTeam( iTeam );

	// Validate / replace class. If the requested class is invalid, disabled,
	// or already capped on this team, pick a random allowed one.
	if ( iClass < CLASS_SCOUT || iClass > CLASS_CIVILIAN )
	{
		iClass = FFBot_PickAutoClass( iTeam );
	}
	else
	{
		CFFTeam *pTeam = GetGlobalFFTeam( iTeam );
		if ( pTeam && pTeam->GetClassLimit( iClass ) == -1 )
		{
			Msg( "[CFFBot] Requested class %s is disabled on team %d; picking another.\n",
				Class_IntToString( iClass ), iTeam );
			iClass = FFBot_PickAutoClass( iTeam );
		}
	}

	const char *className = Class_IntToString( iClass );
	if ( className && className[0] )
		pBot->ChangeClass( className );

	pBot->RemoveAllItems( true );
	pBot->Spawn();

	Msg( "[CFFBot] Spawned bot '%s' on team %d as class %s.\n",
		botname, iTeam, className ? className : "?" );

	g_FFBotCurrentNumber++;
	return pBot;
}


//-----------------------------------------------------------------------------
// Periodic autobalance — moves a bot from the largest team to the smallest
// when the gap is >= 2 players. Runs every FFBOT_AUTOBALANCE_INTERVAL seconds
// from Bot_RunAll. Only ever moves bots; never humans.
//-----------------------------------------------------------------------------
#define FFBOT_AUTOBALANCE_INTERVAL	30.0f
#define FFBOT_AUTOBALANCE_THRESHOLD	2

void FFBotManager_Tick( void )
{
	// Intel layer runs every frame: alert decay, flag-stolen detection.
	FFBotIntel::Tick();

	// FIX 11 — watch every player (human and bot) and learn nav connections
	// the mesh is missing. Cheap: one nav lookup per moving player per ~64
	// units travelled.
	FFBotLearnedLinks::Update();

	// Manual nav authoring overlay. Returns immediately unless
	// ff_manual_nav_builder is on.
	FFNavBuilder::Tick();

	// Keep the last ff_nav_visualize view on screen. Returns immediately when
	// no view is active.
	FFBotCommands_TickVisualization();

	// Reconcile Lua-declared objectives with their live state and re-tag the
	// nav mesh when the set of real objectives moves — an AvD phase change, a
	// key becoming available, a cap going active.
	FFBotLuaObjectives::Tick();

	// Re-read what kind of game this is from the live objective set, and re-run
	// the per-team offense/defense quota. Both internally throttled. Must run
	// AFTER FFBotLuaObjectives::Tick, which is what marks the mode stale.
	FFBotGameMode::Tick();

	static CountdownTimer s_balanceTimer;
	if ( !s_balanceTimer.HasStarted() )
	{
		s_balanceTimer.Start( FFBOT_AUTOBALANCE_INTERVAL );
		return;
	}
	if ( !s_balanceTimer.IsElapsed() )
		return;
	s_balanceTimer.Start( FFBOT_AUTOBALANCE_INTERVAL );

	// Tally per-team population. Index by FF team number (TEAM_BLUE..TEAM_GREEN
	// = 2..5); use a small fixed-size array indexed directly.
	const int kFirstTeam = TEAM_BLUE;
	const int kLastTeam  = TEAM_GREEN;

	int teamSize[ 4 ] = { 0, 0, 0, 0 };
	CUtlVector< CFFBot * > teamBots[ 4 ];

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp )
			continue;
		const int t = pp->GetTeamNumber();
		if ( t < kFirstTeam || t > kLastTeam )
			continue;
		const int slot = t - kFirstTeam;
		++teamSize[ slot ];
		if ( pp->IsBot() )
			teamBots[ slot ].AddToTail( static_cast< CFFBot * >( pp ) );
	}

	// Find biggest and smallest map-allowed teams.
	int largestTeam  = -1;
	int smallestTeam = -1;
	int largestSize  = -1;
	int smallestSize = INT_MAX;
	for ( int t = kFirstTeam; t <= kLastTeam; ++t )
	{
		if ( !FFBot_IsTeamAvailableOnMap( t ) )
			continue;
		const int slot = t - kFirstTeam;
		if ( teamSize[ slot ] > largestSize )
		{
			largestSize  = teamSize[ slot ];
			largestTeam  = t;
		}
		if ( teamSize[ slot ] < smallestSize )
		{
			smallestSize = teamSize[ slot ];
			smallestTeam = t;
		}
	}

	if ( largestTeam < 0 || smallestTeam < 0 || largestTeam == smallestTeam )
		return;
	if ( largestSize - smallestSize < FFBOT_AUTOBALANCE_THRESHOLD )
		return;

	// Pick a bot to move. Prefer one that isn't carrying a flag (we don't
	// want to drop the flag mid-game by switching teams). Skip dead bots —
	// they'll just respawn on the new team naturally.
	CFFBot *moveCandidate = NULL;
	const CUtlVector< CFFBot * > &candidates = teamBots[ largestTeam - kFirstTeam ];
	for ( int i = 0; i < candidates.Count(); ++i )
	{
		CFFBot *bot = candidates[ i ];
		if ( !bot )
			continue;
		if ( FFBotHelpers::IsBotCarryingFlag( bot ) )
			continue;
		moveCandidate = bot;
		break;
	}

	if ( !moveCandidate )
		return;

	const char *botName = moveCandidate->GetPlayerName();
	moveCandidate->ChangeTeam( smallestTeam );

	// Re-pick a class that's allowed on the new team — old class may be
	// disabled there or already capped.
	const int newClassSlot = FFBot_PickAutoClass( smallestTeam );
	const char *newClassName = Class_IntToString( newClassSlot );
	if ( newClassName && newClassName[ 0 ] )
		moveCandidate->ChangeClass( newClassName );

	Msg( "[CFFBot] Autobalance: moved '%s' from team %d to team %d (%d vs %d).\n",
		botName ? botName : "?", largestTeam, smallestTeam, largestSize, smallestSize );
}
