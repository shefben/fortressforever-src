//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_payload_block.cpp
// Prevent the other team from moving the cart
// Michael Booth, April 2010

#include "cbase.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "ff_gamerules.h"
#include "team_control_point_master.h"
#include "team_train_watcher.h"
#include "bot/ff_bot.h"
#include "bot/behavior/scenario/payload/ff_bot_payload_block.h"
#include "bot/behavior/medic/ff_bot_medic_heal.h"
#include "bot/behavior/engineer/ff_bot_engineer_build.h"


extern ConVar ff_bot_path_lookahead_range;

//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotPayloadBlock::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_path.Invalidate();

	m_giveUpTimer.Start( RandomFloat( 3.0f, 5.0f ) );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotPayloadBlock::Update( CFFBot *me, float interval )
{
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleRecently() )
	{
		// prepare to fight
		me->EquipBestWeaponForThreat( threat );
	}

	if ( m_giveUpTimer.IsElapsed() )
	{
		return Done( "Been blocking long enough" );
	}

	// move toward the point, periodically repathing to account for changing situation
	if ( m_repathTimer.IsElapsed() )
	{
		VPROF_BUDGET( "CFFBotPayloadBlock::Update( repath )", "NextBot" );
		
		CTeamTrainWatcher *trainWatcher = TFGameRules()->GetPayloadToBlock( me->GetTeamNumber() );
		if ( !trainWatcher )
		{
			return Done( "Train Watcher is missing" );
		}

		CBaseEntity *cart = trainWatcher->GetTrainEntity();
		if ( !cart )
		{
			return Done( "Cart is missing" );
		}

		CFFBotPathCost cost( me, DEFAULT_ROUTE );
		m_path.Compute( me, cart->WorldSpaceCenter(), cost );
		m_repathTimer.Start( RandomFloat( 0.2f, 0.4f ) );
	}

	// move towards next capture point
	m_path.Update( me );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotPayloadBlock::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	VPROF_BUDGET( "CFFBotPayloadBlock::OnResume", "NextBot" );

	m_repathTimer.Invalidate();

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotPayloadBlock::OnStuck( CFFBot *me )
{
	VPROF_BUDGET( "CFFBotPayloadBlock::OnStuck", "NextBot" );

	m_repathTimer.Invalidate();
	me->GetLocomotionInterface()->ClearStuckStatus();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotPayloadBlock::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotPayloadBlock::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	VPROF_BUDGET( "CFFBotPayloadBlock::OnMoveToFailure", "NextBot" );

	m_repathTimer.Invalidate();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotPayloadBlock::OnTerritoryContested( CFFBot *me, int territoryID )
{
	return TryToSustain( RESULT_IMPORTANT );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotPayloadBlock::OnTerritoryCaptured( CFFBot *me, int territoryID )
{
	return TryToSustain( RESULT_IMPORTANT );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotPayloadBlock::OnTerritoryLost( CFFBot *me, int territoryID )
{
	return TryToSustain( RESULT_IMPORTANT );
}


//---------------------------------------------------------------------------------------------
QueryResultType	CFFBotPayloadBlock::ShouldRetreat( const INextBot *bot ) const
{
	return ANSWER_UNDEFINED;
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotPayloadBlock::ShouldHurry( const INextBot *bot ) const
{
	// hurry and block the cart - don't retreat, etc
	return ANSWER_YES;
}

