//========= Fortress Forever Bot =============================================//
//
// CFFBotCtfObjective — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_ctf.h"
#include "ff_bot_helpers.h"
#include "ff_bot_intel.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_player.h"
#include "ff_info_script.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "engine/IEngineTrace.h"

#include "omnibot_interface.h"	// Omnibot::kFlag, kFlagCap

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Wander goals are picked from areas within this radius of the bot, same as
// CFFBotWander — keeps fallback behavior consistent.
#define FFBOT_CTF_WANDER_RADIUS	2000.0f


// True if `pos` is within 200u of any persistent grenade-aftermath danger
// entity (pyro fire, scout gas, hwguy slowfield, enemy stickies). When this
// fires, the contained action should invalidate its path so the next tick's
// repath uses the new danger snapshot in CFFBotPathCost and routes around.
//
// Without this, the bot keeps walking the (stale) pre-fire path for up to
// ~1.5s after fire appears in front of them — long enough to take real
// damage. The check is cheap: ~5-10 entity origins per query.
static bool IsStandingInGrenadeDanger( int myTeam, const Vector &pos )
{
	static const char * const kDangerClasses[] = {
		"ff_grenade_napalmlet",
		"ff_grenade_gas",
		"ff_grenade_slowfield",
	};
	const float radSq = 200.0f * 200.0f;
	for ( int c = 0; c < ARRAYSIZE( kDangerClasses ); ++c )
	{
		CBaseEntity *e = NULL;
		while ( ( e = gEntList.FindEntityByClassname( e, kDangerClasses[ c ] ) ) != NULL )
		{
			if ( ( e->GetAbsOrigin() - pos ).LengthSqr() < radSq )
				return true;
		}
	}
	CBaseEntity *sb = NULL;
	while ( ( sb = gEntList.FindEntityByClassname( sb, "ff_projectile_pl" ) ) != NULL )
	{
		if ( sb->GetTeamNumber() == myTeam )
			continue;
		if ( ( sb->GetAbsOrigin() - pos ).LengthSqr() < radSq )
			return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// True if the flag is currently grabbable: not removed, not currently carried
// by any player.
//-----------------------------------------------------------------------------
static bool IsFlagGrabbable( CFFInfoScript *flag )
{
	return flag != NULL && !flag->IsRemoved() && !flag->IsCarried();
}

//-----------------------------------------------------------------------------
// Find the closest enemy flag that's grabbable right now. Skips flags carried
// by anyone (teammate or enemy). Uses the touch-flag bitmask for ownership
// because CFFInfoScript::GetTeamNumber() is unreliable (Lua doesn't propagate
// the "team" table field to the C++ entity's m_iTeamNum).
//-----------------------------------------------------------------------------
static CFFInfoScript *FindClosestEnemyFlag( int myTeam, const Vector &myPos )
{
	CFFInfoScript *best = NULL;
	float bestDistSq = FLT_MAX;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( !IsFlagGrabbable( s ) )
			continue;
		if ( !FFBotHelpers::CanBotGrabFlag( myTeam, s ) )
			continue;	// our own flag (touch-flag bit absent)

		const float dSq = ( s->GetAbsOrigin() - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = s;
		}
	}
	return best;
}

// FFBotHelpers::FindOwnCapPoint and FFBotHelpers::IsBotCarryingFlag now live
// in ff_bot_helpers.cpp — used here, in the engineer/demo class drivers, and
// in the autobalance manager.


//-----------------------------------------------------------------------------
// Find the closest nav area tagged FF_NAV_HUNTED_ESCAPE — the goal for an
// escaping civilian (Hunted-mode VIP). Returns NULL when no escape area is
// tagged on the current map.
//-----------------------------------------------------------------------------
static CNavArea *FindNearestHuntedEscapeArea( const Vector &fromPos )
{
	CNavArea *best = NULL;
	float bestDistSq = FLT_MAX;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area->HasAttributeFF( FF_NAV_HUNTED_ESCAPE ) )
			continue;
		const float dSq = ( area->GetCenter() - fromPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}
	return best;
}


