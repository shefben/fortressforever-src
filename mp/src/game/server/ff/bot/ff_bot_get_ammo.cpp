//========= Fortress Forever Bot =============================================//
//
// CFFBotGetAmmo — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_get_ammo.h"
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


ConVar ff_bot_ammo_search_range( "ff_bot_ammo_search_range", "5000",
	FCVAR_CHEAT, "Travel distance bots will search to find ammo." );


//-----------------------------------------------------------------------------
// Per-frame cache: lets a parent action call IsPossible() and have the
// chosen candidate persist into OnStart on the same tick without rescanning.
//-----------------------------------------------------------------------------
static CFFBot *s_possibleBot = NULL;
static CHandle< CBaseEntity > s_possibleAmmo;
static int s_possibleFrame = 0;


//-----------------------------------------------------------------------------
// Filter: is this candidate a valid ammo source for `me`?
// Uses the candidate's nearest CFFNavArea (passed by ref so caller can read
// the area's GetCostSoFar() — set by CollectSurroundingAreas BFS).
//-----------------------------------------------------------------------------
static bool IsValidAmmoSource( CFFBot *me, CBaseEntity *candidate, CFFNavArea **outArea )
{
	*outArea = NULL;
	if ( !candidate || !TheNavMesh )
		return false;

	CNavArea *base = TheNavMesh->GetNearestNavArea( candidate->WorldSpaceCenter() );
	if ( !base )
		return false;
	*outArea = static_cast< CFFNavArea * >( base );

	// Static map ammo: info_ff_script with kBackPack_Ammo or kBackPack_Grenades
	// goalType. Tagger already converted these to FF_NAV_HAS_AMMO etc., but we
	// scan the entities here so we can pick up "removed/respawning" state.
	if ( candidate->ClassMatches( "info_ff_script" ) )
	{
		CFFInfoScript *script = static_cast< CFFInfoScript * >( candidate );
		const int goalType = script->GetBotGoalType();
		if ( goalType == Omnibot::kBackPack_Ammo ||
		     goalType == Omnibot::kBackPack_Grenades )
		{
			return !script->IsRemoved() && !candidate->IsEffectActive( EF_NODRAW );
		}
		return false;
	}

	// Dropped backpacks: anyone can pick up.
	if ( candidate->ClassMatches( "ff_item_backpack" ) )
	{
		return !candidate->IsEffectActive( EF_NODRAW );
	}

	// Friendly dispensers: must be built and on our team.
	if ( candidate->ClassMatches( "FF_Dispenser" ) )
	{
		CFFDispenser *disp = static_cast< CFFDispenser * >( candidate );
		if ( !disp->IsBuilt() )
			return false;
		if ( disp->GetTeamNumber() != me->GetTeamNumber() )
			return false;
		// Engineers prefer to gather backpacks rather than leech off their
		// own dispenser when their SG is up. Mirror TFBot behavior.
		if ( me->GetClassSlot() == CLASS_ENGINEER )
			return false;
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
CFFBotGetAmmo::CFFBotGetAmmo( void )
{
	m_path.Invalidate();
	m_ammo = NULL;
	m_isGoalDispenser = false;
}


//-----------------------------------------------------------------------------
bool CFFBotGetAmmo::IsPossible( CFFBot *me )
{
	if ( !me )
		return false;

	CNavArea *startArea = me->GetLastKnownArea();
	if ( !startArea )
		return false;

	ILocomotion *loco = me->GetLocomotionInterface();
	const float stepHeight = loco ? loco->GetStepHeight() : 18.0f;
	const float dropHeight = loco ? loco->GetDeathDropHeight() : 200.0f;

	CUtlVector< CNavArea * > nearby;
	CollectSurroundingAreas( &nearby, startArea,
		ff_bot_ammo_search_range.GetFloat(), stepHeight, dropHeight );

	CBaseEntity *closest = NULL;
	float closestTravel = FLT_MAX;

	// Sweep static map ammo (info_ff_script with backpack-style goalType).
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "info_ff_script" ) ) != NULL )
	{
		CFFNavArea *area = NULL;
		if ( !IsValidAmmoSource( me, e, &area ) )
			continue;
		if ( !area || !area->IsMarked() )
			continue;
		if ( area->GetCostSoFar() < closestTravel )
		{
			closestTravel = area->GetCostSoFar();
			closest = e;
		}
	}

	// Dropped backpacks.
	e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "ff_item_backpack" ) ) != NULL )
	{
		CFFNavArea *area = NULL;
		if ( !IsValidAmmoSource( me, e, &area ) )
			continue;
		if ( !area || !area->IsMarked() )
			continue;
		if ( area->GetCostSoFar() < closestTravel )
		{
			closestTravel = area->GetCostSoFar();
			closest = e;
		}
	}

	// Friendly dispensers.
	e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "FF_Dispenser" ) ) != NULL )
	{
		CFFNavArea *area = NULL;
		if ( !IsValidAmmoSource( me, e, &area ) )
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
	s_possibleAmmo = closest;
	s_possibleFrame = gpGlobals->framecount;
	return true;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotGetAmmo::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	// Reuse cached probe if it ran this same tick for this bot.
	if ( s_possibleFrame != gpGlobals->framecount || s_possibleBot != me )
	{
		if ( !IsPossible( me ) || s_possibleAmmo == NULL )
		{
			return Done( "Can't get ammo" );
		}
	}

	m_ammo = s_possibleAmmo;
	m_isGoalDispenser = m_ammo->ClassMatches( "FF_Dispenser" );

	CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
	if ( !m_path.Compute( me, m_ammo->WorldSpaceCenter(), cost ) )
	{
		return Done( "No path to ammo" );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotGetAmmo::Update( CFFBot *me, float interval )
{
	if ( me->IsAmmoFull() )
	{
		return Done( "Ammo is full" );
	}

	if ( m_ammo == NULL )
	{
		return Done( "Ammo source has been taken" );
	}

	if ( m_isGoalDispenser )
	{
		// Get within touch range and wait — dispenser refills on touch tick.
		const float nearRange = 75.0f;
		if ( ( me->GetAbsOrigin() - m_ammo->GetAbsOrigin() ).IsLengthLessThan( nearRange ) )
		{
			IVision *vision = me->GetVisionInterface();
			if ( vision && vision->IsLineOfSightClearToEntity( m_ammo ) )
			{
				if ( me->IsAmmoFull() )
					return Done( "Dispenser refilled me" );

				// Don't camp the dispenser if we're being shot at and we
				// have *some* ammo to work with.
				if ( !me->IsAmmoLow() && vision->GetPrimaryKnownThreat() )
					return Done( "Combat — must fight, not refill" );

				return Continue();
			}
		}
	}

	if ( !m_path.IsValid() )
	{
		return Done( "Path became invalid" );
	}

	// Refresh weapon — we may have just run dry on the active weapon.
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
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnStuck( CFFBot *me )
{
	return TryDone( RESULT_CRITICAL, "Stuck trying to reach ammo" );
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	return TryDone( RESULT_CRITICAL, "Failed to reach ammo" );
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotGetAmmo::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_YES;
}
