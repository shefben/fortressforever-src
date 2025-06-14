//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_guard_area.cpp
// Defend an area against intruders
// Michael Booth, October 2009

#include "cbase.h"

#ifdef TF_RAID_MODE

#include "ff_player.h"
#include "bot/ff_bot.h"
#include "team_control_point_master.h"
#include "econ_entity_creation.h"
#include "bot/behavior/ff_bot_retreat_to_cover.h"
#include "bot/behavior/sniper/ff_bot_sniper_attack.h"
#include "bot/behavior/engineer/ff_bot_engineer_build.h"
#include "bot/behavior/medic/ff_bot_medic_heal.h"
#include "bot/behavior/scenario/raid/ff_bot_wander.h"
#include "bot/behavior/scenario/raid/ff_bot_guard_area.h"
#include "bot/behavior/ff_bot_attack.h"
#include "bot/behavior/demoman/ff_bot_prepare_stickybomb_trap.h"

#include "nav_mesh.h"

extern ConVar ff_bot_path_lookahead_range;
ConVar ff_bot_guard_aggro_range( "ff_bot_guard_aggro_range", "750", FCVAR_CHEAT );
//ConVar ff_bot_guard_give_up_range( "ff_bot_guard_give_up_range", "1250", FCVAR_CHEAT );

ConVar ff_raid_special_vocalize_min_interval( "ff_raid_special_vocalize_min_interval", "10", FCVAR_CHEAT );
ConVar ff_raid_special_vocalize_max_interval( "ff_raid_special_vocalize_max_interval", "15", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotGuardArea::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_chasePath.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

/*
	// give this guy a hat!
	randomitemcriteria_t criteria;
	criteria.iItemLevel = AE_USE_SCRIPT_VALUE;
	criteria.iItemQuality = AE_USE_SCRIPT_VALUE;
	criteria.vecAbsOrigin = me->GetAbsOrigin();
	criteria.vecAbsAngles = vec3_angle;

	switch( me->GetPlayerClass()->GetClassIndex() )
	{
	case CLASS_SCOUT:		criteria.pszItemName = "Scout Hat 1";		break;
	case CLASS_SNIPER:		criteria.pszItemName = "Sniper Hat 1";		break;
	case CLASS_SOLDIER:		criteria.pszItemName = "Soldier Pot Hat";	break;
	case CLASS_DEMOMAN:		criteria.pszItemName = "Demo Top Hat";		break;
	case CLASS_MEDIC:		criteria.pszItemName = "Medic Hat 1";		break;
	case CLASS_HEAVYWEAPONS:	criteria.pszItemName = "Heavy Ushanka Hat";	break;
	case CLASS_PYRO:			criteria.pszItemName = "Pyro Chicken Hat";	break;
	case CLASS_SPY:			criteria.pszItemName = "Spy Derby Hat";		break;
	case CLASS_ENGINEER:		criteria.pszItemName = "Engineer Hat 1";	break;
	default:					criteria.pszItemName = "";					break;
	}

	CBaseEntity *hat = ItemGeneration()->GenerateRandomItem( &criteria );
	if ( hat )
	{
		// Fake global id
		static int s_nFakeID = 1;
		static_cast< CEconEntity * >( hat )->GetAttributeContainer()->GetItem()->SetItemID( s_nFakeID++ );

		DispatchSpawn( hat );
		static_cast< CEconEntity * >( hat )->GetAttributeContainer()->GetItem()->GenerateAttributes();
		static_cast< CEconEntity * >( hat )->GiveTo( me );
	}
	else
	{
		Msg( "Failed to create hat\n" );
	}
*/

	return Continue();
}


//---------------------------------------------------------------------------------------------
class CFindVantagePoint : public ISearchSurroundingAreasFunctor
{
public:
	CFindVantagePoint( void )
	{
		m_vantageArea = NULL;
	}

	virtual bool operator() ( CNavArea *baseArea, CNavArea *priorArea, float travelDistanceSoFar )
	{
		if ( travelDistanceSoFar > 2000.0f )
			return false;

		CTFNavArea *area = (CTFNavArea *)baseArea;

		CTeam *raidingTeam = GetGlobalTeam( FF_TEAM_BLUE );
		for( int i=0; i<raidingTeam->GetNumPlayers(); ++i )
		{
			CTFPlayer *player = (CTFPlayer *)raidingTeam->GetPlayer(i);

			if ( !player->IsAlive() || !player->GetLastKnownArea() )
				continue;

			CTFNavArea *playerArea = (CTFNavArea *)player->GetLastKnownArea();
			if ( playerArea->IsCompletelyVisible( area ) )
			{
				// nearby area from which we can see the enemy team
				m_vantageArea = area;
				return false;
			}
		}

		return true;
	}

