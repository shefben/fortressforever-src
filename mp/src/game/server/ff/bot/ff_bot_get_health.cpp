//========= Fortress Forever Bot =============================================//
//
// CFFBotGetHealth — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_get_health.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_buildableobject.h"
#include "ff_buildable_dispenser.h"
#include "ff_info_script.h"
#include "ff_player.h"
#include "entitylist.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "NextBotKnownEntity.h"
#include "NextBotInterface.h"
#include "NextBotLocomotionInterface.h"
#include "NextBotVisionInterface.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_health_search_range( "ff_bot_health_search_range", "2000",
	FCVAR_CHEAT, "Travel distance bots will search to find health." );


//-----------------------------------------------------------------------------
static CFFBot *s_possibleBot = NULL;
static CHandle< CBaseEntity > s_possibleHealth;
static int s_possibleFrame = 0;


//-----------------------------------------------------------------------------
static bool IsValidHealthSource( CFFBot *me, CBaseEntity *candidate, CFFNavArea **outArea )
{
	*outArea = NULL;
	if ( !candidate || !TheNavMesh )
		return false;

	CNavArea *base = TheNavMesh->GetNearestNavArea( candidate->WorldSpaceCenter() );
	if ( !base )
		return false;
	*outArea = static_cast< CFFNavArea * >( base );

	if ( candidate->ClassMatches( "info_ff_script" ) )
	{
		CFFInfoScript *script = static_cast< CFFInfoScript * >( candidate );
		const int goalType = script->GetBotGoalType();
		if ( goalType == Omnibot::kBackPack_Health )
		{
			return !script->IsRemoved() && !candidate->IsEffectActive( EF_NODRAW );
		}
		return false;
	}

	if ( candidate->ClassMatches( "ff_item_healthdrop" ) )
	{
		return !candidate->IsEffectActive( EF_NODRAW );
	}

	// Dropped backpacks restore health too.
	if ( candidate->ClassMatches( "ff_item_backpack" ) )
	{
		return !candidate->IsEffectActive( EF_NODRAW );
	}

	if ( candidate->ClassMatches( "FF_Dispenser" ) )
	{
		CFFDispenser *disp = static_cast< CFFDispenser * >( candidate );
		if ( !disp->IsBuilt() )
			return false;
		if ( disp->GetTeamNumber() != me->GetTeamNumber() )
			return false;
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
CFFBotGetHealth::CFFBotGetHealth( void )
{
	m_path.Invalidate();
	m_health = NULL;
	m_isGoalDispenser = false;
}


//-----------------------------------------------------------------------------
bool CFFBotGetHealth::IsPossible( CFFBot *me )
{
	if ( !me )
		return false;

	CNavArea *startArea = me->GetLastKnownArea();
	if ( !startArea )
		return false;

	ILocomotion *loco = me->GetLocomotionInterface();
	const float stepHeight = loco ? loco->GetStepHeight() : 18.0f;
	const float dropHeight = loco ? loco->GetDeathDropHeight() : 200.0f;

	// More urgency when low HP — search farther when nearly dead.
	const float hpMax = (float)me->GetMaxHealth();
	const float hpRatio = ( hpMax > 0.0f ) ? ( me->GetHealth() / hpMax ) : 1.0f;
	float searchScale = clamp( ( hpRatio - 0.3f ) / ( 0.8f - 0.3f ), 0.0f, 1.0f );
	const float searchRange = 2000.0f + searchScale * ( 1000.0f - 2000.0f );

	const float effectiveRange = MIN( searchRange, ff_bot_health_search_range.GetFloat() );

	CUtlVector< CNavArea * > nearby;
	CollectSurroundingAreas( &nearby, startArea, effectiveRange, stepHeight, dropHeight );

	CBaseEntity *closest = NULL;
	float closestTravel = FLT_MAX;

	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "info_ff_script" ) ) != NULL )
	{
		CFFNavArea *area = NULL;
		if ( !IsValidHealthSource( me, e, &area ) )
			continue;
		if ( !area || !area->IsMarked() )
			continue;
		if ( area->GetCostSoFar() < closestTravel )
		{
			closestTravel = area->GetCostSoFar();
			closest = e;
		}
	}

	e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "ff_item_healthdrop" ) ) != NULL )
	{
		CFFNavArea *area = NULL;
		if ( !IsValidHealthSource( me, e, &area ) )
			continue;
		if ( !area || !area->IsMarked() )
			continue;
		if ( area->GetCostSoFar() < closestTravel )
		{
			closestTravel = area->GetCostSoFar();
			closest = e;
		}
	}

	e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "ff_item_backpack" ) ) != NULL )
	{
		CFFNavArea *area = NULL;
		if ( !IsValidHealthSource( me, e, &area ) )
			continue;
		if ( !area || !area->IsMarked() )
			continue;
		if ( area->GetCostSoFar() < closestTravel )
		{
			closestTravel = area->GetCostSoFar();
			closest = e;
		}
	}

	e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "FF_Dispenser" ) ) != NULL )
	{
		CFFNavArea *area = NULL;
		if ( !IsValidHealthSource( me, e, &area ) )
			continue;
		if ( !area || !area->IsMarked() )
			continue;
		if ( area->GetCostSoFar() < closestTravel )
		{
			closestTravel = area->GetCostSoFar();
			closest = e;
		}
	}

	if ( !closest )
		return false;

	s_possibleBot = me;
	s_possibleHealth = closest;
	s_possibleFrame = gpGlobals->framecount;
	return true;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotGetHealth::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	if ( s_possibleFrame != gpGlobals->framecount || s_possibleBot != me )
	{
		if ( !IsPossible( me ) || s_possibleHealth == NULL )
		{
			return Done( "Can't get health" );
		}
	}

	m_health = s_possibleHealth;
	m_isGoalDispenser = m_health->ClassMatches( "FF_Dispenser" );

	CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
	if ( !m_path.Compute( me, m_health->WorldSpaceCenter(), cost ) )
	{
		return Done( "No path to health" );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotGetHealth::Update( CFFBot *me, float interval )
{
	if ( m_health == NULL )
	{
		return Done( "Health source has been taken" );
	}

	if ( me->GetHealth() >= me->GetMaxHealth() )
	{
		return Done( "I've been healed" );
	}

	if ( m_isGoalDispenser )
	{
		const float nearRange = 75.0f;
		if ( ( me->GetAbsOrigin() - m_health->GetAbsOrigin() ).IsLengthLessThan( nearRange ) )
		{
			IVision *vision = me->GetVisionInterface();
			if ( vision && vision->IsLineOfSightClearToEntity( m_health ) )
			{
				if ( me->GetHealth() >= me->GetMaxHealth() )
					return Done( "Dispenser refilled me" );

				// Don't camp the dispenser if a threat is visible — fight back.
				if ( vision->GetPrimaryKnownThreat() && vision->GetPrimaryKnownThreat()->IsVisibleInFOVNow() )
					return Done( "Combat — must fight, not heal" );

				return Continue();
			}
		}
	}

	if ( !m_path.IsValid() )
	{
		// May have overshot a momentarily-gone health item; recompute.
		CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
		if ( !m_path.Compute( me, m_health->WorldSpaceCenter(), cost ) )
			return Done( "Path became invalid" );
	}

	const CKnownEntity *threat = me->GetVisionInterface() ? me->GetVisionInterface()->GetPrimaryKnownThreat() : NULL;
	me->EquipBestWeaponForThreat( threat );

	// FIX 1 — single movement authority. CanDrivePath publishes the
	// path goal for the aim driver and refuses while the movement
	// arbiter owns locomotion, so this can never issue a second,
	// contradictory Approach() in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me );
	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetHealth::OnStuck( CFFBot *me )
{
	return TryDone( RESULT_CRITICAL, "Stuck trying to reach health" );
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetHealth::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetHealth::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	return TryDone( RESULT_CRITICAL, "Failed to reach health" );
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotGetHealth::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_YES;
}
