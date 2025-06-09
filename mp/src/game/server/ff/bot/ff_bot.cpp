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
// #include "ff_bot_chatter.h" // Removed CS-specific chatter
#include "ff_gamestate.h"
#include "../ff_player.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "../../shared/ff/ff_gamerules.h"

// Shared bot headers changed to local
#include "bot_profile.h"
#include "bot_constants.h"
#include "bot_util.h"

// State headers - CFFBot instantiates these
#include "states/ff_bot_state_idle.h"
#include "states/ff_bot_state_hunt.h"
#include "states/ff_bot_state_attack.h"
#include "states/ff_bot_state_investigate_noise.h"
#include "states/ff_bot_state_buy.h"
#include "states/ff_bot_state_move_to.h"
// Removed includes for deleted CS bomb states
// #include "states/ff_bot_state_fetch_bomb.h"
// #include "states/ff_bot_state_plant_bomb.h"
// #include "states/ff_bot_state_defuse_bomb.h"
#include "states/ff_bot_state_hide.h"
// #include "states/ff_bot_state_escape_from_bomb.h"
#include "states/ff_bot_state_follow.h"
#include "states/ff_bot_state_use_entity.h"
#include "states/ff_bot_state_open_door.h"

// Other specific includes
#include "cs_simple_hostage.h" // TODO: Check if FF equivalent exists or if shared
#include "func_breakablesurf.h" // Assumed engine/shared
#include "obstacle_pushaway.h" // Assumed engine/shared
#include "nav_mesh.h" // For TheNavMesh, CNavArea
#include "nav_hiding_spot.h" // For HidingSpot
#include "nav_pathfind.h" // For ShortestPathCost related classes (if used directly)
// #include "../../shared/ff/weapons/ff_weapon_parse.h" // Potentially needed if CFFWeaponInfo is used directly - already commented out

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( ff_bot, CFFBot );

BEGIN_DATADESC( CFFBot )

END_DATADESC()


//--------------------------------------------------------------------------------------------------------------
/**
 * Return the number of bots following the given player
 */