	CTFNavArea *m_vantageArea;
};


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotGuardArea::Update( CFFBot *me, float interval )
{
	// emit vocalizations to warn players we're in the area
	if ( m_vocalizeTimer.IsElapsed() )
	{
		m_vocalizeTimer.Start( RandomFloat( ff_raid_special_vocalize_min_interval.GetFloat(), ff_raid_special_vocalize_max_interval.GetFloat() ) );
		me->SpeakConceptIfAllowed( MP_CONCEPT_PLAYER_JEERS );
	}

	if ( me->IsPlayerClass( CLASS_MEDIC ) )
	{
		return SuspendFor( new CFFBotMedicHeal );
	}

	if ( me->IsPlayerClass( CLASS_ENGINEER ) )
	{
		return SuspendFor( new CFFBotEngineerBuild );
	}

	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleRecently() )
	{
		m_pathToVantageArea.Invalidate();

		CTFNavArea *myArea = (CTFNavArea *)me->GetLastKnownArea();
		CTFNavArea *threatArea = (CTFNavArea *)threat->GetLastKnownArea();
		if ( myArea && threatArea )
		{
			if ( threatArea->GetIncursionDistance( FF_TEAM_BLUE ) < myArea->GetIncursionDistance( FF_TEAM_BLUE ) )
			{
				if ( me->IsRangeGreaterThan( threat->GetLastKnownPosition(), ff_bot_guard_aggro_range.GetFloat() ) )
				{
					// threat is far off and hasn't reached us yet - hide until they are closer
					return SuspendFor( new CFFBotRetreatToCover, "Hiding until threat gets closer" );
				}
			}
		}

		// attack!
		return SuspendFor( new CFFBotAttack, "Attacking nearby threat" );
	}
	else
	{
		// no enemy is visible
		Vector moveTo = me->GetAbsOrigin();

		// if point is being captured, move to it
		CTeamControlPoint *point = me->GetMyControlPoint();
		if ( point && point->LastContestedAt() > 0.0f && ( gpGlobals->curtime - point->LastContestedAt() ) < 5.0f )
		{
			// the point is, or was very recently, contested - defend it!
			moveTo = point->GetAbsOrigin();
		}
		else if ( me->GetHomeArea() )
		{
			// no enemy is visible - return to our home position
			moveTo = me->GetHomeArea()->GetCenter();
		}

		if ( !m_pathToPoint.IsValid() || m_repathTimer.IsElapsed() )
		{
			CFFBotPathCost cost( me, FASTEST_ROUTE );
			m_pathToPoint.Compute( me, moveTo, cost );
			m_repathTimer.Start( RandomFloat( 2.0f, 3.0f ) );
		}

		if ( ( me->GetAbsOrigin() - moveTo ).IsLengthGreaterThan( 25.0f ) )
		{
			m_pathToPoint.Update( me );
		}

		if ( me->GetHomeArea() == me->GetLastKnownArea() )
		{
			// at home
			if ( CFFBotPrepareStickybombTrap::IsPossible( me ) )
			{
				return SuspendFor( new CFFBotPrepareStickybombTrap, "Laying sticky bombs!" );
			}
		}

/*
		// no enemy is visible - move to where we can see them
		if ( !m_pathToVantageArea.IsValid() )
		{
			CTFNavArea *vantageArea = me->FindVantagePoint();
			if ( vantageArea )
			{
				CFFBotPathCost cost( me, FASTEST_ROUTE );
				m_pathToVantageArea.Compute( me, vantageArea->GetCenter(), cost );
			}
		}

		m_pathToVantageArea.Update( me );
*/
	}


	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGuardArea::OnStuck( CFFBot *me )
{
	m_chasePath.Invalidate();
	m_pathToPoint.Invalidate();
	m_pathToVantageArea.Invalidate();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGuardArea::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGuardArea::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
QueryResultType	CFFBotGuardArea::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGuardArea::OnCommandApproach( CFFBot *me, const Vector &pos, float range )
{
	return TryContinue();
}

#endif // TF_RAID_MODE
