//========= Fortress Forever Bot =============================================//
//
// CFFBotMoveToVantagePoint — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_move_to_vantage_point.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
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
// Snapshot of alive enemy player eye-positions, used to LOS-test candidate
// vantage areas.
//-----------------------------------------------------------------------------
static void CollectEnemyEyePositions( CFFBot *me, CUtlVector< Vector > *outPositions )
{
	const int myTeam = me->GetTeamNumber();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || pp == me )
			continue;
		if ( !pp->IsAlive() )
			continue;
		const int t = pp->GetTeamNumber();
		if ( t < TEAM_BLUE || t > TEAM_GREEN )
			continue;
		if ( t == myTeam )
			continue;
		outPositions->AddToTail( pp->EyePosition() );
	}
}


//-----------------------------------------------------------------------------
// BFS for a vantage area: closest reachable area with LOS to ≥1 enemy.
//-----------------------------------------------------------------------------
class CFFFindVantagePoint : public ISearchSurroundingAreasFunctor
{
public:
	CFFFindVantagePoint( CFFBot *me, const CUtlVector< Vector > &enemyEyes, float maxRange )
		: m_me( me ), m_enemyEyes( enemyEyes ), m_maxRange( maxRange ),
		  m_vantage( NULL ), m_vantageDist( FLT_MAX )
	{
	}

	virtual bool operator() ( CNavArea *baseArea, CNavArea *priorArea, float travelDistanceSoFar ) OVERRIDE
	{
		if ( m_enemyEyes.Count() == 0 )
			return false;

		const Vector eyeFrom = baseArea->GetCenter() + Vector( 0, 0, 64.0f );
		for ( int i = 0; i < m_enemyEyes.Count(); ++i )
		{
			trace_t tr;
			UTIL_TraceLine( eyeFrom, m_enemyEyes[ i ],
				MASK_VISIBLE_AND_NPCS, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 0.99f )
			{
				// Effective distance — apply terrain bonuses so high-
				// ground vantages win over equidistant ground vantages,
				// and water vantages lose to dry ones. ~300u of travel
				// is worth giving up for elevation; ~500u to stay dry.
				const unsigned int attrs = static_cast< CFFNavArea * >( baseArea )->GetAttributesFF();
				float effectiveDist = travelDistanceSoFar;
				if ( attrs & FF_NAV_HIGH_GROUND )
					effectiveDist -= 300.0f;
				if ( attrs & ( FF_NAV_WATER | FF_NAV_UNDERWATER ) )
					effectiveDist += 500.0f;

				if ( effectiveDist < m_vantageDist )
				{
					m_vantageDist = effectiveDist;
					m_vantage = static_cast< CFFNavArea * >( baseArea );
				}
				break;
			}
		}
		return true;
	}

	virtual bool ShouldSearch( CNavArea *adjArea, CNavArea *currentArea, float travelDistanceSoFar ) OVERRIDE
	{
		if ( travelDistanceSoFar > m_maxRange )
			return false;
		return m_me->GetLocomotionInterface()->IsAreaTraversable( adjArea );
	}

	CFFBot *m_me;
	const CUtlVector< Vector > &m_enemyEyes;
	float m_maxRange;
	CFFNavArea *m_vantage;
	float m_vantageDist;
};


//-----------------------------------------------------------------------------
CFFBotMoveToVantagePoint::CFFBotMoveToVantagePoint( float maxTravelDistance )
{
	m_maxTravelDistance = maxTravelDistance;
	m_vantageArea = NULL;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMoveToVantagePoint::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	CNavArea *startArea = me->GetLastKnownArea();
	if ( !startArea )
		return Done( "No starting area" );

	CUtlVector< Vector > enemyEyes;
	CollectEnemyEyePositions( me, &enemyEyes );
	if ( enemyEyes.Count() == 0 )
		return Done( "No enemies alive" );

	CFFFindVantagePoint search( me, enemyEyes, m_maxTravelDistance );
	SearchSurroundingAreas( startArea, search );

	m_vantageArea = search.m_vantage;
	if ( !m_vantageArea )
		return Done( "No vantage point found" );

	m_path.Invalidate();
	m_repathTimer.Invalidate();
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMoveToVantagePoint::Update( CFFBot *me, float interval )
{
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat() : NULL;
	if ( threat && threat->IsVisibleInFOVNow() )
		return Done( "Enemy is now visible — vantage achieved" );

	if ( !m_path.IsValid() && m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( 1.0f );
		CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
		if ( !m_path.Compute( me, m_vantageArea->GetCenter(), cost ) )
			return Done( "No path to vantage point" );
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
EventDesiredResult< CFFBot > CFFBotMoveToVantagePoint::OnStuck( CFFBot *me )
{
	m_path.Invalidate();
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMoveToVantagePoint::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryDone( RESULT_CRITICAL, "Vantage point reached" );
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMoveToVantagePoint::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	m_path.Invalidate();
	return TryContinue();
}
