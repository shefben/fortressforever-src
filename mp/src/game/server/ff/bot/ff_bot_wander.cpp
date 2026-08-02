//========= Fortress Forever Bot =============================================//
//
// CFFBotWander — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_wander.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "engine/IEngineTrace.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Wander goals are picked from areas within this radius of the bot.
#define FFBOT_WANDER_RADIUS	2000.0f


CFFBotWander::CFFBotWander()
{
	m_hasGoal = false;
	m_goalPos.Init();
	m_lastStuckPos.Init();
	m_avoidStuckRadius = 0.0f;
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotWander::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( 300.0f );
	m_pickGoalTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_hasGoal = false;
	return Continue();
}

//-----------------------------------------------------------------------------
// Filter out goals we don't want to wander to: areas tagged AVOID, areas whose
// center is underwater (we drown there), and areas more than the wander
// radius away (so the bot doesn't try to cross the entire map for no reason).
//-----------------------------------------------------------------------------
static bool IsAcceptableWanderGoal( const CNavArea *area, const Vector &fromPos,
									const Vector &avoidPos = vec3_origin,
									float avoidRadius = 0.0f )
{
	if ( !area )
		return false;

	if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
		return false;

	const Vector center = area->GetCenter();

	// Reject areas whose center is in water — bots can't swim well yet and
	// will drown if a path takes them through deep water.
	int contents = enginetrace->GetPointContents( center );
	if ( contents & ( CONTENTS_WATER | CONTENTS_SLIME ) )
		return false;

	// Distance gate.
	const float distSq = ( center - fromPos ).LengthSqr();
	if ( distSq > FFBOT_WANDER_RADIUS * FFBOT_WANDER_RADIUS )
		return false;

	// If we recently got stuck somewhere, avoid the area near that spot —
	// stops the bot from picking goals that route through the same blocked
	// door it just hit its head on.
	if ( avoidRadius > 0.0f )
	{
		const float avoidDistSq = ( center - avoidPos ).LengthSqr();
		if ( avoidDistSq < avoidRadius * avoidRadius )
			return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotWander::Update( CFFBot *me, float interval )
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() || TheNavAreas.Count() == 0 )
	{
		return Continue();
	}

	// FIX 5 — stuck stage 3 asks us to give up on this goal. Drop it and pick
	// somewhere else rather than re-planning a route we cannot walk.
	if ( me->m_abandonGoalRequest )
	{
		me->m_abandonGoalRequest = false;
		m_lastStuckPos = me->GetAbsOrigin();
		m_avoidStuckRadius = 512.0f;
		m_hasGoal = false;
		m_pickGoalTimer.Invalidate();
		m_repathTimer.Invalidate();
		m_path.Invalidate();
	}

	// Pick a new random goal periodically (or if we don't have one yet).
	if ( !m_hasGoal || m_pickGoalTimer.IsElapsed() )
	{
		const Vector myPos = me->GetAbsOrigin();

		// Try a handful of random areas; pick the first acceptable one.
		CNavArea *goalArea = NULL;
		for ( int attempt = 0; attempt < 30; ++attempt )
		{
			int idx = RandomInt( 0, TheNavAreas.Count() - 1 );
			CNavArea *candidate = TheNavAreas[ idx ];
			if ( IsAcceptableWanderGoal( candidate, myPos, m_lastStuckPos, m_avoidStuckRadius ) )
			{
				goalArea = candidate;
				break;
			}
		}

		if ( goalArea )
		{
			m_goalPos = goalArea->GetCenter();
			m_hasGoal = true;
			m_pickGoalTimer.Start( RandomFloat( 8.0f, 15.0f ) );
			m_repathTimer.Invalidate();	// force a fresh path
			m_avoidStuckRadius = 0.0f;	// stuck-avoidance only applies to the next pick
		}
	}

	// Recompute path occasionally. FIX 7 — hysteresis: leave a working path
	// alone while we're actually travelling it, so volatile cost terms can't
	// flip the route between two near-equal lanes every second.
	if ( m_hasGoal && m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		if ( FFBotHelpers::ShouldRecomputePath( me, m_path, m_goalPos ) )
		{
			CFFBotPathCost cost( me, FFBOT_DEFAULT_ROUTE );
			m_path.Compute( me, m_goalPos, cost );
		}
	}

	// FIX 1 — single movement authority. CanDrivePath publishes the
	// path goal for the aim driver and refuses while the movement
	// arbiter owns locomotion, so this can never issue a second,
	// contradictory Approach() in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me );
	return Continue();
}

//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotWander::OnStuck( CFFBot *me )
{
	// Drop the current goal and remember where we got blocked so the next
	// pick avoids any area within ~512u of the stuck spot. That side-steps
	// one-way / team-locked doors when the bot keeps trying to bash through.
	m_lastStuckPos = me->GetAbsOrigin();
	m_avoidStuckRadius = 512.0f;
	m_hasGoal = false;
	m_pickGoalTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_path.Invalidate();
	return TryContinue();
}
