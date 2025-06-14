//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_escort_flag_carrier.cpp
// Escort the flag carrier to their destination
// Michael Booth, May 2011

#include "cbase.h"

#include "bot/ff_bot.h"
#include "bot/behavior/scenario/capture_the_flag/ff_bot_escort_flag_carrier.h"
#include "bot/behavior/scenario/capture_the_flag/ff_bot_attack_flag_defenders.h"
#include "bot/behavior/scenario/capture_the_flag/ff_bot_deliver_flag.h"

extern ConVar ff_bot_flag_escort_range;

ConVar ff_bot_flag_escort_give_up_range( "ff_bot_flag_escort_give_up_range", "1000", FCVAR_CHEAT );
ConVar ff_bot_flag_escort_max_count( "ff_bot_flag_escort_max_count", "4", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
// 
// Count the number of TFBots currently engaged in the "EscortFlagCarrier" behavior
//
int GetBotEscortCount( int team )
{
	int count = 0;

	CUtlVector< CTFPlayer * > livePlayerVector;
	CollectPlayers( &livePlayerVector, team, COLLECT_ONLY_LIVING_PLAYERS );

	int i;
	for( i=0; i<livePlayerVector.Count(); ++i )
	{
		CFFBot *bot = dynamic_cast< CFFBot * >( livePlayerVector[i] );
		if ( bot )
		{
			Behavior< CFFBot > *behavior = (Behavior< CFFBot > *)bot->GetIntentionInterface()->FirstContainedResponder();
			if ( behavior )
			{
				Action< CFFBot > *action = (Action< CFFBot > *)behavior->FirstContainedResponder();

				while( action && action->GetActiveChildAction() )
				{
					action = action->GetActiveChildAction();
				}

				if ( action && action->IsNamed( "EscortFlagCarrier" ) )
				{
					++count;
				}
			}
		}
	}

	return count;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotEscortFlagCarrier::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEscortFlagCarrier::Update( CFFBot *me, float interval )
{
	CCaptureFlag *flag = me->GetFlagToFetch();

	if ( !flag )
	{
		return Done( "No flag" );
	}

	CTFPlayer *carrier = ToTFPlayer( flag->GetOwnerEntity() );
	if ( !carrier )
	{
		return Done( "Flag was dropped" );
	}
	else if ( me->IsSelf( carrier ) )
	{
		return Done( "I picked up the flag!" );
	}

	// stay near the carrier
	if ( me->IsRangeGreaterThan( carrier, ff_bot_flag_escort_give_up_range.GetFloat() ) )
	{
		if ( me->SelectRandomReachableEnemy() )
		{
			// too far away - give up
			return ChangeTo( new CFFBotAttackFlagDefenders, "Too far from flag carrier - attack defenders!" );
		}
	}

	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleRecently() )
	{
		// prepare to fight
		me->EquipBestWeaponForThreat( threat );
	}

	CTFWeaponBase *myWeapon = me->m_Shared.GetActiveTFWeapon();
	if ( myWeapon && myWeapon->IsMeleeWeapon() )
	{
		if ( me->IsRangeLessThan( carrier, ff_bot_flag_escort_range.GetFloat() ) && me->IsLineOfSightClear( carrier ) )
		{
			ActionResult< CFFBot > result = m_meleeAttackAction.Update( me, interval );

			if ( result.IsContinue() )
			{
				// we have a melee target, and we're still reasonably close to the flag carrier
				return Continue();
			}
		}
	}

	if ( me->IsRangeGreaterThan( carrier, 0.5f * ff_bot_flag_escort_range.GetFloat() ) )
	{
		// move near carrier
		if ( m_repathTimer.IsElapsed() )
		{
			if ( GetBotEscortCount( me->GetTeamNumber() ) > ff_bot_flag_escort_max_count.GetInt() )
			{
				if ( me->SelectRandomReachableEnemy() )
				{
					return Done( "Too many flag escorts - giving up" );
				}
			}

			CFFBotPathCost cost( me, FASTEST_ROUTE );
			m_path.Compute( me, carrier, cost );

			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		}

		m_path.Update( me );
	}

	return Continue();
}