//-----------------------------------------------------------------------------
// Filter mirroring CFFBotWander::IsAcceptableWanderGoal — keeps the fallback
// pickup behavior consistent. Reject AVOID/blocker areas, in-water areas,
// areas beyond wander radius, and areas near our recent stuck spot.
//-----------------------------------------------------------------------------
static bool IsAcceptableWanderGoal( const CNavArea *area, const Vector &fromPos,
									const Vector &avoidPos, float avoidRadius )
{
	if ( !area )
		return false;
	if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
		return false;

	const Vector center = area->GetCenter();

	int contents = enginetrace->GetPointContents( center );
	if ( contents & ( CONTENTS_WATER | CONTENTS_SLIME ) )
		return false;

	const float distSq = ( center - fromPos ).LengthSqr();
	if ( distSq > FFBOT_CTF_WANDER_RADIUS * FFBOT_CTF_WANDER_RADIUS )
		return false;

	if ( avoidRadius > 0.0f )
	{
		const float aSq = ( center - avoidPos ).LengthSqr();
		if ( aSq < avoidRadius * avoidRadius )
			return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
CFFBotCtfObjective::CFFBotCtfObjective()
{
	m_state = STATE_NONE;
	m_goalPos.Init();
	m_lastStuckPos.Init();
	m_avoidStuckRadius = 0.0f;
	m_wasAlive = false;
	m_needsAngleSnap = false;
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotCtfObjective::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( 300.0f );
	m_evaluateTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_wanderPickTimer.Invalidate();
	m_state = STATE_NONE;
	m_wasAlive = false;	// pretend we just respawned so first tick re-evaluates
	m_needsAngleSnap = true;
	return Continue();
}

//-----------------------------------------------------------------------------
bool CFFBotCtfObjective::PickWanderGoal( const Vector &myPos, Vector *outGoalPos ) const
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() || TheNavAreas.Count() == 0 )
		return false;

	for ( int attempt = 0; attempt < 30; ++attempt )
	{
		int idx = RandomInt( 0, TheNavAreas.Count() - 1 );
		CNavArea *cand = TheNavAreas[ idx ];
		if ( IsAcceptableWanderGoal( cand, myPos, m_lastStuckPos, m_avoidStuckRadius ) )
		{
			*outGoalPos = cand->GetCenter();
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
CFFBotCtfObjective::State CFFBotCtfObjective::EvaluateState(
	CFFBot *me, Vector *outGoalPos, EHANDLE *outTargetEnt ) const
{
	const int myTeam = me->GetTeamNumber();
	const int myClass = me->GetClassSlot();
	const Vector myPos = me->GetAbsOrigin();

	// Class role split. Defensive classes anchor near a defensive position
	// instead of running offense:
	//   - Engineer: must be near sentry/dispenser to maintain them.
	//   - Sniper:   posts up at our cap area for sight lines on attackers.
	//   - HWGuy:    too slow to run flags; holds chokes near our flag.
	const bool isEngineerRole = ( myClass == CLASS_ENGINEER );
	const bool isSniperRole   = ( myClass == CLASS_SNIPER );
	const bool isHWGuyRole    = ( myClass == CLASS_HWGUY );

	// Engineer can transition from defensive (need to build/upgrade/repair)
	// to offensive (everything topped up, time to go fight). Computed below
	// before we lock in isDefensiveClass for the rest of the eval.
	bool engineerToppedUp = false;
	if ( isEngineerRole )
	{
		CFFSentryGun *sg = me->GetSentryGun();
		const bool hasDispenser = ( me->GetDispenser() != NULL );
		if ( sg && hasDispenser )
		{
			const float sgDist = ( sg->GetAbsOrigin() - myPos ).Length();
			const bool sgWounded = sg->GetHealth() < ( sg->GetMaxHealth() * 0.85f );
			const bool sgSapped  = sg->IsSabotaged();
			const bool sgUpgrading = sg->GetLevel() < 3;
			const bool needsAttentionAndClose =
				( sgWounded || sgSapped ) && ( sgDist < 800.0f );
			if ( !sgUpgrading && !needsAttentionAndClose )
				engineerToppedUp = true;
		}
	}

	// Engineer is "defensive" only while still building / upgrading / their
	// home is under attack. Once topped up, they roam like an offensive
	// class.
	const bool isDefensiveClass =
		( isEngineerRole && !engineerToppedUp ) ||
		isSniperRole ||
		isHWGuyRole;

	// 0a) Hunted: if I'm the civilian (VIP), my only goal is to run to the
	// nearest hunted-escape area.
	if ( myClass == CLASS_CIVILIAN )
	{
		CNavArea *escape = FindNearestHuntedEscapeArea( myPos );
		if ( escape )
		{
			outTargetEnt->Term();
			*outGoalPos = escape->GetCenter();
			return STATE_VIP_RUN;
		}
	}

	// 0b) Hunted: my team has a civilian → escort them. Defensive classes
	// (engineer / sniper / hwguy) still stick to their post.
	if ( !isDefensiveClass && myClass != CLASS_CIVILIAN )
	{
		CFFPlayer *vip = FFBotHelpers::FindFriendlyCivilian( myTeam );
		if ( vip )
		{
			outTargetEnt->Set( vip );
			*outGoalPos = vip->GetAbsOrigin();
			return STATE_ESCORT_VIP;
		}
	}

	// 0b.5) Infected → seek a friendly medic for cure. Higher priority than
	// objective; the DoT will kill us otherwise. If no medic on team, the
	// regular flow continues (we'll just take the damage).
	if ( me->IsInfected() )
	{
		CFFPlayer *medic = FFBotHelpers::FindNearestFriendlyMedic( myTeam, myPos );
		if ( medic )
		{
			outTargetEnt->Set( medic );
			*outGoalPos = medic->GetAbsOrigin();
			return STATE_SEEK_CURE;
		}
	}

	// 0c) Low HP retreat: if I'm hurt and not carrying a flag, fall back to
	// our spawn area to recover. Higher priority than fighting since we'd
	// just feed the enemy. Carriers always push through.
	//
	// The retreat threshold scales with team aggression: a losing team
	// pushes through more pain (lower threshold), a winning team plays
	// safe (higher threshold). 25% baseline.
	const bool carryingFlag = FFBotHelpers::IsBotCarryingFlag( me, NULL );
	const int health = me->GetHealth();
	const int maxHealth = me->GetMaxHealth();
	const float aggression = FFBotIntel::GetTeamAggression( myTeam );
	const int retreatPct = (int)( 25.0f / aggression );	// aggression > 1 → smaller retreat zone
	if ( !carryingFlag && maxHealth > 0 && health < ( ( maxHealth * retreatPct ) / 100 ) )
	{
		// Path back to one of our team's spawn-room areas.
		CFFNavMesh *mesh = TheFFNavMesh();
		const CUtlVector< CFFNavArea * > *spawnAreas = mesh ? mesh->GetSpawnRoomAreas( myTeam ) : NULL;
		if ( spawnAreas && spawnAreas->Count() > 0 )
		{
			CFFNavArea *area = ( *spawnAreas )[ 0 ];
			outTargetEnt->Term();
			*outGoalPos = area->GetCenter();
			return STATE_RETREAT;
		}
	}

	// 1) Carrying a flag → run it to our cap point.
	if ( carryingFlag )
	{
		CFFInfoScript *cap = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
		if ( cap )
		{
			outTargetEnt->Set( cap );
			*outGoalPos = cap->GetAbsOrigin();
			return STATE_CARRY_FLAG;
		}
	}

	// 2) Our own flag is dropped — return takes priority over everything else.
	CFFInfoScript *ownFlag = FFBotHelpers::FindOwnFlag( myTeam );
	if ( ownFlag && ownFlag->IsDropped() )
	{
		outTargetEnt->Set( ownFlag );
		*outGoalPos = ownFlag->GetAbsOrigin();
		return STATE_RETURN_OWN_FLAG;
	}

	// 2.5) Our flag is stolen — chase the enemy carrier. Higher priority than
	// escorting our own flag-carrier: the stolen flag is on a clock and the
	// carrier dies when we kill them, so killing them is what matters.
	// Defensive classes (engineer/sniper) keep their post — the carrier will
	// come back through their lane on the way to cap. Offensive classes
	// converge on the carrier to gun them down.
	CFFPlayer *enemyCarrier = FFBotHelpers::FindEnemyCarryingOurFlag( myTeam );
	if ( enemyCarrier && !isDefensiveClass )
	{
		outTargetEnt->Set( enemyCarrier );
		*outGoalPos = enemyCarrier->GetAbsOrigin();
		return STATE_INTERCEPT_CARRIER;
	}

	// 3) Escort a teammate carrying the enemy flag — non-defensive offensive
	// classes converge on the carrier so they don't get assassinated alone.
	// (This is what made bots feel aimless before: enemy flag was carried, so
	// no GRAB target existed, and bots fell through to wander.)
	if ( !isDefensiveClass )
	{
		CFFPlayer *carrierAlly = FFBotHelpers::FindTeammateCarryingEnemyFlag( me );
		if ( carrierAlly )
		{
			outTargetEnt->Set( carrierAlly );
			*outGoalPos = carrierAlly->GetAbsOrigin();
			return STATE_ESCORT_CARRIER;
		}
	}

	// 4) Reactive defense. Tighter than before: enemy must be IN the flag
	// area (600u, ~the flag room) AND either have LOS or be moving toward
	// the flag. "Enemy crossing midfield" no longer pulls everyone home.
	const bool flagApproaching = ownFlag &&
		FFBotHelpers::IsEnemyApproachingOwnFlag( myTeam, 600.0f, NULL );

	// 5) Defensive class anchoring.
	if ( isEngineerRole )
	{
		CFFSentryGun *sg = me->GetSentryGun();
		const bool hasDispenser = ( me->GetDispenser() != NULL );

		// Single-defender quota: only the engineer closest to our flag
		// claims the flag-room sentry slot. Other engineers (without an
		// SG of their own and where a friendly SG already covers home)
		// push forward to build a *forward* sentry on the way to the
		// enemy's flag — TFBot-style "battle engineer".
		const bool isHomeEngineer = ( ownFlag != NULL ) &&
			( FFBotHelpers::FindClosestAliveEngineer( myTeam,
				ownFlag->GetAbsOrigin() ) == me );

		// 5a) Engineer with no SG yet: home-engineer goes to FF_NAV_SENTRY_SPOT
		// nearest the flag. Other engineers go to the next-nearest unclaimed
		// FF_NAV_SENTRY_SPOT, falling through to offense if none.
		if ( !sg )
		{
			const Vector flagPos = ownFlag ? ownFlag->GetAbsOrigin() : myPos;

			CFFNavArea *spot = NULL;
			float bestScore = -FLT_MAX;
			for ( int i = 0; i < TheNavAreas.Count(); ++i )
			{
				CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
				if ( !area->HasAttributeFF( FF_NAV_SENTRY_SPOT ) )
					continue;
				const Vector spotPos = area->GetCenter();

				// Skip spots already covered by a friendly SG.
				if ( FFBotHelpers::IsFriendlySentryNear( myTeam, spotPos, 400.0f ) )
					continue;

				// Score: home engineer prefers spots near the flag; non-home
				// engineers prefer spots far from the flag (forward push).
				const float distToFlag = ( spotPos - flagPos ).Length();
				const float score = isHomeEngineer ? -distToFlag : distToFlag;
				if ( score > bestScore )
				{
					bestScore = score;
					spot = area;
				}
			}
			if ( spot )
			{
				outTargetEnt->Term();
				*outGoalPos = spot->GetCenter();
				return STATE_DEFEND_OWN_FLAG;
			}

			// No untaken sentry spot found.
			if ( isHomeEngineer && ownFlag )
			{
				// Home engineer: build at the flag itself.
				outTargetEnt->Set( ownFlag );
				*outGoalPos = ownFlag->GetAbsOrigin();
				return STATE_DEFEND_OWN_FLAG;
			}
			// Non-home engineer with nowhere good to build: fall through
			// to offense — they'll attempt the build wherever they end
			// up (the class driver has its own friendlyNear guard).
		}
		else
		{
			// 5b) Engineer has an SG. Decide whether to stay home or roam.
			const float sgDist = ( sg->GetAbsOrigin() - myPos ).Length();
			const bool sgWounded = sg->GetHealth() < ( sg->GetMaxHealth() * 0.85f );
			const bool sgSapped  = sg->IsSabotaged();
			const bool sgUpgrading = sg->GetLevel() < 3;
			const bool sgNeedsHelp = sgWounded || sgSapped;
			const bool closeEnoughToHelp = ( sgDist < 800.0f );

			if ( ( sgNeedsHelp && closeEnoughToHelp ) ||
				 sgUpgrading ||
				 !hasDispenser ||
				 flagApproaching )
			{
				// Stay near the SG to repair / upgrade / build dispenser /
				// defend the home base.
				outTargetEnt->Set( sg );
				*outGoalPos = sg->GetAbsOrigin();
				return STATE_DEFEND_OWN_FLAG;
			}

			// SG is fully built (level 3), dispenser is up, no immediate
			// threat to flag, SG isn't bleeding, nothing pressing at home.
			// Engineer falls through to offense behavior — no more
			// camping.
		}
	}
	else if ( isSniperRole )
	{
		// Snipers normally route through CFFBotSniperLurk (set as the root
		// action for sniper class in CFFBotMainAction::InitialContainedAction).
		// CtfObjective only runs for sniper if Lurk is suspended for some
		// reason — keep a sane fallback to cap area so the bot doesn't
		// stand still.
		CFFInfoScript *cap = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
		if ( cap )
		{
			outTargetEnt->Set( cap );
			*outGoalPos = cap->GetAbsOrigin();
			return STATE_DEFEND_AT_CAP;
		}
		if ( ownFlag )
		{
			outTargetEnt->Set( ownFlag );
			*outGoalPos = ownFlag->GetAbsOrigin();
			return STATE_DEFEND_OWN_FLAG;
		}
	}
	else if ( isHWGuyRole )
	{
		// HWGuy holds a choke between our flag and the nearest enemy spawn
		// exit, NOT the flag itself. Picks the spawn-room threshold area
		// for any enemy team that's closest to our flag — that's the
		// doorway the enemy comes through to reach our flag, which is
		// where the AC's spinup matters most.
		CFFNavMesh *mesh = TheFFNavMesh();
		Vector chokePos;
		bool gotChoke = false;
		if ( mesh && ownFlag )
		{
			const Vector flagPos = ownFlag->GetAbsOrigin();
			float bestDistSq = FLT_MAX;
			for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
			{
				if ( t == myTeam )
					continue;
				CUtlVector< CFFNavArea * > thresholds;
				mesh->CollectSpawnRoomThresholdAreas( t, &thresholds );
				for ( int i = 0; i < thresholds.Count(); ++i )
				{
					const Vector p = thresholds[ i ]->GetCenter();
					const float dSq = ( p - flagPos ).LengthSqr();
					if ( dSq < bestDistSq )
					{
						bestDistSq = dSq;
						chokePos = p;
						gotChoke = true;
					}
				}
			}
		}
		if ( gotChoke )
		{
			// Stand 2/3 of the way from flag to choke — close enough to
			// flag that we can rotate, still spun up at the doorway.
			const Vector flagPos = ownFlag->GetAbsOrigin();
			outTargetEnt->Term();
			*outGoalPos = flagPos + ( chokePos - flagPos ) * 0.66f;
			return STATE_DEFEND_OWN_FLAG;
		}
		// No choke computable — fall back to flag.
		if ( ownFlag )
		{
			outTargetEnt->Set( ownFlag );
			*outGoalPos = ownFlag->GetAbsOrigin();
			return STATE_DEFEND_OWN_FLAG;
		}
	}
	else if ( flagApproaching && ownFlag )
	{
		// Non-defensive class and an enemy is actually pushing the flag.
		// Quota: only the closest non-defensive bot to the flag responds.
		// Others stay on offense.
		const float myDistToFlagSq = ( me->GetAbsOrigin() - ownFlag->GetAbsOrigin() ).LengthSqr();
		bool iAmClosest = true;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
			if ( !pp || pp == me || !pp->IsAlive() )
				continue;
			if ( pp->GetTeamNumber() != myTeam )
				continue;
			const int slot = pp->GetClassSlot();
			if ( slot == CLASS_ENGINEER || slot == CLASS_SNIPER || slot == CLASS_HWGUY )
				continue;	// already defensive or has its own logic
			if ( slot == CLASS_CIVILIAN )
				continue;
			const float dSq = ( pp->GetAbsOrigin() - ownFlag->GetAbsOrigin() ).LengthSqr();
			if ( dSq < myDistToFlagSq )
			{
				iAmClosest = false;
				break;
			}
		}
		if ( iAmClosest )
		{
			outTargetEnt->Set( ownFlag );
			*outGoalPos = ownFlag->GetAbsOrigin();
			return STATE_DEFEND_OWN_FLAG;
		}
	}

	// 6) Offensive classes try to grab the closest enemy flag (default).
	if ( !isDefensiveClass )
	{
		CFFInfoScript *enemyFlag = FindClosestEnemyFlag( myTeam, myPos );
		if ( enemyFlag )
		{
			outTargetEnt->Set( enemyFlag );
			*outGoalPos = enemyFlag->GetAbsOrigin();
			return STATE_GRAB_FLAG;
		}
	}

	// 5) AvD-style maps and similar: no flag goal exists, but a cap point
	// does. Both teams converge on the cap. Defenders hold it; attackers
	// push to it. Either way, going to the cap puts us where the action is.
	{
		CFFInfoScript *cap = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
		if ( !cap )
			cap = FFBotHelpers::FindAnyCapPoint( myPos );
		if ( cap )
		{
			outTargetEnt->Set( cap );
			*outGoalPos = cap->GetAbsOrigin();
			return STATE_PUSH_OBJECTIVE;
		}
	}

	// 6) Nothing objective-relevant — wander (DM maps, conc-jump, etc.).
	if ( PickWanderGoal( myPos, outGoalPos ) )
	{
		outTargetEnt->Term();
		return STATE_WANDER;
	}

	// Wander failed (rare — usually means bot is in a corner pocket with
	// nothing within wander radius that's traversable). Final fallback so
	// the bot doesn't freeze in spawn: aim at our team's spawn-room
	// threshold area, which is always populated if we have spawn rooms.
	{
		CFFNavMesh *mesh = TheFFNavMesh();
		if ( mesh )
		{
			CUtlVector< CFFNavArea * > thresholds;
			mesh->CollectSpawnRoomThresholdAreas( myTeam, &thresholds );
			if ( thresholds.Count() > 0 )
			{
				CFFNavArea *pick = thresholds[ RandomInt( 0, thresholds.Count() - 1 ) ];
				outTargetEnt->Term();
				*outGoalPos = pick->GetCenter();
				return STATE_WANDER;
			}
		}
	}

	return STATE_NONE;
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotCtfObjective::Update( CFFBot *me, float interval )
{
	// Skip path/state work entirely while dead. Computing a path from a
	// corpse origin produces a stale path that fights the locomotor on
	// respawn — that's why bots stood around facing wrong directions
	// after spawning.
	if ( !me->IsAlive() )
	{
		m_wasAlive = false;
		return Continue();
	}

	// Detect dead → alive transition. On the first tick alive after a
	// respawn, scrub all state so re-evaluation runs immediately and the
	// path recomputes from the fresh spawn position.
	if ( !m_wasAlive )
	{
		m_state = STATE_NONE;
		m_evaluateTimer.Invalidate();
		m_repathTimer.Invalidate();
		m_wanderPickTimer.Invalidate();
		m_path.Invalidate();
		m_targetEntity.Term();
		m_wasAlive = true;
		m_needsAngleSnap = true;
	}

	// Skip everything if the nav mesh isn't loaded — we have nowhere to path.
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return Continue();

	// Re-evaluate goal periodically (cheaper than every tick — entity scans
	// would otherwise dominate per-bot CPU).
	const bool firstTick = ( m_state == STATE_NONE );
	if ( firstTick || m_evaluateTimer.IsElapsed() )
	{
		Vector newGoal;
		EHANDLE newTarget;
		State newState = EvaluateState( me, &newGoal, &newTarget );

		const bool stateChanged  = ( newState != m_state );
		const bool targetChanged = ( newTarget.Get() != m_targetEntity.Get() );
		const bool goalMoved     = ( newGoal - m_goalPos ).LengthSqr() > ( 32.0f * 32.0f );

		if ( newState != STATE_NONE && ( firstTick || stateChanged || targetChanged || goalMoved ) )
		{
			m_state         = newState;
			m_goalPos       = newGoal;
			m_targetEntity  = newTarget;
			m_repathTimer.Invalidate();		// recompute path immediately
			m_avoidStuckRadius = 0.0f;		// stale stuck-avoidance only counts for one pick
		}

		// Wander state picks a fresh random goal less often than evaluation.
		if ( m_state == STATE_WANDER )
			m_wanderPickTimer.Start( RandomFloat( 8.0f, 15.0f ) );

		m_evaluateTimer.Start( RandomFloat( 0.75f, 1.25f ) );
	}

	// In wander state, also re-pick after the picker timer has elapsed even
	// if state didn't change — keeps the bot moving on idle DM-style maps.
	if ( m_state == STATE_WANDER && m_wanderPickTimer.IsElapsed() )
	{
		Vector newGoal;
		if ( PickWanderGoal( me->GetAbsOrigin(), &newGoal ) )
		{
			m_goalPos = newGoal;
			m_repathTimer.Invalidate();
			m_avoidStuckRadius = 0.0f;
		}
		m_wanderPickTimer.Start( RandomFloat( 8.0f, 15.0f ) );
	}

	if ( m_state == STATE_NONE )
		return Continue();

	// Path inhibit: when MainAction's stuck-recovery is actively pushing
	// the bot backward / lateral, we must NOT also drive the path
	// follower. The locomotor's per-tick Approach() would re-press
	// IN_FORWARD and cancel out our backward press. Skip path.Update
	// while inhibited; bot uses only direct button presses for that
	// window.
	if ( me->m_pathInhibitTimer.HasStarted() && !me->m_pathInhibitTimer.IsElapsed() )
		return Continue();

	// Standing in a fresh fire / gas / slow / sticky zone? Force an
	// immediate repath. The current path was computed before this danger
	// existed and walks straight through it — without this nudge the bot
	// stays on the bad waypoint until the regular m_repathTimer fires
	// (~1.5s) and burns the whole time. Throttled so the scan only runs
	// every ~0.25s — just often enough to react quickly without churning.
	if ( !m_dangerCheckTimer.HasStarted() || m_dangerCheckTimer.IsElapsed() )
	{
		m_dangerCheckTimer.Start( 0.25f );
		if ( IsStandingInGrenadeDanger( me->GetTeamNumber(), me->GetAbsOrigin() ) )
		{
			m_repathTimer.Invalidate();
		}
	}

	// Recompute path occasionally and when the goal-target moves (e.g., the
	// flag carrier we're chasing is moving).
	if ( m_repathTimer.IsElapsed() )
	{
		// Re-fetch the current goal position from the target entity in case
		// it moved (carrier running, dropped flag re-positioned, etc.).
		CBaseEntity *target = m_targetEntity.Get();
		if ( target )
			m_goalPos = target->GetAbsOrigin();

		m_repathTimer.Start( RandomFloat( 0.75f, 1.5f ) );
		CFFBotPathCost cost( me, FFBOT_DEFAULT_ROUTE );
		m_path.Compute( me, m_goalPos, cost );

		// Post-respawn angle snap: align eyes with first path segment so
		// PlayerLocomotion::Approach (which reads EyeVectors and dots
		// against goal direction to pick IN_FORWARD vs IN_BACK) sees a
		// positive ahead-dot on the very first tick, instead of pressing
		// IN_BACK because the spawn entity's facing was opposite the
		// route. SnapEyeAngles is required, not AimHeadTowards: the body
		// only slews toward AimHeadTowards over many frames, during which
		// the locomotor reads stale EyeVectors and walks the bot backward
		// into walls.
		//
		// We also call AimHeadTowards so PlayerBody::Upkeep's per-tick
		// SnapEyeAngles (which slews from current toward m_lookAtPos)
		// targets the same direction — otherwise Upkeep would slew us
		// right back to wherever m_lookAtPos pointed before respawn.
		//
		// IMPORTANT: m_path[0].pos == bot's start position, so direction
		// must come from first->forward (the unit vector along the
		// segment), NOT from (first->pos - bot.pos), which is always
		// near-zero and silently fails the normalize check.
		if ( m_needsAngleSnap )
		{
			Vector dir = vec3_origin;
			if ( m_path.IsValid() )
			{
				const Path::Segment *first = m_path.FirstSegment();
				if ( first )
					dir = first->forward;
			}
			// Fallback when the path didn't compute: snap toward the goal
			// pos directly. Better than no snap at all — the goal is
			// usually in the right hemisphere even if the path bends.
			if ( dir.IsZero() )
				dir = m_goalPos - me->GetAbsOrigin();

			dir.z = 0.0f;
			if ( dir.NormalizeInPlace() > 0.1f )
			{
				QAngle angles;
				VectorAngles( dir, angles );
				me->SnapEyeAngles( angles );

				IBody *body = me->GetBodyInterface();
				if ( body )
				{
					Vector lookAt = me->EyePosition() + dir * 200.0f;
					body->AimHeadTowards( lookAt, IBody::IMPORTANT, 0.3f,
						NULL, "Post-respawn aim align" );
				}

				m_needsAngleSnap = false;
			}
		}
	}

	m_path.Update( me );

	return Continue();
}

//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotCtfObjective::OnStuck( CFFBot *me )
{
	// Wander handling: avoid the stuck spot for the next pick. CTF goals
	// (flag/cap) we can't side-step — those have to be reached, but the
	// per-tick door-mashing in CFFBotMainAction::HandleStuckState handles
	// the door case. Drop the path so it recomputes on the next tick.
	m_lastStuckPos = me->GetAbsOrigin();
	m_avoidStuckRadius = 512.0f;
	m_repathTimer.Invalidate();
	m_path.Invalidate();
	return TryContinue();
}
