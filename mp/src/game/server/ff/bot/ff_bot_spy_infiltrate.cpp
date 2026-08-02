//========= Fortress Forever Bot =============================================//
//
// CFFBotSpyInfiltrate — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_spy_infiltrate.h"
#include "ff_bot_helpers.h"
#include "ff_bot_spy_attack.h"
#include "ff_bot_spy_sap.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_buildableobject.h"
#include "ff_player.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "NextBotInterface.h"
#include "NextBotLocomotionInterface.h"
#include "NextBotVisionInterface.h"
#include "NextBotKnownEntity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpyInfiltrate::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_hideArea = NULL;
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpyInfiltrate::Update( CFFBot *me, float interval )
{
	CFFNavArea *myArea = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
	if ( !myArea )
		return Continue();

	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat() : NULL;

	// Sap opportunity: primary threat is an enemy buildable.
	if ( threat && threat->GetEntity() )
	{
		CFFBuildableObject *building = dynamic_cast< CFFBuildableObject * >( threat->GetEntity() );
		if ( building && building->GetTeamNumber() != me->GetTeamNumber() &&
			 building->CanSabotage() && !building->IsSabotaged() )
		{
			return SuspendFor( new CFFBotSpySap( building ), "Sapping known enemy building" );
		}
	}

	// Standalone scan for any sappable building (not necessarily the primary
	// threat — TFBot also kicks off sap from cached enemy-sentry memory).
	CFFBuildableObject *anySap = CFFBotSpySap::FindNearestSappableTarget( me );
	if ( anySap && me->IsRangeLessThan( anySap, 1500.0f ) )
	{
		return SuspendFor( new CFFBotSpySap( anySap ), "Sapping nearby enemy building" );
	}

	// Backstab opportunity: enemy player who's deeper in their own territory
	// than we are (i.e., we got behind them).
	if ( threat && threat->GetEntity() && threat->GetTimeSinceLastKnown() < 3.0f )
	{
		CFFPlayer *victim = ToFFPlayer( threat->GetEntity() );
		if ( victim )
		{
			CFFNavArea *victimArea = static_cast< CFFNavArea * >( victim->GetLastKnownArea() );
			if ( victimArea )
			{
				const int victimTeam = victim->GetTeamNumber();
				if ( victimArea->GetIncursionDistance( victimTeam ) >
					 myArea->GetIncursionDistance( victimTeam ) )
				{
					return SuspendFor( new CFFBotSpyAttack( victim ),
						"Going after a backstab victim" );
				}
			}
		}
	}

	// Pick a hiding spot if we don't have one or it's stale.
	if ( !m_hideArea && m_findHidingSpotTimer.IsElapsed() )
	{
		FindHidingSpot( me );
		m_findHidingSpotTimer.Start( 3.0f );
	}

	if ( m_hideArea )
	{
		if ( myArea == m_hideArea )
		{
			// Sit and wait. After 5-10s, pick a new spot to keep us mobile.
			if ( !m_waitTimer.HasStarted() )
			{
				m_waitTimer.Start( RandomFloat( 5.0f, 10.0f ) );
			}
			else if ( m_waitTimer.IsElapsed() )
			{
				m_hideArea = NULL;
				m_waitTimer.Invalidate();
			}
		}
		else
		{
			// Move to ambush position.
			if ( m_repathTimer.IsElapsed() )
			{
				m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
				// FIX 7 — repath hysteresis: leave a working path alone while the bot
				// is actually travelling it. Volatile cost terms (combat intensity,
				// grenade danger, ammo/health discounts, recent-stuck penalties) used
				// to flip A* between near-equal lanes on consecutive repaths.
				if ( FFBotHelpers::ShouldRecomputePath( me, m_path, m_hideArea->GetCenter() ) )
				{
					CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
					m_path.Compute( me, m_hideArea->GetCenter(), cost );
				}
			}
			// FIX 1 — single movement authority. CanDrivePath publishes the
			// path goal for the aim driver and refuses while the movement
			// arbiter owns locomotion, so this can never issue a second,
			// contradictory Approach() in the same tick.
			if ( FFBotHelpers::CanDrivePath( me, m_path ) )
				m_path.Update( me );
			m_waitTimer.Invalidate();
		}
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpyInfiltrate::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	m_repathTimer.Invalidate();
	m_hideArea = NULL;
	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSpyInfiltrate::OnStuck( CFFBot *me )
{
	m_hideArea = NULL;
	m_findHidingSpotTimer.Invalidate();
	return TryContinue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSpyInfiltrate::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	// Don't blow our cover by firing while infiltrating — let the spy attack
	// sub-action decide whether to engage.
	return ANSWER_NO;
}


//-----------------------------------------------------------------------------
// Trace-based "is area visible from any enemy spawn exit" check.
//-----------------------------------------------------------------------------
static bool IsAreaVisibleFromSpawnExits( CFFNavArea *area, const CUtlVector< CFFNavArea * > &spawnExits )
{
	const Vector areaEye = area->GetCenter() + Vector( 0, 0, 64.0f );
	for ( int i = 0; i < spawnExits.Count(); ++i )
	{
		const Vector exitEye = spawnExits[ i ]->GetCenter() + Vector( 0, 0, 64.0f );
		trace_t tr;
		UTIL_TraceLine( exitEye, areaEye, MASK_VISIBLE_AND_NPCS, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 0.99f )
			return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
bool CFFBotSpyInfiltrate::FindHidingSpot( CFFBot *me )
{
	m_hideArea = NULL;

	CFFNavMesh *ffMesh = TheFFNavMesh();
	if ( !ffMesh )
		return false;

	// Collect enemy spawn-room exit areas across every enemy team.
	CUtlVector< CFFNavArea * > enemyExits;
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == me->GetTeamNumber() )
			continue;
		const CUtlVector< CFFNavArea * > *exits = ffMesh->GetSpawnRoomExitAreas( t );
		if ( exits )
		{
			for ( int i = 0; i < exits->Count(); ++i )
				enemyExits.AddToTail( ( *exits )[ i ] );
		}
	}

	if ( enemyExits.Count() == 0 )
		return false;

	// Collect candidate areas within ~2500u travel of any enemy exit.
	ILocomotion *loco = me->GetLocomotionInterface();
	const float stepHeight = loco ? loco->GetStepHeight() : 18.0f;

	CUtlVector< CNavArea * > nearby;
	const float nearbyHideRange = 2500.0f;
	for ( int x = 0; x < enemyExits.Count(); ++x )
	{
		CUtlVector< CNavArea * > local;
		CollectSurroundingAreas( &local, enemyExits[ x ], nearbyHideRange, stepHeight, stepHeight );
		nearby.AddVectorToTail( local );
	}

	// Filter to areas not visible from any enemy exit. Falling back to the
	// raw nearby list if none qualify. Always reject underwater — a spy
	// hiding submerged can't backstab anyone, can't really hide from
	// anyone (bubbles + slow swim). Surface water is fine.
	CUtlVector< CFFNavArea * > hidden;
	for ( int i = 0; i < nearby.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( nearby[ i ] );
		if ( !loco->IsAreaTraversable( area ) )
			continue;
		if ( area->HasAttributeFF( FF_NAV_UNDERWATER ) )
			continue;
		if ( IsAreaVisibleFromSpawnExits( area, enemyExits ) )
			continue;
		hidden.AddToTail( area );
	}

	if ( hidden.Count() == 0 )
	{
		// Fallback: any traversable, non-submerged area near an exit.
		for ( int i = 0; i < nearby.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( nearby[ i ] );
			if ( !loco->IsAreaTraversable( area ) )
				continue;
			if ( area->HasAttributeFF( FF_NAV_UNDERWATER ) )
				continue;
			hidden.AddToTail( area );
		}
	}

	if ( hidden.Count() == 0 )
	{
		// Last resort: pick a random enemy spawn exit.
		m_hideArea = enemyExits[ RandomInt( 0, enemyExits.Count() - 1 ) ];
		return false;
	}

	m_hideArea = hidden[ RandomInt( 0, hidden.Count() - 1 ) ];
	return true;
}
