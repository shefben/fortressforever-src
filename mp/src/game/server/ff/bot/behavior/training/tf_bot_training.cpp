//========= Copyright Valve Corporation, All rights reserved. ============//
////////////////////////////////////////////////////////////////////////////////////////////////////
// ff_bot_training.cpp
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "cbase.h"
#include "team.h"
#include "bot/ff_bot.h"
#include "bot/map_entities/ff_bot_generator.h"
#include "bot/behavior/training/ff_bot_training.h"
#include "ff_obj_sentrygun.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

ActionResult< CFFBot > CTFDespawn::Update( CFFBot *me, float interval )
{
	// players need to be kicked, not deleted
	if ( me->GetEntity()->IsPlayer() )
	{
		CBasePlayer *player = dynamic_cast< CBasePlayer * >( me->GetEntity() );
		engine->ServerCommand( UTIL_VarArgs( "kickid %d\n", player->GetUserID() ) );
	}
	else
	{
		UTIL_Remove( me->GetEntity() );
	}
	return Continue();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ActionResult< CFFBot > CTFTrainingAttackSentryActionPoint::Update( CFFBot *me, float interval )
{
	CFFBotActionPoint* pActionPoint = me->GetActionPoint();
	if ( pActionPoint == NULL )
	{
		return Done();
	}

	if ( pActionPoint->IsWithinRange( me ) )
	{
		CObjectSentrygun *pSentrygun = me->GetEnemySentry();
		if ( pSentrygun )
		{
			me->GetBodyInterface()->AimHeadTowards( pSentrygun, IBody::MANDATORY, 1.0f, NULL, "Aiming at enemy sentry" );

			// because sentries are stationary, check if XY is on target to allow SelectTargetPoint() to adjust Z for grenades
			Vector toSentry = pSentrygun->WorldSpaceCenter() - me->EyePosition();
			toSentry.NormalizeInPlace();
			Vector forward;
			me->EyeVectors( &forward );
			
			if ( ( forward.x * toSentry.x + forward.y * toSentry.y ) > 0.95f )
			{			
				me->PressFireButton();
			}
		}
	}
	else
	{
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			
			CFFBotPathCost cost( me, FASTEST_ROUTE );
			m_path.Compute( me, pActionPoint->GetAbsOrigin(), cost );
		}
		
		m_path.Update( me );
	}

	return Continue();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ActionResult< CFFBot > CTFGotoActionPoint::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_stayTimer.Invalidate();
	m_wasTeleported = false;

	return Continue();
}

ActionResult< CFFBot > CTFGotoActionPoint::Update( CFFBot *me, float interval )
{
	CFFBotActionPoint* pActionPoint = me->GetActionPoint();
	if ( pActionPoint == NULL )
	{
		return Done();
	}

	if ( pActionPoint->IsWithinRange( me ) )
	{
		// track if we ever get teleported during this process
		m_wasTeleported |= me->m_Shared.InCond( TF_COND_SELECTED_TO_TELEPORT );

		// we're at the action point
		if ( m_stayTimer.HasStarted() == false )
		{
			// this method may cause us to become suspended for other actions
			pActionPoint->ReachedActionPoint( me );

			m_stayTimer.Start( pActionPoint->m_stayTime );
		}
		else if ( m_stayTimer.IsElapsed() )
		{
			me->SetActionPoint( dynamic_cast< CFFBotActionPoint * >( pActionPoint->m_moveGoal.Get() ) );
			return ChangeTo( new CTFGotoActionPoint, "Reached point, going to next" );
		}		
	}
	else if ( m_wasTeleported )
	{
		// we reached our action point, but were teleported far away.
		// presumably we've resumed, so just go to the next action point.
		me->SetActionPoint( dynamic_cast< CFFBotActionPoint * >( pActionPoint->m_moveGoal.Get() ) );
		return ChangeTo( new CTFGotoActionPoint, "Reached point, going to next" );
	}
	else
	{
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			
			CFFBotPathCost cost( me, FASTEST_ROUTE );
			m_path.Compute( me, pActionPoint->GetAbsOrigin(), cost );
		}
		
		m_path.Update( me );
	}
	return Continue();
}
