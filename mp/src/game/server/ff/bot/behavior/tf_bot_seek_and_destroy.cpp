//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_seek_and_destroy.h
// Roam the environment, attacking victims
// Michael Booth, January 2010

#include "cbase.h"
#include "ff_player.h"
#include "ff_gamerules.h"
#include "team_control_point_master.h"
#include "bot/ff_bot.h"
#include "bot/behavior/ff_bot_attack.h"
#include "bot/behavior/ff_bot_seek_and_destroy.h"
#include "bot/behavior/sniper/ff_bot_sniper_attack.h"
#include "nav_mesh.h"

extern ConVar ff_bot_path_lookahead_range;
extern ConVar ff_bot_offense_must_push_time;
extern ConVar ff_bot_defense_must_defend_time;

ConVar ff_bot_debug_seek_and_destroy( "ff_bot_debug_seek_and_destroy", "0", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
CFFBotSeekAndDestroy::CFFBotSeekAndDestroy( float duration )
{
	if ( duration > 0.0f )
	{
		m_giveUpTimer.Start( duration );
	}
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSeekAndDestroy::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	RecomputeSeekPath( me );

	CTeamControlPoint *point = me->GetMyControlPoint();
	m_isPointLocked = ( point && point->IsLocked() );

	// restart the timer if we have one
	if ( m_giveUpTimer.HasStarted() )
	{
		m_giveUpTimer.Reset();
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSeekAndDestroy::Update( CFFBot *me, float interval )
{
	if ( m_giveUpTimer.HasStarted() && m_giveUpTimer.IsElapsed() )
	{
		return Done( "Behavior duration elapsed" );
	}

	if ( TFGameRules()->IsInTraining() )
	{
		// if the trainee has started capturing the point, assist them
		if ( me->IsAnyPointBeingCaptured() )
		{
			return Done( "Assist trainee in capturing the point" );
		}
	}
	else
	{
		if ( me->IsCapturingPoint() )
		{
			return Done( "Keep capturing point I happened to stumble upon" );
		}

		if ( m_isPointLocked )
		{
			CTeamControlPoint *point = me->GetMyControlPoint();

			if ( point && !point->IsLocked() )
			{
				return Done( "The point just unlocked" );
			}
		}
		
		if ( !TFGameRules()->RoundHasBeenWon() && me->GetTimeLeftToCapture() < ff_bot_offense_must_push_time.GetFloat() )
		{
			return Done( "Time to push for the objective" );
		}
	}

	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat )
	{
		if ( TFGameRules()->RoundHasBeenWon() )
		{
			// hunt down the losers
			return SuspendFor( new CFFBotAttack, "Chasing down the losers" );
		}

		const float engageRange = 1000.0f;
		if ( me->IsRangeLessThan( threat->GetLastKnownPosition(), engageRange ) )
		{
			return SuspendFor( new CFFBotAttack, "Going after an enemy" );
		}
	}

	// move towards our seek goal
	m_path.Update( me );

	if ( !m_path.IsValid() && m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( 1.0f );

		RecomputeSeekPath( me );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSeekAndDestroy::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	RecomputeSeekPath( me );

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnStuck( CFFBot *me )
{
	RecomputeSeekPath( me );

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	RecomputeSeekPath( me );

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	RecomputeSeekPath( me );

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
QueryResultType	CFFBotSeekAndDestroy::ShouldRetreat( const INextBot *meBot ) const
{
	CFFBot *me = (CFFBot *)meBot->GetEntity();

	if ( me->IsPlayerClass( CLASS_PYRO ) )
	{
		return ANSWER_NO;
	}

	return ANSWER_UNDEFINED;
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotSeekAndDestroy::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_UNDEFINED;
}


//---------------------------------------------------------------------------------------------
CTFNavArea *CFFBotSeekAndDestroy::ChooseGoalArea( CFFBot *me )
{
	CUtlVector< CTFNavArea * > goalVector;

	TheTFNavMesh()->CollectSpawnRoomThresholdAreas( &goalVector, GetEnemyTeam( me->GetTeamNumber() ) );

	CTeamControlPoint *point = me->GetMyControlPoint();
	if ( point && !point->IsLocked() )
	{
		// add current control point as a seek goal
		const CUtlVector< CTFNavArea * > *controlPointAreas = TheTFNavMesh()->GetControlPointAreas( point->GetPointIndex() );
		if ( controlPointAreas && controlPointAreas->Count() > 0 )
		{
			goalVector.AddToTail( controlPointAreas->Element( RandomInt( 0, controlPointAreas->Count()-1 ) ) );
		}
	}

	if ( ff_bot_debug_seek_and_destroy.GetBool() )
	{
		for( int i=0; i<goalVector.Count(); ++i )
		{
			TheNavMesh->AddToSelectedSet( goalVector[i] );
		}
	}

	// pick a new goal
	if ( goalVector.Count() > 0 )
	{
		return goalVector[ RandomInt( 0, goalVector.Count()-1 ) ];
	}

	return NULL;
}


//---------------------------------------------------------------------------------------------
void CFFBotSeekAndDestroy::RecomputeSeekPath( CFFBot *me )
{
	m_goalArea = ChooseGoalArea( me );
	if ( m_goalArea )
	{
		CFFBotPathCost cost( me, SAFEST_ROUTE );
		m_path.Compute( me, m_goalArea->GetCenter(), cost );
	}
	else
	{
		m_path.Invalidate();
	}
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnTerritoryContested( CFFBot *me, int territoryID )
{
	return TryDone( RESULT_IMPORTANT, "Defending the point" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnTerritoryCaptured( CFFBot *me, int territoryID )
{
	return TryDone( RESULT_IMPORTANT, "Giving up due to point capture" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSeekAndDestroy::OnTerritoryLost( CFFBot *me, int territoryID )
{
	return TryDone( RESULT_IMPORTANT, "Giving up due to point lost" );
}

