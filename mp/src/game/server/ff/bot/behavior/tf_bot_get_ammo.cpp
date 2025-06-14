//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_get_ammo.h
// Pick up any nearby ammo
// Michael Booth, May 2009

#include "cbase.h"
#include "ff_obj.h"
#include "ff_gamerules.h"
#include "bot/ff_bot.h"
#include "bot/behavior/ff_bot_get_ammo.h"

extern ConVar ff_bot_path_lookahead_range;

ConVar ff_bot_ammo_search_range( "ff_bot_ammo_search_range", "5000", FCVAR_CHEAT, "How far bots will search to find ammo around them" );
ConVar ff_bot_debug_ammo_scavenging( "ff_bot_debug_ammo_scavenging", "0", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
CFFBotGetAmmo::CFFBotGetAmmo( void )
{
	m_path.Invalidate();
	m_ammo = NULL;
	m_isGoalDispenser = false;
}


//---------------------------------------------------------------------------------------------
class CAmmoFilter : public INextBotFilter
{
public:
	CAmmoFilter( CFFBot *me )
	{
		m_me = me;
		m_ammoArea = NULL;
	}

	bool IsSelected( const CBaseEntity *constCandidate ) const
	{
		CBaseEntity *candidate = const_cast< CBaseEntity * >( constCandidate );

		m_ammoArea = (CTFNavArea *)TheNavMesh->GetNearestNavArea( candidate->WorldSpaceCenter() );
		if ( !m_ammoArea )
			return false;

		CClosestTFPlayer close( candidate );
		ForEachPlayer( close );

		// if the closest player to this candidate object is an enemy, don't use it
		if ( close.m_closePlayer && !m_me->InSameTeam( close.m_closePlayer ) )
			return false;

		// resupply cabinets (not assigned a team)
		if ( candidate->ClassMatches( "func_regenerate" ) )
		{
			if ( !m_ammoArea->HasAttributeTF( TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED ) )
			{
				// Assume any resupply cabinets not in a teamed spawn room are inaccessible.
				// Ex: pl_upward has forward spawn rooms that neither team can use until 
				// certain checkpoints are reached.
				return false;
			}

			if ( ( m_me->GetTeamNumber() == FF_TEAM_RED && m_ammoArea->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED ) ) ||
				 ( m_me->GetTeamNumber() == FF_TEAM_BLUE && m_ammoArea->HasAttributeTF( TF_NAV_SPAWN_ROOM_BLUE ) ) )
			{
				// the supply cabinet is in my spawn room, or not in any spawn room
				return true;
			}
			return false;
		}

		// ignore non-existent ammo to ensure we collect nearby existing ammo
		if ( candidate->IsEffectActive( EF_NODRAW ) )
			return false;

		if ( candidate->ClassMatches( "ff_ammo_pack" ) )
			return true;

		if ( candidate->ClassMatches( "item_ammopack*" ) )
			return true;

		if ( m_me->InSameTeam( candidate ) )
		{
			// friendly engineer's dispenser
			if ( candidate->ClassMatches( "obj_dispenser*" ) )
			{
				// for now, assume Engineers want to go fetch ammo boxes unless their dispenser is fully upgraded
				// unless we have no sentry yet, then we need to leech off of buddy's dispenser to get started
				if ( !m_me->IsPlayerClass( CLASS_ENGINEER ) || ( (CBaseObject *)candidate )->GetUpgradeLevel() >= 3 || !m_me->GetObjectOfType( OBJ_SENTRYGUN ) )
				{
					CBaseObject	*dispenser = (CBaseObject *)candidate;
					if ( !dispenser->IsBuilding() && !dispenser->IsDisabled() )
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	CFFBot *m_me;
	mutable CTFNavArea *m_ammoArea;
};


//---------------------------------------------------------------------------------------------
static CFFBot *s_possibleBot = NULL;
static CHandle< CBaseEntity > s_possibleAmmo = NULL;
static int s_possibleFrame = 0;


//---------------------------------------------------------------------------------------------
/**
 * Return true if this Action has what it needs to perform right now
 */
bool CFFBotGetAmmo::IsPossible( CFFBot *me )
{
	VPROF_BUDGET( "CFFBotGetAmmo::IsPossible", "NextBot" );

	int i;

	CUtlVector< CNavArea * > nearbyAreaVector;
	CollectSurroundingAreas( &nearbyAreaVector, me->GetLastKnownArea(), ff_bot_ammo_search_range.GetFloat(), me->GetLocomotionInterface()->GetStepHeight(), me->GetLocomotionInterface()->GetDeathDropHeight() );

	CAmmoFilter ammoFilter( me );

	const CUtlVector< CHandle< CBaseEntity > > &staticAmmoVector = TFGameRules()->GetAmmoEntityVector();
	CBaseEntity *closestAmmo = NULL;
	float closestAmmoTravelDistance = FLT_MAX;

	for( i=0; i<staticAmmoVector.Count(); ++i )
	{
		CBaseEntity *ammo = staticAmmoVector[i];
		if ( ammo )
		{
			if ( ammoFilter.IsSelected( ammo ) )
			{
				if ( ammoFilter.m_ammoArea && ammoFilter.m_ammoArea->IsMarked() )
				{
					// "cost so far" was computed during the breadth first search within CollectSurroundingAreas()
					// and is the travel distance from to this area
					if ( ammoFilter.m_ammoArea->GetCostSoFar() < closestAmmoTravelDistance )
					{
						closestAmmo = ammo;
						closestAmmoTravelDistance = ammoFilter.m_ammoArea->GetCostSoFar();
					}

					if ( ff_bot_debug_ammo_scavenging.GetBool() )
					{
						NDebugOverlay::Cross3D( ammo->WorldSpaceCenter(), 5.0f, 255, 255, 0, true, 999.9 );
					}
				}
			}
		}
	}

	// append nearby dropped weapons
	CBaseEntity *ammoPack = NULL;
	while( ( ammoPack = gEntList.FindEntityByClassname( ammoPack, "ff_ammo_pack" ) ) != NULL )
	{
		if ( ammoFilter.IsSelected( ammoPack ) )
		{
			if ( ammoFilter.m_ammoArea && ammoFilter.m_ammoArea->IsMarked() )
			{
				if ( ammoFilter.m_ammoArea->GetCostSoFar() < closestAmmoTravelDistance )
				{
					closestAmmo = ammoPack;
					closestAmmoTravelDistance = ammoFilter.m_ammoArea->GetCostSoFar();
				}

				if ( ff_bot_debug_ammo_scavenging.GetBool() )
				{
					NDebugOverlay::Cross3D( ammoPack->WorldSpaceCenter(), 5.0f, 255, 100, 0, true, 999.9 );
				}
			}
		}
	}

	if ( !closestAmmo )
	{
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
		{
			Warning( "%3.2f: No ammo nearby\n", gpGlobals->curtime );
		}
		return false;
	}

	s_possibleBot = me;
	s_possibleAmmo = closestAmmo;
	s_possibleFrame = gpGlobals->framecount;

	return true;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotGetAmmo::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	VPROF_BUDGET( "CFFBotGetAmmo::OnStart", "NextBot" );

	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	// if IsPossible() has already been called, use its cached data
	if ( s_possibleFrame != gpGlobals->framecount || s_possibleBot != me )
	{
		if ( !IsPossible( me ) || s_possibleAmmo == NULL )
		{
			return Done( "Can't get ammo" );
		}
	}

	m_ammo = s_possibleAmmo;
	m_isGoalDispenser = m_ammo->ClassMatches( "obj_dispenser*" );

	CFFBotPathCost cost( me, FASTEST_ROUTE );
	if ( !m_path.Compute( me, m_ammo->WorldSpaceCenter(), cost ) )
	{
		return Done( "No path to ammo!" );
	}

	// if I'm a spy, cloak and disguise
	if ( me->IsPlayerClass( CLASS_SPY ) )
	{
		if ( !me->m_Shared.IsStealthed() )
		{
			me->PressAltFireButton();
		}
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotGetAmmo::Update( CFFBot *me, float interval )
{
	if ( me->IsAmmoFull() )
	{
		return Done( "My ammo is full" );
	}

	if ( m_ammo == NULL ) // || ( m_ammo->IsEffectActive( EF_NODRAW ) && !FClassnameIs( m_ammo, "func_regenerate" ) ) )
	{
/*
		// engineers try to gather all the metal they can
		if ( me->IsPlayerClass( CLASS_ENGINEER ) && CFFBotGetAmmo::IsPossible( me ) )
		{
			// more ammo to be had
			return ChangeTo( new CFFBotGetAmmo, "Not full yet - grabbing more ammo" );
		}
*/

		return Done( "Ammo I was going for has been taken" );
	}

	if ( m_isGoalDispenser )
	{
		// we need to get near and wait, not try to run over
		const float nearRange = 75.0f;
		if ( ( me->GetAbsOrigin() - m_ammo->GetAbsOrigin() ).IsLengthLessThan( nearRange ) )
		{
			if ( me->GetVisionInterface()->IsLineOfSightClearToEntity( m_ammo ) )
			{
				if ( me->IsAmmoFull() )
				{
					return Done( "Ammo refilled by the Dispenser" );
				}

				// don't wait if I'm in combat
				if ( !me->IsAmmoLow() && me->GetVisionInterface()->GetPrimaryKnownThreat() )
				{
					return Done( "No time to wait for more ammo, I must fight" );
				}

				// wait until the dispenser refills us
				return Continue();
			}
		}
	}

	if ( !m_path.IsValid() )
	{
		return Done( "My path became invalid" );
	}

/* TODO: Rethink this. Currently creates zombie behavior loop.
	// if the closest player to the item we're after is an enemy, give up
	CClosestTFPlayer close( m_ammo );
	ForEachPlayer( close );
	if ( close.m_closePlayer && !me->InSameTeam( close.m_closePlayer ) )
		return Done( "An enemy is closer to it" );
*/

	// may need to switch weapons due to out of ammo
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	me->EquipBestWeaponForThreat( threat );

	m_path.Update( me );

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnStuck( CFFBot *me )
{
	return TryDone( RESULT_CRITICAL, "Stuck trying to reach ammo" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotGetAmmo::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	return TryDone( RESULT_CRITICAL, "Failed to reach ammo" );
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotGetAmmo::ShouldHurry( const INextBot *me ) const
{
	// if we need ammo, we best hustle
	return ANSWER_YES;
}
