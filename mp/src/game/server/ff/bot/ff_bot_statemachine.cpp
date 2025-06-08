//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h" // For TheFFBots()
#include "../ff_player.h"     // For CFFPlayer
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase (potentially used via CFFBot)
// #include "../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
// #include "../../shared/ff/ff_gamerules.h" // For FFGameRules() (potentially used)
#include "ff_gamestate.h"   // For FFGameState
#include "nav_mesh.h"       // For TheNavMesh, CNavArea
#include "nav_pathfind.h"   // For PathCost, NavAreaBuildPath, CNavPath (formerly CCSNavPath)
#include "nav_hiding_spot.h"// For FindNearbyHidingSpot, FindInitialEncounterSpot
#include "bot_constants.h"  // For TaskType, RouteType, RadioType etc.

// TODO: cs_nav_path.h was included. If CFFNavPath is a direct replacement, include "ff_nav_path.h".
// Otherwise, nav_pathfind.h or nav_mesh.h might provide CNavPath or similar generic path objects.
// For now, assuming CNavPath from nav_pathfind.h or similar.


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//--------------------------------------------------------------------------------------------------------------
/**
 * This method is the ONLY legal way to change a bot's current state
 */
void CFFBot::SetState( BotState *state )
{
	if (!state) return; // Null check

	PrintIfWatched( "%s: SetState: %s -> %s\n", GetPlayerName(), (m_state) ? m_state->GetName() : "NULL", state->GetName() );

	// if we changed state from within the special Attack state, we are no longer attacking
	if (m_isAttacking)
		StopAttacking();

	if (m_state)
		m_state->OnExit( this );

	state->OnEnter( this );

	m_state = state;
	m_stateTimestamp = gpGlobals->curtime;
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::Idle( void )
{
	SetTask( CFFBot::SEEK_AND_DESTROY ); // Ensure TaskType enums are accessible
	SetState( &m_idleState );
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::EscapeFromBomb( void )
{
	SetTask( CFFBot::ESCAPE_FROM_BOMB ); // Ensure TaskType enums are accessible
	SetState( &m_escapeFromBombState );
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::Follow( CFFPlayer *player )
{
	if (player == NULL)
		return;

	// note when we began following
	if (!m_isFollowing || m_leader != player)
		m_followTimestamp = gpGlobals->curtime;

	m_isFollowing = true;
	m_leader = player;

	SetTask( CFFBot::FOLLOW ); // Ensure TaskType enums are accessible
	m_followState.SetLeader( player );
	SetState( &m_followState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Continue following our leader after finishing what we were doing
 */
void CFFBot::ContinueFollowing( void )
{
	SetTask( CFFBot::FOLLOW ); // Ensure TaskType enums are accessible

	if (!m_leader.IsValid() || !m_leader.Get()) { // Check if leader is valid
		Idle(); // Leader lost, go to idle
		return;
	}
	m_followState.SetLeader( m_leader.Get() ); // Use Get() for EHANDLE

	SetState( &m_followState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Stop following
 */
void CFFBot::StopFollowing( void )
{
	m_isFollowing = false;
	m_leader = NULL;
	m_allowAutoFollowTime = gpGlobals->curtime + 10.0f;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Begin process of rescuing hostages
 */
// TODO: Hostage logic is CS-specific. Adapt or remove for FF.
void CFFBot::RescueHostages( void )
{
	SetTask( CFFBot::RESCUE_HOSTAGES ); // Ensure TaskType enums are accessible
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Use the entity
 */
void CFFBot::UseEntity( CBaseEntity *entity )
{
	if (!entity) return; // Null check
	m_useEntityState.SetEntity( entity );
	SetState( &m_useEntityState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Open the door.
 * This assumes the bot is directly in front of the door with no obstructions.
 * NOTE: This state is special, like Attack, in that it suspends the current behavior and returns to it when done.
 */
void CFFBot::OpenDoor( CBaseEntity *door )
{
	if(!door) return; // Null check
	m_openDoorState.SetDoor( door );
	m_isOpeningDoor = true; // This implies m_openDoorState might not use SetState to become active immediately
	m_openDoorState.OnEnter( this );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * DEPRECATED: Use TryToHide() instead.
 * Move to a hiding place.
 * If 'searchFromArea' is non-NULL, hiding spots are looked for from that area first.
 */
void CFFBot::Hide( CNavArea *searchFromArea, float duration, float hideRange, bool holdPosition )
{
	DestroyPath();

	CNavArea *source;
	Vector sourcePos;
	if (searchFromArea)
	{
		source = searchFromArea;
		sourcePos = searchFromArea->GetCenter();
	}
	else
	{
		source = m_lastKnownArea;
		if (source) sourcePos = source->GetCenter(); // Null check m_lastKnownArea
		else sourcePos = GetCentroid( this ); // Fallback if no area
	}

	if (source == NULL && !searchFromArea) // Corrected logic: if searchFromArea was NULL and m_lastKnownArea was NULL
	{
		PrintIfWatched( "Hide from area is NULL.\n" );
		Idle();
		return;
	}

	m_hideState.SetSearchArea( source ); // source can still be null if searchFromArea was null and m_lastKnownArea was null
	m_hideState.SetSearchRange( hideRange );
	m_hideState.SetDuration( duration );
	m_hideState.SetHoldPosition( holdPosition );

	// search around source area for a good hiding spot
	Vector useSpot;

	// FindNearbyHidingSpot needs to be defined/ported
	const Vector *pos = FindNearbyHidingSpot( this, sourcePos, hideRange, IsSniper() );
	if (pos == NULL)
	{
		PrintIfWatched( "No available hiding spots.\n" );
		// hide at our current position
		useSpot = GetCentroid( this );
	}
	else
	{
		useSpot = *pos;
	}

	m_hideState.SetHidingSpot( useSpot );

	// build a path to our new hiding spot
	if (ComputePath( useSpot, FASTEST_ROUTE ) == false) // FASTEST_ROUTE
	{
		PrintIfWatched( "Can't pathfind to hiding spot\n" );
		Idle();
		return;
	}

	SetState( &m_hideState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Move to the given hiding place
 */
void CFFBot::Hide( const Vector &hidingSpot, float duration, bool holdPosition )
{
	if (!TheNavMesh) { Idle(); return; } // Null check
	CNavArea *hideArea = TheNavMesh->GetNearestNavArea( hidingSpot );
	if (hideArea == NULL)
	{
		PrintIfWatched( "Hiding spot off nav mesh\n" );
		Idle();
		return;
	}

	DestroyPath();

	m_hideState.SetSearchArea( hideArea );
	m_hideState.SetSearchRange( 750.0f );
	m_hideState.SetDuration( duration );
	m_hideState.SetHoldPosition( holdPosition );
	m_hideState.SetHidingSpot( hidingSpot );

	// build a path to our new hiding spot
	if (ComputePath( hidingSpot, FASTEST_ROUTE ) == false) // FASTEST_ROUTE
	{
		PrintIfWatched( "Can't pathfind to hiding spot\n" );
		Idle();
		return;
	}

	SetState( &m_hideState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Try to hide nearby.  Return true if hiding, false if can't hide here.
 * If 'searchFromArea' is non-NULL, hiding spots are looked for from that area first.
 */
bool CFFBot::TryToHide( CNavArea *searchFromArea, float duration, float hideRange, bool holdPosition, bool useNearest )
{
	CNavArea *source;
	Vector sourcePos;
	if (searchFromArea)
	{
		source = searchFromArea;
		sourcePos = searchFromArea->GetCenter();
	}
	else
	{
		source = m_lastKnownArea;
		if (source) sourcePos = source->GetCenter(); // Null check
		else sourcePos = GetCentroid( this ); // Fallback
	}

	if (source == NULL && !searchFromArea) // Corrected logic
	{
		PrintIfWatched( "Hide from area is NULL.\n" );
		return false;
	}

	m_hideState.SetSearchArea( source );
	m_hideState.SetSearchRange( hideRange );
	m_hideState.SetDuration( duration );
	m_hideState.SetHoldPosition( holdPosition );

	// search around source area for a good hiding spot
	const Vector *pos = FindNearbyHidingSpot( this, sourcePos, hideRange, IsSniper(), useNearest ); // FindNearbyHidingSpot
	if (pos == NULL)
	{
		PrintIfWatched( "No available hiding spots.\n" );
		return false;
	}

	m_hideState.SetHidingSpot( *pos );

	// build a path to our new hiding spot
	if (ComputePath( *pos, FASTEST_ROUTE ) == false) // FASTEST_ROUTE
	{
		PrintIfWatched( "Can't pathfind to hiding spot\n" );
		return false;
	}

	SetState( &m_hideState );
	return true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Retreat to a nearby hiding spot, away from enemies
 */
bool CFFBot::TryToRetreat( float maxRange, float duration )
{
	const Vector *spot = FindNearbyRetreatSpot( this, maxRange ); // FindNearbyRetreatSpot
	if (spot)
	{
		// ignore enemies for a second to give us time to hide
		// reaching our hiding spot clears our disposition
		IgnoreEnemies( 10.0f );

		if (duration < 0.0f)
		{
			duration = RandomFloat( 3.0f, 15.0f );
		}

		StandUp();
		Run();
		Hide( *spot, duration );

		PrintIfWatched( "Retreating to a safe spot!\n" );

		return true;
	}

	return false;
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::Hunt( void )
{
	SetState( &m_huntState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Attack our the given victim
 * NOTE: Attacking does not change our task.
 */
void CFFBot::Attack( CFFPlayer *victim )
{
	if (victim == NULL)
		return;

	// zombies never attack
	if (cv_bot_zombie.GetBool()) // cv_bot_zombie needs to be accessible
		return;

	// cannot attack if we are reloading
	if (IsReloading())
		return;

	// change enemy
	SetBotEnemy( victim );

	//
	// Do not "re-enter" the attack state if we are already attacking
	//
	if (IsAttacking())
		return;

	// if we're holding a grenade, throw it at the victim
	if (IsUsingGrenade())
	{
		// throw towards their feet
		ThrowGrenade( victim->GetAbsOrigin() );
		return;
	}


	// if we are currently hiding, increase our chances of crouching and holding position
	if (IsAtHidingSpot())
		m_attackState.SetCrouchAndHold( (RandomFloat( 0.0f, 100.0f ) < 60.0f) ? true : false );
	else
		m_attackState.SetCrouchAndHold( false );

	m_isAttacking = true;
	m_attackState.OnEnter( this );


	Vector victimOrigin = GetCentroid( victim );

	// cheat a bit and give the bot the initial location of its victim
	m_lastEnemyPosition = victimOrigin;
	m_lastSawEnemyTimestamp = gpGlobals->curtime;
	m_aimSpreadTimestamp = gpGlobals->curtime;

	// compute the angle difference between where are looking, and where we need to look
	Vector toEnemy = victimOrigin - GetCentroid( this );

	QAngle idealAngle;
	VectorAngles( toEnemy, idealAngle );

	float deltaYaw = (float)fabs(m_lookYaw - idealAngle.y);

	while( deltaYaw > 180.0f )
		deltaYaw -= 360.0f;

	if (deltaYaw < 0.0f)
		deltaYaw = -deltaYaw;

	// immediately aim at enemy - accuracy penalty depending on how far we must turn to aim
	// accuracy is halved if we have to turn 180 degrees
	float turn = deltaYaw / 180.0f;
	if (GetProfile()) // Null check
	{
		float accuracy = GetProfile()->GetSkill() / (1.0f + turn);
		SetAimOffset( accuracy );
	}


	// define time when aim offset will automatically be updated
	// longer time the more we had to turn (surprise)
	m_aimOffsetTimestamp = gpGlobals->curtime + RandomFloat( 0.25f + turn, 1.5f );

	// forget any look at targets we have
	ClearLookAt();
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Exit the Attack state
 */
void CFFBot::StopAttacking( void )
{
	PrintIfWatched( "ATTACK END\n" );
	m_attackState.OnExit( this );
	m_isAttacking = false;

	// if we are following someone, go to the Idle state after the attack to decide whether we still want to follow
	if (IsFollowing())
	{
		Idle();
	}
}


//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsAttacking( void ) const
{
	return m_isAttacking;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are escaping from the bomb
 */
bool CFFBot::IsEscapingFromBomb( void ) const
{
	if (m_state == static_cast<const BotState *>( &m_escapeFromBombState ))
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are defusing the bomb
 */
bool CFFBot::IsDefusingBomb( void ) const
{
	if (m_state == static_cast<const BotState *>( &m_defuseBombState ))
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are hiding
 */
bool CFFBot::IsHiding( void ) const
{
	if (m_state == static_cast<const BotState *>( &m_hideState ))
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are hiding and at our hiding spot
 */
bool CFFBot::IsAtHidingSpot( void ) const
{
	if (!IsHiding())
		return false;

	return m_hideState.IsAtSpot();
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return number of seconds we have been at our current hiding spot
 */
float CFFBot::GetHidingTime( void ) const
{
	if (IsHiding())
	{
		return m_hideState.GetHideTime();
	}

	return 0.0f;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are huting
 */
bool CFFBot::IsHunting( void ) const
{
	if (m_state == static_cast<const BotState *>( &m_huntState ))
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are in the MoveTo state
 */
bool CFFBot::IsMovingTo( void ) const
{
	if (m_state == static_cast<const BotState *>( &m_moveToState ))
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are buying
 */
bool CFFBot::IsBuying( void ) const
{
	if (m_state == static_cast<const BotState *>( &m_buyState ))
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsInvestigatingNoise( void ) const
{
	if (m_state == static_cast<const BotState *>( &m_investigateNoiseState ))
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Move to potentially distant position
 */
void CFFBot::MoveTo( const Vector &pos, RouteType route ) // RouteType
{
	m_moveToState.SetGoalPosition( pos );
	m_moveToState.SetRouteType( route );
	SetState( &m_moveToState );
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::PlantBomb( void )
{
	SetState( &m_plantBombState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Bomb has been dropped - go get it
 */
void CFFBot::FetchBomb( void )
{
	SetState( &m_fetchBombState );
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::DefuseBomb( void )
{
	SetState( &m_defuseBombState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Investigate recent enemy noise
 */
void CFFBot::InvestigateNoise( void )
{
	SetState( &m_investigateNoiseState );
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::Buy( void )
{
	SetState( &m_buyState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Move to a hiding spot and wait for initial encounter with enemy team.
 * Return false if can't do this behavior (ie: no hiding spots available).
 */
// TODO: This logic is highly CS-specific (teams, enemy spawn names)
bool CFFBot::MoveToInitialEncounter( void )
{
	int myTeam = GetTeamNumber();
	int enemyTeam = OtherTeam( myTeam ); // OtherTeam needs to be FF compatible

	// build a path to an enemy spawn point
	if (!TheFFBots()) return false; // Null check
	CBaseEntity *enemySpawn = TheFFBots()->GetRandomSpawn( enemyTeam ); // Ensure GetRandomSpawn handles FF teams

	if (enemySpawn == NULL)
	{
		PrintIfWatched( "MoveToInitialEncounter: No enemy spawn points?\n" );
		return false;
	}

	// build a path from us to the enemy spawn
	CNavPath path; // Assuming CNavPath is the generic path object
	PathCost cost( this, FASTEST_ROUTE ); // PathCost, FASTEST_ROUTE
	// path.Compute( WorldSpaceCenter(), enemySpawn->GetAbsOrigin(), cost ); // CNavPath::Compute might differ
	if ( !NavAreaBuildPath( m_lastKnownArea, TheNavMesh->GetNearestNavArea(enemySpawn->GetAbsOrigin()), &(enemySpawn->GetAbsOrigin()), cost, NULL, TheNavMesh->GetMaxPathNodes(), true, &path ) )
	{
		PrintIfWatched( "MoveToInitialEncounter: Pathfind failed.\n" );
		return false;
	}


	if (!path.IsValid())
	{
		PrintIfWatched( "MoveToInitialEncounter: Pathfind failed (path not valid).\n" );
		return false;
	}

	// find battlefront area where teams will first meet along this path
	int i;
	for( i=0; i<path.GetSegmentCount(); ++i )
	{
		const CNavPathSegment *segment = path.GetSegment(i);
		if (!segment || !segment->area) continue; // Null checks
		if (segment->area->GetEarliestOccupyTime( myTeam ) > segment->area->GetEarliestOccupyTime( enemyTeam ))
		{
			break;
		}
	}

	if (i == path.GetSegmentCount())
	{
		PrintIfWatched( "MoveToInitialEncounter: Can't find battlefront!\n" );
		return false;
	}

	const CNavPathSegment *battleSegment = path.GetSegment(i);
	if (!battleSegment || !battleSegment->area) return false; // Null check

	/// @todo Remove this evil side-effect
	SetInitialEncounterArea( battleSegment->area );

	// find a hiding spot on our side of the battlefront that has LOS to it
	const float maxRange = 1500.0f;
	const HidingSpot *spot = FindInitialEncounterSpot( this, battleSegment->area->GetCenter(), battleSegment->area->GetEarliestOccupyTime( enemyTeam ), maxRange, IsSniper() ); // FindInitialEncounterSpot

	if (spot == NULL)
	{
		PrintIfWatched( "MoveToInitialEncounter: Can't find a hiding spot\n" );
		return false;
	}

	if (!spot->GetArea()) return false; // Null check

	float timeToWait = battleSegment->area->GetEarliestOccupyTime( enemyTeam ) - spot->GetArea()->GetEarliestOccupyTime( myTeam );
	float minWaitTime = 4.0f * (GetProfile() ? GetProfile()->GetAggression() : 0.5f) + 3.0f; // Null check GetProfile
	if (timeToWait < minWaitTime)
	{
		timeToWait = minWaitTime;
	}

	Hide( spot->GetPosition(), timeToWait );

	return true;
}
