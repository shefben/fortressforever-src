//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_raid_companion.cpp
// Teammate bots for Raid mode
// Michael Booth, October 2009

#include "cbase.h"

#ifdef TF_RAID_MODE

#include "team.h"
#include "bot/ff_bot.h"
#include "team_control_point_master.h"
#include "bot/behavior/medic/ff_bot_medic_heal.h"
#include "bot/behavior/scenario/raid/ff_bot_companion.h"
#include "bot/behavior/ff_bot_attack.h"
#include "bot/behavior/ff_bot_move_to_vantage_point.h"
#include "bot/behavior/engineer/ff_bot_engineer_build.h"
#include "bot/behavior/sniper/ff_bot_sniper_lurk.h"

#include "bot/map_entities/ff_bot_generator.h"		// action point

ConVar ff_raid_companion_follow_range( "ff_raid_companion_follow_range", "150", FCVAR_CHEAT );
ConVar ff_raid_companion_allow_bot_leader( "ff_raid_companion_allow_bot_leader", "0", FCVAR_CHEAT );

extern ConVar ff_bot_path_lookahead_range;


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotCompanion::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
CFFPlayer *CFFBotCompanion::GetLeader( void )
{
	CTeam *raidingTeam = GetGlobalTeam( FF_TEAM_BLUE );
	CFFPlayer *leader = NULL;
	float leaderSpeed = FLT_MAX;

	for( int i=0; i<raidingTeam->GetNumPlayers(); ++i )
	{
		CFFPlayer *player = (CFFPlayer *)raidingTeam->GetPlayer(i);

		if ( player->IsBot() && !ff_raid_companion_allow_bot_leader.GetBool() )
			continue;

/*
		if ( player->IsPlayerClass( CLASS_ENGINEER ) ||
			 player->IsPlayerClass( CLASS_SNIPER ) ||
			 player->IsPlayerClass( CLASS_MEDIC ) )
			continue;
*/

		if ( player->IsAlive() )
		{
			float speed = player->GetPlayerClass()->GetMaxSpeed();

			if ( speed < leaderSpeed )
			{
				leader = player;
				leaderSpeed = speed;
			}
		}
	}

	return leader;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotCompanion::Update( CFFBot *me, float interval )
{
	if ( me->IsPlayerClass( CLASS_MEDIC ) )
	{
		const CKnownEntity *patient = me->GetVisionInterface()->GetClosestKnown( me->GetTeamNumber() );
		if ( patient )
		{
			return SuspendFor( new CFFBotMedicHeal );
		}
	}

	CFFPlayer *leader = GetLeader();
	if ( !leader )
		return Continue();

	CFFBotPathCost cost( me, FASTEST_ROUTE );
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();

	if ( me->IsSelf( leader ) )
	{
		const float engageRange = 500.0f;
		if ( threat && threat->IsVisibleRecently() && me->IsRangeLessThan( threat->GetEntity(), engageRange ) )
		{
			// stop pushing ahead and kill nearby threats
			return SuspendFor( new CFFBotAttack, "Attacking nearby threats" );
		}

		// head toward next capture point
		CTeamControlPoint *point = me->GetMyControlPoint();
		if ( point )
		{
			m_path.Update( me, point, cost );
		}
	}
	else
	{
		if ( ( !threat || threat->GetTimeSinceLastSeen() > 3.0f ) && leader->GetTimeSinceLastInjury() < 1.0f )
		{
			// we don't see anything, but the leader is under attack - find a better vantage point
			const float nearRange = 1000.0f;
			return SuspendFor( new CFFBotMoveToVantagePoint( nearRange ), "Moving to where I can see the enemy" );
		}

		if ( leader && me->IsDistanceBetweenGreaterThan( leader, ff_raid_companion_follow_range.GetFloat() ) )
		{
			m_path.Update( me, leader, cost );
		}
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotCompanion::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	m_path.Invalidate();
	return Continue();
}



//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotGuardian::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotGuardian::Update( CFFBot *me, float interval )
{
	if ( me->GetActionPoint() )
	{
		const float atHomeRange = 35.0f; // 25.0f;
		const Vector &home = me->GetActionPoint()->GetAbsOrigin();

		if ( me->IsRangeGreaterThan( home, atHomeRange ) )
		{
			if ( m_repathTimer.IsElapsed() && !m_path.IsValid() )
			{
				m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );

				CFFBotPathCost cost( me, FASTEST_ROUTE );
				m_path.Compute( me, home, cost );
			}

			// move home
			m_path.Update( me );

			return Continue();
		}
	}

	// at home
	m_path.Invalidate();
	me->SetHomeArea( me->GetLastKnownArea() );

	if ( me->IsPlayerClass( CLASS_ENGINEER ) )
	{
		return SuspendFor( new CFFBotEngineerBuild );
	}

	if ( me->IsPlayerClass( CLASS_SNIPER ) )
	{
		return SuspendFor( new CFFBotSniperLurk );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotGuardian::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	m_path.Invalidate();
	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGuardian::OnStuck( CFFBot *me )
{
	m_path.Invalidate();
	return TryContinue( RESULT_IMPORTANT );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGuardian::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	m_path.Invalidate();
	return TryContinue( RESULT_IMPORTANT );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGuardian::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	m_path.Invalidate();
	return TryContinue( RESULT_IMPORTANT );
}

#endif // TF_RAID_MODE
