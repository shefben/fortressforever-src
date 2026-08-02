//========= Fortress Forever Bot =============================================//
//
// CFFBotSeekAndDestroy — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_seek_and_destroy.h"
#include "ff_bot_helpers.h"
#include "ff_bot_attack.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "nav_mesh.h"
#include "NextBotKnownEntity.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
CFFBotSeekAndDestroy::CFFBotSeekAndDestroy( float duration )
{
	m_goalArea = NULL;
	if ( duration > 0.0f )
		m_giveUpTimer.Start( duration );
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSeekAndDestroy::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	RecomputeSeekPath( me );

	if ( m_giveUpTimer.HasStarted() )
		m_giveUpTimer.Reset();

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSeekAndDestroy::Update( CFFBot *me, float interval )
{
	if ( m_giveUpTimer.HasStarted() && m_giveUpTimer.IsElapsed() )
		return Done( "Behavior duration elapsed" );

	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat() : NULL;

	if ( threat && threat->GetEntity() )
	{
		const float engageRange = 1000.0f;
		if ( me->IsRangeLessThan( threat->GetLastKnownPosition(), engageRange ) )
		{
			return SuspendFor( new CFFBotAttack, "Going after an enemy" );
		}
	}

	// FIX 1 — single movement authority. CanDrivePath publishes the
	// path goal for the aim driver and refuses while the movement
	// arbiter owns locomotion, so this can never issue a second,
	// contradictory Approach() in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me );

	if ( !m_path.IsValid() && m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( 1.0f );
		RecomputeSeekPath( me );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSeekAndDestroy::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	RecomputeSeekPath( me );
	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnStuck( CFFBot *me )
{
	RecomputeSeekPath( me );
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	RecomputeSeekPath( me );
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	RecomputeSeekPath( me );
	return TryContinue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSeekAndDestroy::ShouldRetreat( const INextBot *meBot ) const
{
	CFFBot *me = const_cast< CFFBot * >( static_cast< const CFFBot * >( meBot->GetEntity() ) );
	if ( me && me->GetClassSlot() == CLASS_PYRO )
		return ANSWER_NO;
	return ANSWER_UNDEFINED;
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSeekAndDestroy::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_UNDEFINED;
}


//-----------------------------------------------------------------------------
CFFNavArea *CFFBotSeekAndDestroy::ChooseGoalArea( CFFBot *me )
{
	CFFNavMesh *ffMesh = TheFFNavMesh();
	if ( !ffMesh )
		return NULL;

	CUtlVector< CFFNavArea * > goals;

	// Enemy spawn-room thresholds — areas right outside enemy spawns,
	// classic seek hot-spots.
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == me->GetTeamNumber() )
			continue;
		ffMesh->CollectSpawnRoomThresholdAreas( t, &goals );
	}

	// Enemy flag rest areas + capture points — these are the strategic
	// hubs the action wants to put us near.
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == me->GetTeamNumber() )
			continue;
		const CUtlVector< CFFNavArea * > *flags = ffMesh->GetFlagAreas( t );
		if ( flags )
		{
			for ( int i = 0; i < flags->Count(); ++i )
				goals.AddToTail( ( *flags )[ i ] );
		}
		const CUtlVector< CFFNavArea * > *caps = ffMesh->GetCapAreas( t );
		if ( caps )
		{
			for ( int i = 0; i < caps->Count(); ++i )
				goals.AddToTail( ( *caps )[ i ] );
		}
	}

	if ( goals.Count() == 0 )
		return NULL;

	return goals[ RandomInt( 0, goals.Count() - 1 ) ];
}


//-----------------------------------------------------------------------------
void CFFBotSeekAndDestroy::RecomputeSeekPath( CFFBot *me )
{
	m_goalArea = ChooseGoalArea( me );
	if ( m_goalArea )
	{
		CFFBotPathCost cost( me, FFBOT_DEFAULT_ROUTE );
		m_path.Compute( me, m_goalArea->GetCenter(), cost );
	}
	else
	{
		m_path.Invalidate();
	}
}