int GetBotFollowCount( CFFPlayer *leader )
{
	int count = 0;

	for( int i=1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *entity = UTIL_PlayerByIndex( i );

		if (entity == NULL)
			continue;

		CBasePlayer *player = static_cast<CBasePlayer *>( entity );

		if (!player->IsBot())
			continue;

 		if (!player->IsAlive())
 			continue;

		CFFBot *bot = dynamic_cast<CFFBot *>( player );
		if (bot && bot->GetFollowLeader() == leader)
			++count;
	}

	return count;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Change movement speed to walking
 */
void CFFBot::Walk( void )
{
	if (m_mustRunTimer.IsElapsed())
	{
		BaseClass::Walk();
	}
	else
	{
		// must run
		Run();
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if jump was started.
 * This is extended from the base jump to disallow jumping when in a crouch area.
 */
bool CFFBot::Jump( bool mustJump )
{
	// prevent jumping if we're crouched, unless we're in a crouchjump area - jump wins
	bool inCrouchJumpArea = (m_lastKnownArea && 
		(m_lastKnownArea->GetAttributes() & NAV_MESH_CROUCH) &&
		(m_lastKnownArea->GetAttributes() & NAV_MESH_JUMP));

	if ( !IsUsingLadder() && IsDucked() && !inCrouchJumpArea )
	{
		return false;
	}

	return BaseClass::Jump( mustJump );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when injured by something
 * NOTE: We dont want to directly call Attack() here, or the bots will have super-human reaction times when injured
 */
int CFFBot::OnTakeDamage( const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetInflictor();

	// getting hurt makes us alert
	BecomeAlert();
	StopWaiting();

	// if we were attacked by a teammate, rebuke
	if (attacker && attacker->IsPlayer()) // Added null check for attacker
	{
		CFFPlayer *player = static_cast<CFFPlayer *>( attacker );
		
		if (player && InSameTeam( player ) && !player->IsBot()) // Added null check for player
		{
			// GetChatter()->FriendlyFire(); // Removed CS-specific chatter
			// TODO_FF: Consider alternative feedback for friendly fire if needed
		}
	}

	if (attacker && attacker->IsPlayer() && IsEnemy( attacker )) // Added null check for attacker
	{
		// Track previous attacker so we don't try to panic multiple times for a shotgun blast
		CFFPlayer *lastAttacker = m_attacker;
		float lastAttackedTimestamp = m_attackedTimestamp;

		// keep track of our last attacker
		m_attacker = reinterpret_cast<CFFPlayer *>( attacker );
		m_attackedTimestamp = gpGlobals->curtime;

		// no longer safe
		AdjustSafeTime();

		if ( !IsSurprised() && (m_attacker != lastAttacker || m_attackedTimestamp != lastAttackedTimestamp) )
		{
			CFFPlayer *enemy = static_cast<CFFPlayer *>( attacker );

			// being hurt by an enemy we can't see causes panic
			if (enemy && !IsVisible( enemy, CHECK_FOV )) // Added null check for enemy
			{
				// if not attacking anything, look around to try to find attacker
				if (!IsAttacking())
				{
					Panic();
				}
				else	// we are attacking
				{
					if (!IsEnemyVisible())
					{
						// can't see our current enemy, panic to acquire new attacker
						Panic();
					}
				}
			}
		}
	}

	// extend
	return BaseClass::OnTakeDamage( info );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when killed
 */
void CFFBot::Event_Killed( const CTakeDamageInfo &info )
{ 
//	PrintIfWatched( "Killed( attacker = %s )\n", STRING(pevAttacker->netname) );

	// GetChatter()->OnDeath(); // Removed CS-specific chatter

	// increase the danger where we died
	const float deathDanger = 1.0f;
	const float deathDangerRadius = 500.0f;
	if (TheNavMesh && m_lastKnownArea) // Ensure TheNavMesh and m_lastKnownArea are valid
		TheNavMesh->IncreaseDangerNearby( GetTeamNumber(), deathDanger, m_lastKnownArea, GetAbsOrigin(), deathDangerRadius );

	// end voice feedback
	m_voiceEndTimestamp = 0.0f;

	// extend
	BaseClass::Event_Killed( info );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if line segment intersects rectagular volume
 */
#define HI_X	0x01
#define LO_X 0x02
#define HI_Y	0x04
#define LO_Y 0x08
#define HI_Z	0x10
#define LO_Z 0x20

inline bool IsIntersectingBox( const Vector& start, const Vector& end, const Vector& boxMin, const Vector& boxMax )
{
	unsigned char startFlags = 0;
	unsigned char endFlags = 0;

	// classify start point
	if (start.x < boxMin.x)
		startFlags |= LO_X;
	if (start.x > boxMax.x)
		startFlags |= HI_X;

	if (start.y < boxMin.y)
		startFlags |= LO_Y;
	if (start.y > boxMax.y)
		startFlags |= HI_Y;

	if (start.z < boxMin.z)
		startFlags |= LO_Z;
	if (start.z > boxMax.z)
		startFlags |= HI_Z;

	// classify end point
	if (end.x < boxMin.x)
		endFlags |= LO_X;
	if (end.x > boxMax.x)
		endFlags |= HI_X;

	if (end.y < boxMin.y)
		endFlags |= LO_Y;
	if (end.y > boxMax.y)
		endFlags |= HI_Y;

	if (end.z < boxMin.z)
		endFlags |= LO_Z;
	if (end.z > boxMax.z)
		endFlags |= HI_Z;

	// trivial reject
	if (startFlags & endFlags)
		return false;

	/// @todo Do exact line/box intersection check

	return true;
}


extern void UTIL_DrawBox( Extent *extent, int lifetime, int red, int green, int blue );

//--------------------------------------------------------------------------------------------------------------
/**
 * When bot is touched by another entity.
 */
void CFFBot::Touch( CBaseEntity *other )
{
	// EXTEND
	BaseClass::Touch( other );

	if (!other) return; // Null check

	// if we have touched a higher-priority player, make way
	/// @todo Need to account for reaction time, etc.
	if (other->IsPlayer())
	{
		// if we are defusing a bomb, don't move
		if (IsDefusingBomb()) // TODO: CS-specific
			return;

		// if we are on a ladder, don't move
		if (IsUsingLadder())
			return;

		CFFPlayer *player = static_cast<CFFPlayer *>( other );
		if (!player || !TheFFBots()) return; // Null checks

		// get priority of other player
		unsigned int otherPri = TheFFBots()->GetPlayerPriority( player );

		// get our priority
		unsigned int myPri = TheFFBots()->GetPlayerPriority( this );

		// if our priority is better, don't budge
		if (myPri < otherPri)
			return;

		// they are higher priority - make way, unless we're already making way for someone more important
		if (m_avoid != NULL)
		{
			CBasePlayer* avoidPlayer = static_cast<CBasePlayer *>( static_cast<CBaseEntity *>( m_avoid.Get() ) ); // Use .Get()
			if (avoidPlayer)
			{
				unsigned int avoidPri = TheFFBots()->GetPlayerPriority( avoidPlayer );
				if (avoidPri < otherPri)
				{
					// ignore 'other' because we're already avoiding someone better
					return;
				}
			}
		}

		m_avoid = other;
		m_avoidTimestamp = gpGlobals->curtime;
	}

	// Check for breakables we're actually touching
	// If we're not stuck or crouched, we don't care
	if ( !m_isStuck && !IsCrouching() && !IsOnLadder() )
		return;

	// See if it's breakable
	if ( IsBreakableEntity( other ) )
	{
		// it's breakable - try to shoot it.
		SetLookAt( "Breakable", other->WorldSpaceCenter(), PRIORITY_HIGH, 0.1f, false, 5.0f, true );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are busy doing something important
 */
bool CFFBot::IsBusy( void ) const
{
	// TODO: Update TaskType enums for FF (PLANT_BOMB, RESCUE_HOSTAGES)
	if (IsAttacking() || 
		IsBuying() ||
		IsDefusingBomb() || // CS-specific
		GetTask() == PLANT_BOMB ||
		GetTask() == RESCUE_HOSTAGES ||
		IsSniping()) // CS-specific
	{
		return true;
	}

	return false;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::BotDeathThink( void )
{
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Try to join the given team
 */
void CFFBot::TryToJoinTeam( int team )
{
	m_desiredTeam = team;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Assign given player as our current enemy to attack
 */
void CFFBot::SetBotEnemy( CFFPlayer *enemy )
{
	if (m_enemy != enemy)
	{
		m_enemy = enemy; 
		m_currentEnemyAcquireTimestamp = gpGlobals->curtime;

		PrintIfWatched(this, "SetBotEnemy: %s\n", (enemy) ? enemy->GetPlayerName() : "(NULL)" ); // Updated PrintIfWatched
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * If we are not on the navigation mesh (m_currentArea == NULL),
 * move towards last known area.
 * Return false if off mesh.
 */
bool CFFBot::StayOnNavMesh( void )
{
	if (m_currentArea == NULL)
	{
		// move back onto the area map
		CNavArea *goalArea = NULL;
		if (!m_currentArea && !m_lastKnownArea)
		{
			if (!TheNavMesh) return false;
			goalArea = TheNavMesh->GetNearestNavArea( GetCentroid( this ) );
			PrintIfWatched(this, "Started off the nav mesh - moving to closest nav area...\n" ); // Updated PrintIfWatched
		}
		else
		{
			goalArea = m_lastKnownArea;
			PrintIfWatched(this, "Getting out of NULL area...\n" ); // Updated PrintIfWatched
		}

		if (goalArea)
		{
			Vector pos;
			goalArea->GetClosestPointOnArea( GetCentroid( this ), &pos );
			Vector to = pos - GetCentroid( this );
			to.NormalizeInPlace();
			const float stepInDist = 5.0f;
			pos = pos + (stepInDist * to);
			MoveTowardsPosition( pos );
		}

		if (m_isStuck) Wiggle();
		return false;
	}
	return true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we will do scenario-related tasks
 */
bool CFFBot::IsDoingScenario( void ) const
{
	if (cv_bot_defer_to_human.GetBool())
	{
		// TODO: Update ALIVE enum for FF if different
		if (UTIL_HumansOnTeam( GetTeamNumber(), ALIVE ))
			return false;
	}
	return true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we noticed the bomb on the ground or on the radar (for T's only)
 */
// TODO: Bomb logic for FF
// TODO_FF: CS Bomb-specific methods below are removed.
// bool CFFBot::NoticeLooseBomb( void ) const { ... }
// bool CFFBot::CanSeeLooseBomb( void ) const { ... }
// bool CFFBot::CanSeePlantedBomb( void ) const { ... }


//--------------------------------------------------------------------------------------------------------------
/**
 * Transition to the CapturePointState to capture/defend the given control point.
 */
void CFFBot::CapturePoint( CBaseEntity *cpEntity )
{
	if (!cpEntity)
	{
		Warning("CFFBot::CapturePoint: cpEntity is NULL. Going Idle.\n");
		Idle();
		return;
	}
	PrintIfWatched( "Player %s capturing/defending point %s\n", GetPlayerName(), STRING(cpEntity->GetEntityName()) );
	SetTaskEntity( cpEntity );
	SetState( &m_capturePointState );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return last enemy that hurt us
 */
CFFPlayer *CFFBot::GetAttacker( void ) const
{
	if (m_attacker.IsValid() && m_attacker->IsAlive()) // Use .IsValid() for EHANDLE
		return m_attacker.Get(); // Use .Get() for EHANDLE
	return NULL;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return the bot's currently equipped weapon
 */
CFFWeaponBase *CFFBot::GetActiveFFWeapon() const
{
    CBaseCombatWeapon *pCombatWeapon = GetActiveWeapon(); // Inherited from CBasePlayer
    return dynamic_cast<CFFWeaponBase *>(pCombatWeapon);
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Immediately jump off of our ladder, if we're on one
 */
void CFFBot::GetOffLadder( void )
{
	if (IsUsingLadder())
	{
		Jump( true ); // Was MUST_JUMP, assuming true is equivalent
		DestroyPath();
	}
}



//--------------------------------------------------------------------------------------------------------------
/**
 * Return time when given spot was last checked
 */
float CFFBot::GetHidingSpotCheckTimestamp( HidingSpot *spot ) const
{
	if (!spot) return -999999.9f; // Null check
	for( int i=0; i<m_checkedHidingSpotCount; ++i )
		if (m_checkedHidingSpot[i].spot && m_checkedHidingSpot[i].spot->GetID() == spot->GetID())
			return m_checkedHidingSpot[i].timestamp;
	return -999999.9f;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Set the timestamp of the given spot to now.
 */
void CFFBot::SetHidingSpotCheckTimestamp( HidingSpot *spot )
{
	if (!spot) return;
	int leastRecent = 0;
	float leastRecentTime = gpGlobals->curtime + 1.0f;
	for( int i=0; i<m_checkedHidingSpotCount; ++i )
	{
		if (m_checkedHidingSpot[i].spot && m_checkedHidingSpot[i].spot->GetID() == spot->GetID())
		{
			m_checkedHidingSpot[i].timestamp = gpGlobals->curtime;
			return;
		}
		if (m_checkedHidingSpot[i].timestamp < leastRecentTime)
		{
			leastRecentTime = m_checkedHidingSpot[i].timestamp;
			leastRecent = i;
		}
	}
	if (m_checkedHidingSpotCount < MAX_CHECKED_SPOTS_FF)
	{
		m_checkedHidingSpot[ m_checkedHidingSpotCount ].spot = spot;
		m_checkedHidingSpot[ m_checkedHidingSpotCount ].timestamp = gpGlobals->curtime;
		++m_checkedHidingSpotCount;
	}
	else if (m_checkedHidingSpotCount > 0)
	{
		m_checkedHidingSpot[ leastRecent ].spot = spot;
		m_checkedHidingSpot[ leastRecent ].timestamp = gpGlobals->curtime;
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Periodic check of hostage count in case we lost some
 */
// TODO_FF: Hostage logic is CS-specific. Removed.
void CFFBot::UpdateHostageEscortCount( void )
{
	// CS Hostage logic removed
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are outnumbered by enemies
 */
bool CFFBot::IsOutnumbered( void ) const
{
	return (GetNearbyFriendCount() < GetNearbyEnemyCount()-1) ? true : false;		
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return number of enemies we are outnumbered by
 */
int CFFBot::OutnumberedCount( void ) const
{
	if (IsOutnumbered())
		return (GetNearbyEnemyCount()-1) - GetNearbyFriendCount();
	return 0;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return the closest "important" enemy for the given scenario
 */
CFFPlayer *CFFBot::GetImportantEnemy( bool checkVisibility ) const
{
	if (!TheFFBots()) return NULL;
	CFFPlayer *nearEnemy = NULL;
	float nearDist = FLT_MAX;

	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *entity = UTIL_PlayerByIndex( i );
		if (!entity || !entity->IsPlayer()) continue;
		CFFPlayer *player = static_cast<CFFPlayer *>( entity );
		if (!player->IsAlive() || InSameTeam( player ) || !TheFFBots()->IsImportantPlayer( player )) continue;

		float distSq = (GetAbsOrigin() - player->GetAbsOrigin()).LengthSqr();
		if (distSq < nearDist)
		{
			if (checkVisibility && !IsVisible( player, true )) continue;
			nearEnemy = player;
			nearDist = distSq;
		}
	}
	return nearEnemy;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Sets our current disposition
 */
void CFFBot::SetDisposition( DispositionType disposition )
{ 
	m_disposition = disposition;
	if (m_disposition != IGNORE_ENEMIES) m_ignoreEnemiesTimer.Invalidate();
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return our current disposition
 */
CFFBot::DispositionType CFFBot::GetDisposition( void ) const
{
	if (!m_ignoreEnemiesTimer.IsElapsed()) return IGNORE_ENEMIES;
	return m_disposition;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Ignore enemies for a short duration
 */
void CFFBot::IgnoreEnemies( float duration )
{
	m_ignoreEnemiesTimer.Start( duration );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Increase morale one step
 */
void CFFBot::IncreaseMorale( void )
{
	if (m_morale < EXCELLENT) m_morale = static_cast<MoraleType>( m_morale + 1 );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Decrease morale one step
 */
void CFFBot::DecreaseMorale( void )
{
	if (m_morale > TERRIBLE) m_morale = static_cast<MoraleType>( m_morale - 1 );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are acting like a rogue
 */
bool CFFBot::IsRogue( void ) const
{ 
	if (!TheFFBots() || !TheFFBots()->AllowRogues()) return false;
	if (m_rogueTimer.IsElapsed())
	{
		m_rogueTimer.Start( RandomFloat( 10.0f, 30.0f ) );
		if (GetProfile()) m_isRogue = (RandomFloat( 0, 100 ) < (100.0f * (1.0f - GetProfile()->GetTeamwork())));
		else m_isRogue = false;
	}
	return m_isRogue; 
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are in a hurry 
 */
bool CFFBot::IsHurrying( void ) const
{
	if (!m_hurryTimer.IsElapsed()) return true;
	if (!TheFFBots() || !GetGameState()) return false;
	// TODO: Update for FF Scenarios and Teams
	if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_DEFUSE_BOMB && TheFFBots()->IsBombPlanted()) return true;
	if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_RESCUE_HOSTAGES && GetTeamNumber() == TEAM_TERRORIST && GetGameState()->AreAllHostagesBeingRescued()) return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if it is the early, "safe", part of the round
 */
bool CFFBot::IsSafe( void ) const
{
	if (!TheFFBots()) return true;
	if (TheFFBots()->GetElapsedRoundTime() < m_safeTime) return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if it is well past the early, "safe", part of the round
 */
bool CFFBot::IsWellPastSafe( void ) const
{
	if (!TheFFBots()) return false;
	if (TheFFBots()->GetElapsedRoundTime() > 2.0f * m_safeTime) return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we were in the safe time last update, but not now
 */
bool CFFBot::IsEndOfSafeTime( void ) const
{
	return m_wasSafe && !IsSafe();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return the amount of "safe time" we have left
 */
float CFFBot::GetSafeTimeRemaining( void ) const
{
	if (!TheFFBots()) return m_safeTime;
	return m_safeTime - TheFFBots()->GetElapsedRoundTime();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Called when enemy seen to adjust safe time for this round
 */
void CFFBot::AdjustSafeTime( void )
{
	if (!TheFFBots()) return;
	if (TheFFBots()->GetElapsedRoundTime() < m_safeTime)
	{
		m_safeTime = TheFFBots()->GetElapsedRoundTime() - 2.0f;
		if (m_safeTime < 0) m_safeTime = 0;
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we haven't seen an enemy for "a long time"
 */
bool CFFBot::HasNotSeenEnemyForLongTime( void ) const
{
	const float longTime = 30.0f;
	return (GetTimeSinceLastSawEnemy() > longTime);
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Pick a random zone and hide near it
 */
bool CFFBot::GuardRandomZone( float range )
{
	if (!TheFFBots()) return false;
	const CFFBotManager::Zone *zone = TheFFBots()->GetRandomZone();
	if (zone)
	{
		CNavArea *areaToGuard = TheFFBots()->GetRandomAreaInZone( zone );
		if (areaToGuard)
		{
			Hide( areaToGuard, -1.0f, range );
			return true;
		}
	}
	return false;
}



//--------------------------------------------------------------------------------------------------------------
// CollectRetreatSpotsFunctor and FindNearbyRetreatSpot are defined in ff_bot.cpp
// (They were static in CSBot, now global or static within ff_bot.cpp if not moved to bot_util.cpp)

//--------------------------------------------------------------------------------------------------------------
// FarthestHostage functor and GetRangeToFarthestEscortedHostage are CS-specific (hostages)
// TODO: Remove or adapt for FF.

//--------------------------------------------------------------------------------------------------------------
/**
 * Return string describing current task
 */
const char *CFFBot::GetTaskName( void ) const
{
	// This static array should be updated with FF TaskType names from bot_constants.h
	static const char *name[] = { "SEEK_AND_DESTROY", /* ... other task names ... */ "NUM_TASKS_INVALID" };
	if (GetTask() < 0 || GetTask() >= NUM_BOT_TASKS) return "INVALID_TASK"; // Use NUM_BOT_TASKS from bot_constants.h
	return name[ (int)GetTask() ];
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return string describing current disposition
 */
const char *CFFBot::GetDispositionName( void ) const
{
	static const char *name[] = { "ENGAGE_AND_INVESTIGATE", "OPPORTUNITY_FIRE", "SELF_DEFENSE", "IGNORE_ENEMIES", "NUM_DISPOSITIONS_INVALID" };
	if (GetDisposition() < 0 || GetDisposition() >= NUM_DISPOSITIONS) return "INVALID_DISPOSITION"; // NUM_DISPOSITIONS from bot_constants.h
	return name[ (int)GetDisposition() ];
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return string describing current morale
 */
const char *CFFBot::GetMoraleName( void ) const
{
	static const char *name[] = { "TERRIBLE", "BAD", "NEGATIVE", "NEUTRAL", "POSITIVE", "GOOD", "EXCELLENT", "NUM_MORALE_TYPES_INVALID" };
	if (GetMorale() + 3 < 0 || GetMorale() + 3 >= NUM_MORALE_TYPES) return "INVALID_MORALE"; // NUM_MORALE_TYPES from bot_constants.h
	return name[ (int)GetMorale() + 3 ];
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Fill in a CUserCmd with our data
 */
void CFFBot::BuildUserCmd( CUserCmd& cmd, const QAngle& viewangles, float forwardmove, float sidemove, float upmove, int buttons, byte impulse )
{
	Q_memset( &cmd, 0, sizeof( cmd ) );
	if ( !RunMimicCommand( cmd ) )
	{
		if ( m_Local.m_bDucked || m_Local.m_bDucking )
		{
			buttons &= ~IN_SPEED; // IN_SPEED needs to be defined (e.g. in in_buttons.h)
		}
		cmd.command_number = gpGlobals->tickcount;
		cmd.forwardmove = forwardmove;
		cmd.sidemove = sidemove;
		cmd.upmove = upmove;
		cmd.buttons = buttons;
		cmd.impulse = impulse;
		VectorCopy( viewangles, cmd.viewangles );
		if (random) cmd.random_seed = random->RandomInt( 0, 0x7fffffff );
		else cmd.random_seed = 0;
	}
}

//--------------------------------------------------------------------------------------------------------------
// Definitions for CFFBot constructor, destructor, ResetValues, SetState, state transition methods (Idle, Attack, etc.),
// Spawn, Upkeep, Update methods are in ff_bot.cpp.
// State logic (IdleState::OnEnter etc.) is in the state files (e.g. states/ff_bot_state_idle.cpp).
// State transition wrappers (CFFBot::Idle(), CFFBot::Attack()) are in ff_bot_statemachine.cpp.
// This ensures a cleaner separation of concerns.
