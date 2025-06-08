//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "../ff_player.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "ff_gamestate.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "nav_hiding_spot.h"

// Local bot utility headers
#include "bot_constants.h"
#include "bot_profile.h"    // For GetProfile()
#include "bot_util.h"       // For PrintIfWatched

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

	PrintIfWatched(this, "%s: SetState: %s -> %s\n", GetPlayerName(), (m_state) ? m_state->GetName() : "NULL", state->GetName() ); // Updated PrintIfWatched

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
	SetTask( CFFBot::SEEK_AND_DESTROY );
	SetState( &m_idleState );
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::EscapeFromBomb( void )
{
	SetTask( CFFBot::ESCAPE_FROM_BOMB );
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

	SetTask( CFFBot::FOLLOW );
	m_followState.SetLeader( player );
	SetState( &m_followState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Continue following our leader after finishing what we were doing
 */
void CFFBot::ContinueFollowing( void )
{
	SetTask( CFFBot::FOLLOW );

	if (!m_leader.IsValid() || !m_leader.Get()) {
		Idle();
		return;
	}
	m_followState.SetLeader( m_leader.Get() );

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
	SetTask( CFFBot::RESCUE_HOSTAGES );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Use the entity
 */
void CFFBot::UseEntity( CBaseEntity *entity )
{
	if (!entity) return;
	m_useEntityState.SetEntity( entity );
	SetState( &m_useEntityState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Open the door.
 */
void CFFBot::OpenDoor( CBaseEntity *door )
{
	if(!door) return;
	m_openDoorState.SetDoor( door );
	m_isOpeningDoor = true;
	m_openDoorState.OnEnter( this );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * DEPRECATED: Use TryToHide() instead.
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
		if (source) sourcePos = source->GetCenter();
		else sourcePos = GetCentroid( this );
	}

	if (source == NULL && !searchFromArea)
	{
		PrintIfWatched(this, "Hide from area is NULL.\n" ); // Updated PrintIfWatched
		Idle();
		return;
	}

	m_hideState.SetSearchArea( source );
	m_hideState.SetSearchRange( hideRange );
	m_hideState.SetDuration( duration );
	m_hideState.SetHoldPosition( holdPosition );

	Vector useSpot;
	const Vector *pos = FindNearbyHidingSpot( this, sourcePos, hideRange, IsSniper() );
	if (pos == NULL)
	{
		PrintIfWatched(this, "No available hiding spots.\n" ); // Updated PrintIfWatched
		useSpot = GetCentroid( this );
	}
	else
	{
		useSpot = *pos;
	}

	m_hideState.SetHidingSpot( useSpot );

	if (ComputePath( useSpot, FASTEST_ROUTE ) == false)
	{
		PrintIfWatched(this, "Can't pathfind to hiding spot\n" ); // Updated PrintIfWatched
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
	if (!TheNavMesh) { Idle(); return; }
	CNavArea *hideArea = TheNavMesh->GetNearestNavArea( hidingSpot );
	if (hideArea == NULL)
	{
		PrintIfWatched(this, "Hiding spot off nav mesh\n" ); // Updated PrintIfWatched
		Idle();
		return;
	}

	DestroyPath();

	m_hideState.SetSearchArea( hideArea );
	m_hideState.SetSearchRange( 750.0f );
	m_hideState.SetDuration( duration );
	m_hideState.SetHoldPosition( holdPosition );
	m_hideState.SetHidingSpot( hidingSpot );

	if (ComputePath( hidingSpot, FASTEST_ROUTE ) == false)
	{
		PrintIfWatched(this, "Can't pathfind to hiding spot\n" ); // Updated PrintIfWatched
		Idle();
		return;
	}

	SetState( &m_hideState );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Try to hide nearby.  Return true if hiding, false if can't hide here.
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
		if (source) sourcePos = source->GetCenter();
		else sourcePos = GetCentroid( this );
	}

	if (source == NULL && !searchFromArea)
	{
		PrintIfWatched(this, "Hide from area is NULL.\n" ); // Updated PrintIfWatched
		return false;
	}

	m_hideState.SetSearchArea( source );
	m_hideState.SetSearchRange( hideRange );
	m_hideState.SetDuration( duration );
	m_hideState.SetHoldPosition( holdPosition );

	const Vector *pos = FindNearbyHidingSpot( this, sourcePos, hideRange, IsSniper(), useNearest );
	if (pos == NULL)
	{
		PrintIfWatched(this, "No available hiding spots.\n" ); // Updated PrintIfWatched
		return false;
	}

	m_hideState.SetHidingSpot( *pos );

	if (ComputePath( *pos, FASTEST_ROUTE ) == false)
	{
		PrintIfWatched(this, "Can't pathfind to hiding spot\n" ); // Updated PrintIfWatched
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
	const Vector *spot = FindNearbyRetreatSpot( this, maxRange );
	if (spot)
	{
		IgnoreEnemies( 10.0f );
		if (duration < 0.0f) duration = RandomFloat( 3.0f, 15.0f );
		StandUp();
		Run();
		Hide( *spot, duration );
		PrintIfWatched(this, "Retreating to a safe spot!\n" ); // Updated PrintIfWatched
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
 */
void CFFBot::Attack( CFFPlayer *victim )
{
	if (victim == NULL) return;
	if (cv_bot_zombie.GetBool()) return;
	if (IsReloading()) return;

	SetBotEnemy( victim );
	if (IsAttacking()) return;

	if (IsUsingGrenade()) // TODO: FF Grenade logic
	{
		ThrowGrenade( victim->GetAbsOrigin() );
		return;
	}

	if (IsAtHidingSpot()) m_attackState.SetCrouchAndHold( (RandomFloat( 0.0f, 100.0f ) < 60.0f) ? true : false );
	else m_attackState.SetCrouchAndHold( false );

	m_isAttacking = true;
	m_attackState.OnEnter( this );

	Vector victimOrigin = GetCentroid( victim );
	m_lastEnemyPosition = victimOrigin;
	m_lastSawEnemyTimestamp = gpGlobals->curtime;
	m_aimSpreadTimestamp = gpGlobals->curtime;

	Vector toEnemy = victimOrigin - GetCentroid( this );
	QAngle idealAngle;
	VectorAngles( toEnemy, idealAngle );
	float deltaYaw = (float)fabs(m_lookYaw - idealAngle.y);
	while( deltaYaw > 180.0f ) deltaYaw -= 360.0f;
	if (deltaYaw < 0.0f) deltaYaw = -deltaYaw;

	float turn = deltaYaw / 180.0f;
	if (GetProfile())
	{
		float accuracy = GetProfile()->GetSkill() / (1.0f + turn);
		SetAimOffset( accuracy );
	}
	m_aimOffsetTimestamp = gpGlobals->curtime + RandomFloat( 0.25f + turn, 1.5f );
	ClearLookAt();
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Exit the Attack state
 */
void CFFBot::StopAttacking( void )
{
	PrintIfWatched(this, "ATTACK END\n" ); // Updated PrintIfWatched
	m_attackState.OnExit( this );
	m_isAttacking = false;
	if (IsFollowing()) Idle();
}


//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsAttacking( void ) const { return m_isAttacking; }
bool CFFBot::IsEscapingFromBomb( void ) const { return (m_state == static_cast<const BotState *>( &m_escapeFromBombState )); }
bool CFFBot::IsDefusingBomb( void ) const { return (m_state == static_cast<const BotState *>( &m_defuseBombState )); }
bool CFFBot::IsHiding( void ) const { return (m_state == static_cast<const BotState *>( &m_hideState )); }
bool CFFBot::IsAtHidingSpot( void ) const { if (!IsHiding()) return false; return m_hideState.IsAtSpot(); }
float CFFBot::GetHidingTime( void ) const { if (IsHiding()) return m_hideState.GetHideTime(); return 0.0f; }
bool CFFBot::IsHunting( void ) const { return (m_state == static_cast<const BotState *>( &m_huntState )); }
bool CFFBot::IsMovingTo( void ) const { return (m_state == static_cast<const BotState *>( &m_moveToState )); }
bool CFFBot::IsBuying( void ) const { return (m_state == static_cast<const BotState *>( &m_buyState )); }
bool CFFBot::IsInvestigatingNoise( void ) const { return (m_state == static_cast<const BotState *>( &m_investigateNoiseState )); }

//--------------------------------------------------------------------------------------------------------------
void CFFBot::MoveTo( const Vector &pos, RouteType route )
{
	m_moveToState.SetGoalPosition( pos );
	m_moveToState.SetRouteType( route );
	SetState( &m_moveToState );
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::PlantBomb( void ) { SetState( &m_plantBombState ); }
void CFFBot::FetchBomb( void ) { SetState( &m_fetchBombState ); }
void CFFBot::DefuseBomb( void ) { SetState( &m_defuseBombState ); }
void CFFBot::InvestigateNoise( void ) { SetState( &m_investigateNoiseState ); }
void CFFBot::Buy( void ) { SetState( &m_buyState ); }

//--------------------------------------------------------------------------------------------------------------
// TODO: This logic is highly CS-specific (teams, enemy spawn names, battlefront calc)
bool CFFBot::MoveToInitialEncounter( void )
{
	// int myTeam = GetTeamNumber();
	// int enemyTeam = OtherTeam( myTeam );
	// if (!TheFFBots()) return false;
	// CBaseEntity *enemySpawn = TheFFBots()->GetRandomSpawn( enemyTeam );
	// if (enemySpawn == NULL) { PrintIfWatched(this, "MoveToInitialEncounter: No enemy spawn points?\n" ); return false; } // Updated PrintIfWatched
	// CNavPath path;
	// PathCost cost( this, FASTEST_ROUTE );
	// if ( !NavAreaBuildPath( m_lastKnownArea, TheNavMesh->GetNearestNavArea(enemySpawn->GetAbsOrigin()), &(enemySpawn->GetAbsOrigin()), cost, NULL, TheNavMesh->GetMaxPathNodes(), true, &path ) )
	// { PrintIfWatched(this, "MoveToInitialEncounter: Pathfind failed.\n" ); return false; } // Updated PrintIfWatched
	// if (!path.IsValid()) { PrintIfWatched(this, "MoveToInitialEncounter: Pathfind failed (path not valid).\n" ); return false; } // Updated PrintIfWatched
	// int i;
	// for( i=0; i<path.GetSegmentCount(); ++i ) { /* ... */ }
	// if (i == path.GetSegmentCount()) { PrintIfWatched(this, "MoveToInitialEncounter: Can't find battlefront!\n" ); return false; } // Updated PrintIfWatched
	// const CNavPathSegment *battleSegment = path.GetSegment(i);
	// if (!battleSegment || !battleSegment->area) return false;
	// SetInitialEncounterArea( battleSegment->area );
	// const float maxRange = 1500.0f;
	// const HidingSpot *spot = FindInitialEncounterSpot( this, battleSegment->area->GetCenter(), battleSegment->area->GetEarliestOccupyTime( enemyTeam ), maxRange, IsSniper() );
	// if (spot == NULL) { PrintIfWatched(this, "MoveToInitialEncounter: Can't find a hiding spot\n" ); return false; } // Updated PrintIfWatched
	// if (!spot->GetArea()) return false;
	// float timeToWait = battleSegment->area->GetEarliestOccupyTime( enemyTeam ) - spot->GetArea()->GetEarliestOccupyTime( myTeam );
	// float minWaitTime = 4.0f * (GetProfile() ? GetProfile()->GetAggression() : 0.5f) + 3.0f;
	// if (timeToWait < minWaitTime) timeToWait = minWaitTime;
	// Hide( spot->GetPosition(), timeToWait );
	return false; // Placeholder, original logic heavily CS-dependent
}
