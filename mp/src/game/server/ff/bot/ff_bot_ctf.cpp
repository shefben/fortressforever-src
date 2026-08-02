//========= Fortress Forever Bot =============================================//
//
// CFFBotCtfObjective — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_ctf.h"
#include "ff_bot_helpers.h"
#include "ff_bot_lua_objectives.h"
#include "ff_bot_gamemode.h"
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

	// Class shapes HOW a bot defends. Whether it defends at all is a team-level
	// decision made by FFBotGameMode's quota, because left to themselves every
	// bot picks offense and a team of eight attackers loses every attack/defend
	// map no matter how good its navigation is.
	//
	// The class branches below still exist and still matter — an engineer
	// defends by building, a sniper by holding an angle, an HWGuy by sitting on
	// a choke — they are just gated on the role now instead of being the role.
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

	// The quota's answer.
	const bool isDefensiveRole = ( me->m_botRole == FFROLE_DEFENSE );

	// ...plus one class exception that is not a role question. An engineer with
	// no buildings has to go and build them wherever it was sent; that is the
	// class, not the assignment. Once topped up they roam like anyone else.
	const bool isDefensiveClass =
		isDefensiveRole ||
		( isEngineerRole && !engineerToppedUp );

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
		CBaseEntity *cap = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
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
				if ( !area->HasAttributeFF( FF_NAV_SENTRY_SPOT | FF_NAV_AUTO_SENTRY_SPOT ) )
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
	else if ( isSniperRole && isDefensiveRole )
	{
		// Snipers normally route through CFFBotSniperLurk (set as the root
		// action for sniper class in CFFBotMainAction::InitialContainedAction).
		// CtfObjective only runs for sniper if Lurk is suspended for some
		// reason — keep a sane fallback to cap area so the bot doesn't
		// stand still.
		CBaseEntity *cap = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
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
	else if ( isHWGuyRole && isDefensiveRole )
	{
		// HWGuy holds a choke between our flag and the nearest enemy
		// approach, NOT the flag itself. Priority order:
		//   1. A real FF_NAV_CHOKE area within 1500u of our flag, on the
		//      line toward an enemy spawn-room threshold. Choke detection
		//      from CFFBotAutoTagger picks narrow corridors and doorways
		//      precisely where the AC spinup matters most.
		//   2. Mid-point between flag and nearest enemy threshold (old
		//      behavior — works when the auto-tagger didn't find a choke).
		//   3. The flag itself (last resort).
		if ( ownFlag )
		{
			const Vector flagPos = ownFlag->GetAbsOrigin();

			// Step 1: find the nearest enemy spawn-room threshold to know
			// which "side" of the map the attackers come from.
			CFFNavMesh *mesh = TheFFNavMesh();
			Vector enemyThresholdPos;
			bool gotThreshold = false;
			if ( mesh )
			{
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
							enemyThresholdPos = p;
							gotThreshold = true;
						}
					}
				}
			}

			// Step 2: pick the FF_NAV_CHOKE area closest to the line
			// between flag and enemy threshold. Score by distance from
			// flag (closer wins, so HWGuy doesn't push too far forward)
			// plus penalty for going past the threshold (we want to
			// hold OUR side of the choke, not theirs).
			CFFNavArea *bestChoke = NULL;
			if ( gotThreshold )
			{
				float bestScore = FLT_MAX;
				for ( int i = 0; i < TheNavAreas.Count(); ++i )
				{
					CFFNavArea *cand = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
					const unsigned int attrs = cand->GetAttributesFF();
					if ( !( attrs & FF_NAV_CHOKE ) )
						continue;
					if ( attrs & ( FF_NAV_SPAWN_ROOM_ANY | FF_NAV_UNDERWATER ) )
						continue;
					const Vector p = cand->GetCenter();
					const float distFromFlag = ( p - flagPos ).Length();
					if ( distFromFlag > 1500.0f )
						continue;
					// Distance from flag is the primary score (smaller is
					// better — HWGuy holds close). Push slightly toward
					// the enemy threshold by adding a small reward for
					// alignment.
					Vector toThreshold = enemyThresholdPos - flagPos;
					Vector toCand = p - flagPos;
					if ( toThreshold.LengthSqr() > 1.0f )
					{
						toThreshold.NormalizeInPlace();
						const float forwardness = toCand.Dot( toThreshold );
						// forwardness 0..distFromFlag — reward more
						// forward chokes by ~30% of forwardness.
						const float score = distFromFlag - forwardness * 0.3f;
						if ( score < bestScore )
						{
							bestScore = score;
							bestChoke = cand;
						}
					}
				}
			}

			if ( bestChoke )
			{
				outTargetEnt->Term();
				*outGoalPos = bestChoke->GetCenter();
				return STATE_DEFEND_OWN_FLAG;
			}

			// Step 3: fall back to the old midpoint heuristic.
			if ( gotThreshold )
			{
				outTargetEnt->Term();
				*outGoalPos = flagPos + ( enemyThresholdPos - flagPos ) * 0.66f;
				return STATE_DEFEND_OWN_FLAG;
			}

			// Step 4: last resort — the flag itself.
			outTargetEnt->Set( ownFlag );
			*outGoalPos = flagPos;
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

	// 6) Fast path for the mode this state machine was built around.
	//
	// On CTF, grabbing the nearest grabbable enemy flag is the answer and the
	// resolver would arrive at the same one by a longer road. Everywhere else
	// this is skipped, because "the nearest thing I can pick up" is exactly the
	// wrong instinct on a map where a keycard has to come first.
	if ( !isDefensiveClass && FFBotGameMode::Get() == FFGAMEMODE_CTF )
	{
		CFFInfoScript *enemyFlag = FindClosestEnemyFlag( myTeam, myPos );
		if ( enemyFlag && !FFBotGameMode::IsObjectiveBlacklisted( me, enemyFlag ) )
		{
			outTargetEnt->Set( enemyFlag );
			*outGoalPos = enemyFlag->GetAbsOrigin();
			return STATE_GRAB_FLAG;
		}
	}

	// 7) Everything else is the mode layer's problem.
	//
	// This one call replaces what used to be three separate fallbacks — grab a
	// flag, walk to a cap, follow the HUD arrow — none of which knew about each
	// other, none of which knew what kind of map they were on, and none of which
	// noticed when the thing they picked was unreachable.
	//
	// It answers for every mode, it applies the sequencing rules (a keycard
	// outranks a flag, because a keycard exists to gate something), it honours
	// touch permissions, and it skips anything this bot has already failed to
	// reach. Defenders get a post out of it rather than an objective.
	{
		FFBotObjective obj;
		if ( FFBotGameMode::ResolveObjective( me, &obj ) )
		{
			if ( obj.entity )
				outTargetEnt->Set( obj.entity );
			else
				outTargetEnt->Term();

			*outGoalPos = obj.pos;

			return ( obj.kind == FFOBJ_DEFEND_POINT ) ? STATE_HOLD_GROUND
			                                          : STATE_PUSH_OBJECTIVE;
		}
	}

	// 8) Nothing objective-relevant — wander (DM maps, conc-jump, etc.).
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

		// Progress watchdog. This is the general prerequisite detector: we
		// don't know what is in the way — a locked door, a phase gate, a lift
		// parked at the wrong floor, a route the mesh doesn't have — but a bot
		// that has stopped getting closer to something for twenty-five seconds
		// has told us there is something. FFBotGameMode blacklists it and the
		// ladder moves on to whatever is next.
		//
		// Only for real objectives. Wandering and holding ground have no target
		// entity and nothing to give up on.
		if ( m_state == STATE_PUSH_OBJECTIVE || m_state == STATE_GRAB_FLAG ||
		     m_state == STATE_CARRY_FLAG )
		{
			CBaseEntity *target = m_targetEntity.Get();
			if ( target )
			{
				FFBotGameMode::NoteObjectiveProgress( me, target,
					( target->GetAbsOrigin() - me->GetAbsOrigin() ).Length() );
			}
		}
		else
		{
			FFBotGameMode::NoteObjectiveProgress( me, NULL, 0.0f );
		}
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

	// FIX 5 — stuck stage 3 asks us to give up on this goal. Honour it here
	// rather than letting the bot keep re-planning a route it cannot walk.
	if ( me->m_abandonGoalRequest )
	{
		me->m_abandonGoalRequest = false;

		// Stuck stage 3 is the movement layer reaching the same conclusion the
		// no-progress watchdog would reach eventually. Record it so the next
		// evaluation picks something else rather than the same thing again.
		if ( m_targetEntity.Get() )
			FFBotGameMode::NoteObjectiveFailure( me, m_targetEntity.Get(), "stuck trying to reach it" );

		m_lastStuckPos = me->GetAbsOrigin();
		m_avoidStuckRadius = 512.0f;
		m_path.Invalidate();
		m_repathTimer.Invalidate();

		// Force a fresh objective evaluation next tick; in wander state, pick
		// a different destination outright.
		m_evaluateTimer.Invalidate();
		if ( m_state == STATE_WANDER )
			m_wanderPickTimer.Invalidate();
		return Continue();
	}

	// The old per-action path-inhibit check lived here, and ONLY here — which
	// is why stuck recovery was a no-op in the other fifteen actions. It now
	// lives in FFBotHelpers::CanDrivePath, which every action calls.

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
	//
	// FIX 7 — repath hysteresis. Cost terms that change every tick (combat
	// intensity, grenade danger zones, low-ammo/health discounts, recent-stuck
	// penalties) used to flip A* between two near-equal lanes on consecutive
	// repaths, so the bot visibly reversed at junctions roughly once a second.
	// ShouldRecomputePath leaves a working path alone while the bot is
	// actually travelling it, and still repaths immediately when there is no
	// path, when the goal has moved, or when we've stopped making progress.
	if ( m_repathTimer.IsElapsed() )
	{
		// Re-fetch the current goal position from the target entity in case
		// it moved (carrier running, dropped flag re-positioned, etc.).
		CBaseEntity *target = m_targetEntity.Get();
		if ( target )
			m_goalPos = target->GetAbsOrigin();

		m_repathTimer.Start( RandomFloat( 0.75f, 1.5f ) );

		if ( FFBotHelpers::ShouldRecomputePath( me, m_path, m_goalPos ) )
		{
			CFFBotPathCost cost( me, FFBOT_DEFAULT_ROUTE );
			if ( !m_path.Compute( me, m_goalPos, cost ) && target != NULL )
			{
				// A* found nothing. That is the strongest possible statement
				// that this objective is gated right now — stronger than the
				// no-progress watchdog, and available immediately rather than
				// twenty-five seconds in. Drop it and re-evaluate next tick.
				FFBotGameMode::NoteObjectiveFailure( me, target, "no path to it" );
				m_evaluateTimer.Invalidate();
			}
		}

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

	// FIX 1 — single movement authority. CanDrivePath publishes the path goal
	// for the aim driver and refuses while the movement arbiter owns
	// locomotion, so this can never issue a second, contradictory Approach()
	// in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
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
