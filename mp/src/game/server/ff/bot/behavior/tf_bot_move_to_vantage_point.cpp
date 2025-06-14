//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_move_to_vantage_point.h
// Move to a position where at least one enemy is visible
// Michael Booth, November 2009

#include "cbase.h"
#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/ff_bot_move_to_vantage_point.h"

#include "nav_mesh.h"

extern ConVar ff_bot_path_lookahead_range;


//---------------------------------------------------------------------------------------------
CFFBotMoveToVantagePoint::CFFBotMoveToVantagePoint( float maxTravelDistance )
{
	m_maxTravelDistance = maxTravelDistance;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMoveToVantagePoint::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	m_vantageArea = me->FindVantagePoint( m_maxTravelDistance );
	if ( !m_vantageArea )
	{
		return Done( "No vantage point found" );
	}

	m_path.Invalidate();
	m_repathTimer.Invalidate();

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMoveToVantagePoint::Update( CFFBot *me, float interval )
{
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleInFOVNow() )
	{
		return Done( "Enemy is visible" );
	}

	if ( !m_path.IsValid() && m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( 1.0f );

		CFFBotPathCost cost( me, FASTEST_ROUTE );
		if ( !m_path.Compute( me, m_vantageArea->GetCenter(), cost ) )
		{
			return Done( "No path to vantage point exists" );
		}
	}

	// move along path to vantage point
	m_path.Update( me );

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMoveToVantagePoint::OnStuck( CFFBot *me )
{
	m_path.Invalidate();
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMoveToVantagePoint::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryDone( RESULT_CRITICAL, "Vantage point reached" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMoveToVantagePoint::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	m_path.Invalidate();
	return TryContinue();
}


