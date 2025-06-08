//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

//
// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003
//
// NOTE: The FF Bot code uses Doxygen-style comments. If you run Doxygen over this code, it will
// auto-generate documentation.  Visit www.doxygen.org to download the system for free.
//

#ifndef _FF_BOT_H_ // Changed from _CS_BOT_H_
#define _FF_BOT_H_ // Changed from _CS_BOT_H_

#include "bot/bot.h"
#include "bot/ff_bot_manager.h" // Changed from cs_bot_manager.h
#include "bot/ff_bot_chatter.h" // Changed from cs_bot_chatter.h
#include "ff_gamestate.h"  // Changed from cs_gamestate.h
#include "ff_player.h"     // Changed from cs_player.h
#include "ff_weapon_base.h" // FF_TODO_WEAPON_STATS: Added for CFFWeaponBase and FFWeaponID
#include "ff_nav_pathfind.h" // Changed from cs_nav_pathfind.h (assuming exists)
#include "ff_nav_area.h"     // Changed from cs_nav_area.h (assuming CNavArea or a CFFNavArea will be used)

class CBaseDoor;
class CBasePropDoor;
class CFFBot; // Changed from CCSBot
class HealTeammateState; // Forward declaration
class BuildSentryState; // Forward declaration
class BuildDispenserState; // Forward declaration
class RepairBuildableState; // Forward declaration
class FindResourcesState; // Forward declaration
class GuardSentryState; // Forward declaration
class InfiltrateState; // Forward declaration
class CPushAwayEnumerator;

//--------------------------------------------------------------------------------------------------------------
/**
 * For use with player->m_rgpPlayerItems[]
 */
enum FFInventorySlotType // Adapted from InventorySlotType
{
	FF_PRIMARY_WEAPON_SLOT = 1, // Numbers might need adjustment based on FF
	FF_SECONDARY_WEAPON_SLOT, // Renamed from PISTOL_SLOT
	FF_MELEE_WEAPON_SLOT,    // Renamed from KNIFE_SLOT
	FF_GRENADE_SLOT,
	FF_BUILDER_SLOT,         // Example: For Engineer buildables
	FF_SPECIAL_SLOT          // Example: For Spy disguise kit, Medic special
	// C4_SLOT removed
};


//--------------------------------------------------------------------------------------------------------------
/**
 * The definition of a bot's behavior state.  One or more finite state machines 
 * using these states implement a bot's behaviors.
 */
class BotState
{
public:
	virtual void OnEnter( CFFBot *bot ) { }				///< when state is entered // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ) { }			///< state behavior // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ) { }				///< when state exited // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const = 0;		///< return state name
};


//--------------------------------------------------------------------------------------------------------------
/**
 * The state is invoked when a bot has nothing to do, or has finished what it was doing.
 * A bot never stays in this state - it is the main action selection mechanism.
 */
class IdleState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "Idle"; }

private:
	CountdownTimer m_medicScanTimer; // For medics to periodically scan for heal targets
	CountdownTimer m_engineerSentryScanTimer; // For engineers to periodically consider building sentries
	CountdownTimer m_engineerDispenserScanTimer; // For engineers to periodically consider building dispensers
	CountdownTimer m_engineerRepairScanTimer; // For engineers to periodically scan for damaged buildables
	CountdownTimer m_engineerResourceScanTimer; // For engineers to periodically scan for resources
	CountdownTimer m_engineerGuardScanTimer; // For engineers to periodically consider guarding their sentry
	CountdownTimer m_spyInfiltrateScanTimer; // For spies to periodically consider infiltrating
	CNavArea *m_huntArea;           // Target area for hunting behavior (was used in ff_bot_idle.cpp but not declared here)
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is actively searching for an enemy.
 */
class HuntState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "Hunt"; }

	void ClearHuntArea( void )						{ m_huntArea = NULL; }

private:
	CNavArea *m_huntArea;										///< "far away" area we are moving to
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot has an enemy and is attempting to kill it
 */
class AttackState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "Attack"; }
	
	void SetCrouchAndHold( bool crouch )			{ m_crouchAndHold = crouch; }

protected:
	enum DodgeStateType
	{
		STEADY_ON,
		SLIDE_LEFT,
		SLIDE_RIGHT,
		JUMP,

		NUM_ATTACK_STATES
	};
	DodgeStateType m_dodgeState;
	float m_nextDodgeStateTimestamp;

	CountdownTimer m_repathTimer;
	float m_scopeTimestamp; // FF_TODO_WEAPON_STATS: May not be relevant for all FF weapons or may need adjustment

	// Evasion
	CountdownTimer m_evasiveActionTimer;
	Vector m_evadeToSpot;
	bool m_isEvading;

	bool m_haveSeenEnemy;										///< false if we haven't yet seen the enemy since we started this attack (told by a friend, etc)
	bool m_isEnemyHidden;										///< true we if we have lost line-of-sight to our enemy
	float m_reacquireTimestamp;									///< time when we can fire again, after losing enemy behind cover
	// float m_shieldToggleTimestamp;								///< time to toggle shield deploy state - FF no shield
	// bool m_shieldForceOpen;										///< if true, open up and shoot even if in danger - FF no shield

	float m_pinnedDownTimestamp;								///< time when we'll consider ourselves "pinned down" by the enemy

	bool m_crouchAndHold;
	bool m_didAmbushCheck;
	bool m_shouldDodge;
	bool m_firstDodge;

	bool m_isCoward;											///< if true, we'll retreat if outnumbered during this fight
	CountdownTimer m_retreatTimer;

	void StopAttacking( CFFBot *bot ); // Changed CCSBot to CFFBot
	void Dodge( CFFBot *bot );									///< do dodge behavior // Changed CCSBot to CFFBot
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot has heard an enemy noise and is moving to find out what it was.
 */
class InvestigateNoiseState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "InvestigateNoise"; }

private:
	void AttendCurrentNoise( CFFBot *bot );						///< move towards currently heard noise // Changed CCSBot to CFFBot
	Vector m_checkNoisePosition;								///< the position of the noise we're investigating
	CountdownTimer m_minTimer;									///< minimum time we will investigate our current noise
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is buying equipment at the start of a round.
 * FF will have a different economy/item system. This state might be heavily adapted or replaced.
 */
class BuyState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "Buy"; }

private:
	bool m_isInitialDelay;
	int m_prefRetries;											///< for retrying buying preferred weapon at current index
	int m_prefIndex;											///< where are we in our list of preferred weapons

	int m_retries;
	bool m_doneBuying;
	// FF_TODO_GAME_MECHANIC: FF Specific buy logic needed here
	// bool m_buyDefuseKit; // FF no bomb
	bool m_buyGrenade;
	// bool m_buyShield; // FF no shield
	bool m_buyPistol; // FF_TODO_WEAPON_STATS: FF weapon categories might differ
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is moving to a potentially far away position in the world.
 */
class MoveToState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "MoveTo"; }
	void SetGoalPosition( const Vector &pos )		{ m_goalPosition = pos; }
	void SetRouteType( RouteType route )			{ m_routeType = route; }

private:
	Vector m_goalPosition;										///< goal position of move
	RouteType m_routeType;										///< the kind of route to build
	bool m_radioedPlan;
	bool m_askedForCover;
};

// CS-Specific States removed (FetchBombState, PlantBombState, DefuseBombState, EscapeFromBombState)
// FF will need its own objective-based states (e.g., CaptureFlagState, DefendPointState, PushCartState)


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is hiding in a corner.
 * NOTE: This state also includes MOVING TO that hiding spot, which may be all the way
 * across the map!
 */
class HideState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const	{ return "Hide"; }

	void SetHidingSpot( const Vector &pos )		{ m_hidingSpot = pos; }
	const Vector &GetHidingSpot( void ) const	{ return m_hidingSpot; }

	void SetSearchArea( CNavArea *area )		{ m_searchFromArea = area; }
	void SetSearchRange( float range )			{ m_range = range; }
	void SetDuration( float time )				{ m_duration = time; }
	void SetHoldPosition( bool hold )			{ m_isHoldingPosition = hold; }

	bool IsAtSpot( void ) const					{ return m_isAtSpot; }

	float GetHideTime( void ) const
	{
		if (IsAtSpot())
		{
			return m_duration - m_hideTimer.GetRemainingTime();
		}

		return 0.0f;
	}

private:
	CNavArea *m_searchFromArea;
	float m_range;

	Vector m_hidingSpot;
	bool m_isLookingOutward;
	bool m_isAtSpot;
	float m_duration;
	CountdownTimer m_hideTimer;								///< how long to hide

	bool m_isHoldingPosition;
	float m_holdPositionTime;								///< how long to hold our position after we hear nearby enemy noise

	bool m_heardEnemy;										///< set to true when we first hear an enemy
	float m_firstHeardEnemyTime;							///< when we first heard the enemy

	int m_retry;											///< counter for retrying hiding spot

	Vector m_leaderAnchorPos;								///< the position of our follow leader when we decided to hide

	bool m_isPaused;										///< if true, we have paused in our retreat for a moment
	CountdownTimer m_pauseTimer;							///< for stoppping and starting our pauses while we retreat
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is following another player.
 */
class FollowState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "Follow"; }

	void SetLeader( CFFPlayer *player )				{ m_leader = player; } // Changed CCSPlayer to CFFPlayer

private:
	CHandle< CFFPlayer > m_leader;								///< the player we are following // Changed CCSPlayer to CFFPlayer
	Vector m_lastLeaderPos;										///< where the leader was when we computed our follow path
	bool m_isStopped;
	float m_stoppedTimestamp;

	enum LeaderMotionStateType
	{
		INVALID,
		STOPPED,
		WALKING,
		RUNNING
	};
	LeaderMotionStateType m_leaderMotionState;
	IntervalTimer m_leaderMotionStateTime;

	bool m_isSneaking;
	float m_lastSawLeaderTime;
	CountdownTimer m_repathInterval;

	IntervalTimer m_walkTime;
	bool m_isAtWalkSpeed;

	float m_waitTime;
	CountdownTimer m_idleTimer;

	void ComputeLeaderMotionState( float leaderSpeed );
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is actually using another entity (ie: facing towards it and pressing the use key)
 */
class UseEntityState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const			{ return "UseEntity"; }

	void SetEntity( CBaseEntity *entity )				{ m_entity = entity; }

private:
	EHANDLE m_entity;											///< the entity we will use
};


//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is opening a door
 */
class OpenDoorState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnUpdate( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual void OnExit( CFFBot *bot ); // Changed CCSBot to CFFBot
	virtual const char *GetName( void ) const		{ return "OpenDoor"; }

	void SetDoor( CBaseEntity *door );

	bool IsDone( void ) const						{ return m_isDone; }	///< return true if behavior is done

private:
	CHandle< CBaseDoor > m_funcDoor;									///< the func_door we are opening
	CHandle< CBasePropDoor > m_propDoor;								///< the prop_door we are opening
	bool m_isDone;
	CountdownTimer m_timeout;
};


//--------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------
/**
 * The Fortress Forever Bot // Changed
 */
class CFFBot : public CBot< CFFPlayer > // Changed CCSPlayer to CFFPlayer
{
public:
	DECLARE_CLASS( CFFBot, CBot< CFFPlayer > ); // Changed CCSBot to CFFBot
	DECLARE_DATADESC();

	CFFBot( void );												///< constructor initializes all values to zero // Changed CCSBot to CFFBot
	virtual ~CFFBot(); // Changed CCSBot to CFFBot
	virtual bool Initialize( const BotProfile *profile, int team );		///< (EXTEND) prepare bot for action

	virtual void Spawn( void );									///< (EXTEND) spawn the bot into the game
	virtual void Touch( CBaseEntity *other );					///< (EXTEND) when touched by another entity

	virtual void BotThink( void );								///< (EXTEND) replacement for Upkeep/Update, called by CBasePlayerBot::BotThink
	// virtual void Upkeep( void );								///< lightweight maintenance, invoked frequently - Now in CBasePlayerBot
	// virtual void Update( void );								///< heavyweight algorithms, invoked less often - Now in CBasePlayerBot
	virtual void BuildUserCmd( CUserCmd& cmd, const QAngle& viewangles, float forwardmove, float sidemove, float upmove, int buttons, byte impulse );
	virtual float GetMaxSpeed( void ) const;					///< (OVERRIDE) Gets the max speed of the bot (Scout has higher speed)
	virtual float GetMoveSpeed( void );							///< returns current movement speed (for walk/run)

	virtual void Walk( void );
	virtual bool Jump( bool mustJump = false );					///< returns true if jump was started

	//- behavior properties ------------------------------------------------------------------------------------------
	float GetCombatRange( void ) const;
	bool IsRogue( void ) const;									///< return true if we dont listen to teammates or pursue scenario goals
	void SetRogue( bool rogue );
	bool IsHurrying( void ) const;								///< return true if we are in a hurry 
	void Hurry( float duration );								///< force bot to hurry
	bool IsSafe( void ) const;									///< return true if we are in a safe region
	bool IsWellPastSafe( void ) const;							///< return true if it is well past the early, "safe", part of the round
	bool IsEndOfSafeTime( void ) const;							///< return true if we were in the safe time last update, but not now
	float GetSafeTimeRemaining( void ) const;					///< return the amount of "safe time" we have left
	float GetSafeTime( void ) const;							///< return what we think the total "safe time" for this map is
	virtual void Blind( float holdTime, float fadeTime, float startingAlpha = 255 );	// player blinded by a flashbang - FF_TODO_GAME_MECHANIC: FF might have different status effects
	bool IsUnhealthy( void ) const;								///< returns true if bot is low on health
	
	bool IsAlert( void ) const;									///< return true if bot is in heightened "alert" mode
	void BecomeAlert( void );									///< bot becomes "alert" for immediately nearby enemies

	bool IsSneaking( void ) const;								///< return true if bot is sneaking
	void Sneak( float duration );								///< sneak for given duration

	//- behaviors ---------------------------------------------------------------------------------------------------
	void Idle( void );

	void Hide( CNavArea *searchFromArea = NULL, float duration = -1.0f, float hideRange = 750.0f, bool holdPosition = false );
	#define USE_NEAREST true
	bool TryToHide( CNavArea *searchFromArea = NULL, float duration = -1.0f, float hideRange = 750.0f, bool holdPosition = false, bool useNearest = false );	///< try to hide nearby, return false if cannot
	void Hide( const Vector &hidingSpot, float duration = -1.0f, bool holdPosition = false );	///< move to the given hiding place
	bool IsHiding( void ) const;								///< returns true if bot is currently hiding
	bool IsAtHidingSpot( void ) const;							///< return true if we are hiding and at our hiding spot
	float GetHidingTime( void ) const;							///< return number of seconds we have been at our current hiding spot

	bool MoveToInitialEncounter( void );						///< move to a hiding spot and wait for initial encounter with enemy team (return false if no spots are available)

	bool TryToRetreat( float maxRange = 1000.0f, float duration = -1.0f );	///< retreat to a nearby hiding spot, away from enemies

	void Hunt( void );
	bool IsHunting( void ) const;								///< returns true if bot is currently hunting

	void Attack( CFFPlayer *victim ); // Changed CCSPlayer to CFFPlayer
	void FireWeaponAtEnemy( void );								///< fire our active weapon towards our current enemy
	void StopAttacking( void );
	bool IsAttacking( void ) const;								///< returns true if bot is currently engaging a target

	void MoveTo( const Vector &pos, RouteType route = SAFEST_ROUTE );	///< move to potentially distant position
	bool IsMovingTo( void ) const;								///< return true if we are in the MoveTo state

	// FF_TODO_AI_BEHAVIOR: FF Objective states (examples, to be defined)
	// void CaptureFlag( void );
	// void DefendPoint( void );
	// void PushCart( void );
	// bool IsCapturingFlag( void ) const;
	// bool IsDefendingPoint( void ) const;
	// bool IsPushingCart( void ) const;


	void UseEntity( CBaseEntity *entity );						///< use the entity

	void OpenDoor( CBaseEntity *door );							///< open the door (assumes we are right in front of it)
	bool IsOpeningDoor( void ) const;							///< return true if we are in the process of opening a door

	void Buy( void );											///< enter the buy state - FF_TODO_GAME_MECHANIC: FF buy system
	bool IsBuying( void ) const;

	void Panic( void );											///< look around in panic
	bool IsPanicking( void ) const;								///< return true if bot is panicked
	void StopPanicking( void );									///< end our panic
	void UpdatePanicLookAround( void );							///< do panic behavior

	void TryToJoinTeam( int team );								///< try to join the given team

	void Follow( CFFPlayer *player );							///< begin following given Player // Changed CCSPlayer to CFFPlayer
	void ContinueFollowing( void );								///< continue following our leader after finishing what we were doing
	void StopFollowing( void );									///< stop following
	// IsFollowing() and GetFollowLeader() inherited from CBasePlayerBot

	float GetFollowDuration( void ) const;						///< return how long we've been following our leader
	bool CanAutoFollow( void ) const;							///< return true if we can auto-follow

	bool IsNotMoving( float minDuration = 0.0f ) const;			///< return true if we are currently standing still and have been for minDuration

	void AimAtEnemy( void );									///< point our weapon towards our enemy
	void StopAiming( void );									///< stop aiming at enemy
	bool IsAimingAtEnemy( void ) const;							///< returns true if we are trying to aim at an enemy

	float GetStateTimestamp( void ) const;						///< get time current state was entered

	bool IsDoingScenario( void ) const;							///< return true if we will do scenario-related tasks - FF_TODO_GAME_MECHANIC: FF specific scenarios

	//- scenario / gamestate -----------------------------------------------------------------------------------------
	CFFBotGameState *GetGameState( void );							///< return an interface to this bot's gamestate // Changed
	const CFFBotGameState *GetGameState( void ) const;				///< return an interface to this bot's gamestate // Changed

	// bool IsAtBombsite( void );									///< return true if we are in a bomb planting zone - FF no bomb
	bool GuardRandomZone( float range = 500.0f );				///< pick a random zone and hide near it - FF_TODO_AI_BEHAVIOR: FF zones

	bool IsBusy( void ) const;									///< return true if we are busy doing something important

	//- high-level tasks ---------------------------------------------------------------------------------------------
	// FF_TODO_AI_BEHAVIOR: FF TaskTypes will be different
	enum TaskType
	{
		SEEK_AND_DESTROY,
		// PLANT_BOMB, // CS
		// FIND_TICKING_BOMB, // CS
		// DEFUSE_BOMB, // CS
		// GUARD_TICKING_BOMB, // CS
		// GUARD_BOMB_DEFUSER, // CS
		// GUARD_LOOSE_BOMB, // CS
		// GUARD_BOMB_ZONE, // CS
		CAPTURE_FLAG, // FF_TODO_AI_BEHAVIOR: FF example
		DEFEND_POINT, // FF_TODO_AI_BEHAVIOR: FF example
		PUSH_CART,    // FF_TODO_AI_BEHAVIOR: FF example
		GUARD_OBJECTIVE, // FF_TODO_AI_BEHAVIOR: FF generic
		GUARD_INITIAL_ENCOUNTER,
		// ESCAPE_FROM_BOMB, // CS
		HOLD_POSITION,
		FOLLOW,
		// VIP_ESCAPE, // CS
		// GUARD_VIP_ESCAPE_ZONE, // CS
		// COLLECT_HOSTAGES, // CS
		// RESCUE_HOSTAGES, // CS
		// GUARD_HOSTAGES, // CS
		// GUARD_HOSTAGE_RESCUE_ZONE, // CS
		MOVE_TO_LAST_KNOWN_ENEMY_POSITION,
		MOVE_TO_SNIPER_SPOT,
		SNIPING,
		CAPTURE_LUA_OBJECTIVE, // FF_TODO_LUA: For Lua-defined objectives
		DEFEND_LUA_OBJECTIVE,  // FF_TODO_LUA: For Lua-defined objectives

		NUM_TASKS // This needs to be last
	};
	void SetTask( TaskType task, CBaseEntity *entity = NULL );	///< set our current "task"
	void SetTask( TaskType task, const CFFBotManager::LuaObjectivePoint *objectiveTarget ); ///< FF_TODO_LUA: Overload for Lua objectives
	TaskType GetTask( void ) const;
	CBaseEntity *GetTaskEntity( void );
	const char *GetTaskName( void ) const;						///< return string describing current task

	//- behavior modifiers ------------------------------------------------------------------------------------------
	enum DispositionType
	{
		ENGAGE_AND_INVESTIGATE,								///< engage enemies on sight and investigate enemy noises
		OPPORTUNITY_FIRE,									///< engage enemies on sight, but only look towards enemy noises, dont investigate
		SELF_DEFENSE,										///< only engage if fired on, or very close to enemy
		IGNORE_ENEMIES,										///< ignore all enemies - useful for ducking around corners, running away, etc

		NUM_DISPOSITIONS
	};
	void SetDisposition( DispositionType disposition );		///< define how we react to enemies
	DispositionType GetDisposition( void ) const;
	const char *GetDispositionName( void ) const;			///< return string describing current disposition

	void IgnoreEnemies( float duration );					///< ignore enemies for a short duration

	enum MoraleType // FF_TODO_AI_BEHAVIOR: This might be generic enough or need FF specific values
	{
		TERRIBLE = -3,
		BAD = -2,
		NEGATIVE = -1,
		NEUTRAL = 0,
		POSITIVE = 1,
		GOOD = 2,
		EXCELLENT = 3,
	};
	MoraleType GetMorale( void ) const;
	const char *GetMoraleName( void ) const;				///< return string describing current morale
	void IncreaseMorale( void );
	void DecreaseMorale( void );

	void Surprise( float duration );						///< become "surprised" - can't attack
	bool IsSurprised( void ) const;							///< return true if we are "surprised"


	//- listening for noises ----------------------------------------------------------------------------------------
	bool IsNoiseHeard( void ) const;							///< return true if we have heard a noise
	bool HeardInterestingNoise( void );							///< return true if we heard an enemy noise worth checking in to
	void InvestigateNoise( void );								///< investigate recent enemy noise
	bool IsInvestigatingNoise( void ) const;					///< return true if we are investigating a noise
	const Vector *GetNoisePosition( void ) const;				///< return position of last heard noise, or NULL if none heard
	CNavArea *GetNoiseArea( void ) const;						///< return area where noise was heard
	void ForgetNoise( void );									///< clear the last heard noise
	bool CanSeeNoisePosition( void ) const;						///< return true if we directly see where we think the noise came from
	float GetNoiseRange( void ) const;							///< return approximate distance to last noise heard

	bool CanHearNearbyEnemyGunfire( float range = -1.0f ) const;///< return true if we hear nearby threatening enemy gunfire within given range (-1 == infinite)
	PriorityType GetNoisePriority( void ) const;				///< return priority of last heard noise

	//- radio and chatter--------------------------------------------------------------------------------------------
	void SendRadioMessage( RadioType event );					///< send a radio message - FF RadioType
	void SpeakAudio( const char *voiceFilename, float duration, int pitch );	///< send voice chatter
	FFBotChatter *GetChatter( void );					///< return an interface to this bot's chatter system // Changed
	bool RespondToHelpRequest( CFFPlayer *player, Place place, float maxRange = -1.0f );	///< decide if we should move to help the player, return true if we will // Changed
	bool IsUsingVoice() const;									///< new-style "voice" chatter gets voice feedback


	//- enemies ------------------------------------------------------------------------------------------------------
	void SetBotEnemy( CFFPlayer *enemy );						///< set given player as our current enemy // Changed
	CFFPlayer *GetBotEnemy( void ) const; // Changed
	// FF_TODO_GAME_MECHANIC: CFFPlayer::IsAttacking() or similar (checking if IN_ATTACK is held) would improve IsEnemyAimingAtMe accuracy.
	bool IsEnemyAimingAtMe( CFFPlayer *pEnemy ) const;			///< returns true if pEnemy is aiming at this bot
	int GetNearbyEnemyCount( void ) const;						///< return max number of nearby enemies we've seen recently
	// unsigned int GetEnemyPlace( void ) const;					///< return location where we see the majority of our enemies - FF_TODO_GAME_MECHANIC: FF Place definition
	// bool CanSeeBomber( void ) const;							///< return true if we can see the bomb carrier - FF No Bomb
	// CFFPlayer *GetBomber( void ) const; // Changed

	int GetNearbyFriendCount( void ) const;						///< return number of nearby teammates
	CFFPlayer *GetClosestVisibleFriend( void ) const;			///< return the closest friend that we can see // Changed
	CFFPlayer *GetClosestVisibleHumanFriend( void ) const;		///< return the closest human friend that we can see // Changed

	bool IsOutnumbered( void ) const;							///< return true if we are outnumbered by enemies
	int OutnumberedCount( void ) const;							///< return number of enemies we are outnumbered by

	#define ONLY_VISIBLE_ENEMIES true
	CFFPlayer *GetImportantEnemy( bool checkVisibility = false ) const;	///< return the closest "important" enemy for the given scenario // Changed

	void UpdateReactionQueue( void );							///< update our reaction time queue
	CFFPlayer *GetRecognizedEnemy( void );						///< return the most dangerous threat we are "conscious" of // Changed
	bool IsRecognizedEnemyReloading( void );					///< return true if the enemy we are "conscious" of is reloading
	// bool IsRecognizedEnemyProtectedByShield( void );			///< return true if the enemy we are "conscious" of is hiding behind a shield - FF_TODO_GAME_MECHANIC: FF No Shield
	float GetRangeToNearestRecognizedEnemy( void );				///< return distance to closest enemy we are "conscious" of

	CFFPlayer *GetAttacker( void ) const;						///< return last enemy that hurt us // Changed
	float GetTimeSinceAttacked( void ) const;					///< return duration since we were last injured by an attacker
	float GetFirstSawEnemyTimestamp( void ) const;				///< time since we saw any enemies
	float GetLastSawEnemyTimestamp( void ) const;
	float GetTimeSinceLastSawEnemy( void ) const;
	float GetTimeSinceAcquiredCurrentEnemy( void ) const;
	bool HasNotSeenEnemyForLongTime( void ) const;				///< return true if we haven't seen an enemy for "a long time"
	const Vector &GetLastKnownEnemyPosition( void ) const;
	bool IsEnemyVisible( void ) const;							///< is our current enemy visible
	float GetEnemyDeathTimestamp( void ) const;
	bool IsFriendInLineOfFire( void );							///< return true if a friend is in our weapon's way
	bool IsAwareOfEnemyDeath( void ) const;						///< return true if we *noticed* that our enemy died
	int GetLastVictimID( void ) const;							///< return the ID (entindex) of the last victim we killed, or zero

	bool CanSeeSniper( void ) const;							///< return true if we can see an enemy sniper
	bool HasSeenSniperRecently( void ) const;					///< return true if we have seen a sniper recently

	float GetTravelDistanceToPlayer( CFFPlayer *player ) const;	///< return shortest path travel distance to this player	// Changed
	bool DidPlayerJustFireWeapon( const CFFPlayer *player ) const;	///< return true if the given player just fired their weapon // Changed

	//- navigation --------------------------------------------------------------------------------------------------
	bool HasPath( void ) const;
	void DestroyPath( void );

	float GetFeetZ( void ) const;								///< return Z of bottom of feet

	enum PathResult
	{
		PROGRESSING,		///< we are moving along the path
		END_OF_PATH,		///< we reached the end of the path
		PATH_FAILURE		///< we failed to reach the end of the path
	};
	#define NO_SPEED_CHANGE false
	PathResult UpdatePathMovement( bool allowSpeedChange = true );	///< move along our computed path - if allowSpeedChange is true, bot will walk when near goal to ensure accuracy

	bool ComputePath( const Vector &goal, RouteType route = SAFEST_ROUTE );	///< compute path to goal position
	bool StayOnNavMesh( void );
	CNavArea *GetLastKnownArea( void ) const;						///< return the last area we know we were inside of // Changed CCSNavArea to CNavArea (or CFFNavArea if exists)
	const Vector &GetPathEndpoint( void ) const;					///< return final position of our current path
	float GetPathDistanceRemaining( void ) const;					///< return estimated distance left to travel along path
	void ResetStuckMonitor( void );
	bool IsAreaVisible( const CNavArea *area ) const;				///< is any portion of the area visible to this bot // Changed CCSNavArea to CNavArea
	const Vector &GetPathPosition( int index ) const;
	bool GetSimpleGroundHeightWithFloor( const Vector &pos, float *height, Vector *normal = NULL );	///< find "simple" ground height, treating current nav area as part of the floor
	void BreakablesCheck( void );
	void DoorCheck( void );											///< Check for any doors along our path that need opening

	virtual void PushawayTouch( CBaseEntity *pOther );

	Place GetPlace( void ) const;									///< get our current radio chatter place - FF_TODO_GAME_MECHANIC: FF Place definition

	bool IsUsingLadder( void ) const;								///< returns true if we are in the process of negotiating a ladder
	void GetOffLadder( void );										///< immediately jump off of our ladder, if we're on one

	void SetGoalEntity( CBaseEntity *entity );
	CBaseEntity *GetGoalEntity( void );

	bool IsNearJump( void ) const;									///< return true if nearing a jump in the path
	float GetApproximateFallDamage( float height ) const;			///< return how much damage will will take from the given fall height

	void ForceRun( float duration );								///< force the bot to run if it moves for the given duration
	virtual bool IsRunning( void ) const;

	void Wait( float duration );									///< wait where we are for the given duration
	bool IsWaiting( void ) const;									///< return true if we are waiting
	void StopWaiting( void );										///< stop waiting

	void Wiggle( void );											///< random movement, for getting un-stuck

	bool IsFriendInTheWay( const Vector &goalPos );					///< return true if a friend is between us and the given position
	void FeelerReflexAdjustment( Vector *goalPosition );			///< do reflex avoidance movements if our "feelers" are touched

	bool HasVisitedEnemySpawn( void ) const;						///< return true if we have visited enemy spawn at least once
	bool IsAtEnemySpawn( void ) const;								///< return true if we are at the/an enemy spawn right now

	//- looking around ----------------------------------------------------------------------------------------------
	const Vector &EyePositionConst( void ) const;
	
	void SetLookAngles( float yaw, float pitch );					///< set our desired look angles
	void UpdateLookAngles( void );									///< move actual view angles towards desired ones
	void UpdateLookAround( bool updateNow = false );				///< update "looking around" mechanism
	void InhibitLookAround( float duration );						///< block all "look at" and "looking around" behavior for given duration - just look ahead

	void SetForwardAngle( float angle );							///< define our forward facing
	void SetLookAheadAngle( float angle );							///< define default look ahead angle

	void SetLookAt( const char *desc, const Vector &pos, PriorityType pri, float duration = -1.0f, bool clearIfClose = false, float angleTolerance = 5.0f, bool attack = false );
	void ClearLookAt( void );										///< stop looking at a point in space and just look ahead
	bool IsLookingAtSpot( PriorityType pri = PRIORITY_LOW ) const;	///< return true if we are looking at spot with equal or higher priority
	bool IsViewMoving( float angleVelThreshold = 1.0f ) const;		///< returns true if bot's view angles are rotating (not still)
	bool HasViewBeenSteady( float duration ) const;					///< how long has our view been "steady" (ie: not moving) for given duration

	bool HasLookAtTarget( void ) const;								///< return true if we are in the process of looking at a target

	enum VisiblePartType // This enum can likely stay as is
	{
		NONE		= 0x00,
		GUT			= 0x01,
		HEAD		= 0x02,
		LEFT_SIDE	= 0x04,
		RIGHT_SIDE	= 0x08,
		FEET		= 0x10
	};

	#define CHECK_FOV true
	bool IsVisible( const Vector &pos, bool testFOV = false, const CBaseEntity *ignore = NULL ) const;	///< return true if we can see the point
	bool IsVisible( CFFPlayer *player, bool testFOV = false, unsigned char *visParts = NULL ) const;	///< return true if we can see any part of the player // Changed
	bool IsNoticable( const CFFPlayer *player, unsigned char visibleParts ) const;	///< return true if we "notice" given player // Changed

	bool IsEnemyPartVisible( VisiblePartType part ) const;			///< if enemy is visible, return the part we see for our current enemy
	const Vector &GetPartPosition( CFFPlayer *player, VisiblePartType part ) const;	///< return world space position of given part on player // Changed

	float ComputeWeaponSightRange( void );							///< return line-of-sight distance to obstacle along weapon fire ray

	bool IsAnyVisibleEnemyLookingAtMe( bool testFOV = false ) const;///< return true if any enemy I have LOS to is looking directly at me

	bool IsSignificantlyCloser( const CFFPlayer *testPlayer, const CFFPlayer *referencePlayer ) const;	///< return true if testPlayer is significantly closer than referencePlayer // Changed

	//- approach points ---------------------------------------------------------------------------------------------
	void ComputeApproachPoints( void );								///< determine the set of "approach points" representing where the enemy can enter this region
	void UpdateApproachPoints( void );								///< recompute the approach point set if we have moved far enough to invalidate the current ones
	void ClearApproachPoints( void );
	void DrawApproachPoints( void ) const;							///< for debugging
	float GetHidingSpotCheckTimestamp( HidingSpot *spot ) const;	///< return time when given spot was last checked
	void SetHidingSpotCheckTimestamp( HidingSpot *spot );			///< set the timestamp of the given spot to now

	const CNavArea *GetInitialEncounterArea( void ) const;			///< return area where we think we will first meet the enemy // Changed CCSNavArea to CNavArea
	void SetInitialEncounterArea( const CNavArea *area ); // Changed CCSNavArea to CNavArea

	//- weapon query and equip --------------------------------------------------------------------------------------
	#define MUST_EQUIP true
	void EquipBestWeapon( bool mustEquip = false );					///< equip the best weapon we are carrying that has ammo
	void EquipPistol( void );										///< equip our pistol - FF specific
	void EquipKnife( void );										///< equip the knife - FF specific

	#define DONT_USE_SMOKE_GRENADE true
	bool EquipGrenade( bool noSmoke = false );						///< equip a grenade, return false if we cant

	bool IsUsingKnife( void ) const;								///< returns true if we have knife equipped - FF_TODO_WEAPON_STATS: FF specific
	bool IsUsingPistol( void ) const;								///< returns true if we have pistol equipped - FF_TODO_WEAPON_STATS: FF specific
	bool IsUsingGrenade( void ) const;								///< returns true if we have grenade equipped
	bool IsUsingSniperRifle( void ) const;							///< returns true if using a "sniper" rifle - FF_TODO_WEAPON_STATS: FF specific
	// bool IsUsing( FFWeaponID weapon ) const;						///< returns true if using the specific weapon - FF_TODO_WEAPON_STATS: Changed CSWeaponID
	bool IsSniper( void ) const;									///< return true if we have a sniper rifle in our inventory - FF_TODO_WEAPON_STATS: FF specific
	bool IsSniping( void ) const;									///< return true if we are actively sniping (moving to sniper spot or settled in)
	bool IsUsingShotgun( void ) const;								///< returns true if using a shotgun - FF_TODO_WEAPON_STATS: FF specific
	bool IsUsingMachinegun( void ) const;							///< returns true if using the big 'ol machinegun - FF_TODO_WEAPON_STATS: FF specific
	void ThrowGrenade( const Vector &target );						///< begin the process of throwing the grenade
	bool IsThrowingGrenade( void ) const;							///< return true if we are in the process of throwing a grenade
	bool HasGrenade( void ) const;									///< return true if we have a grenade in our inventory
	void AvoidEnemyGrenades( void );								///< react to enemy grenades we see
	bool IsAvoidingGrenade( void ) const;							///< return true if we are in the act of avoiding a grenade
	// bool DoesActiveWeaponHaveSilencer( void ) const;				// FF_TODO_WEAPON_STATS: CS-specific, remove or adapt if FF has silencers
	bool CanActiveWeaponFire( void ) const;							///< returns true if our current weapon can attack
	CFFWeaponBase *GetActiveFFWeapon( void ) const;					///< get our current Fortress Forever weapon // FF_TODO_WEAPON_STATS: Changed return type from CBasePlayerWeapon

	void GiveWeapon( const char *weaponAlias );						///< Debug command to give a named weapon

	virtual void PrimaryAttack( void );

	enum ZoomType { NO_ZOOM, LOW_ZOOM, HIGH_ZOOM }; // FF_TODO_WEAPON_STATS: May need FF specific zoom levels
	ZoomType GetZoomLevel( void );

	bool AdjustZoom( float range );
	bool IsWaitingForZoom( void ) const;

	bool IsPrimaryWeaponEmpty( void ) const;
	bool IsPistolEmpty( void ) const;

	// int GetHostageEscortCount( void ) const;						 - FF No Hostages
	// void IncreaseHostageEscortCount( void );
	// float GetRangeToFarthestEscortedHostage( void ) const;
	// void ResetWaitForHostagePatience( void );

	//------------------------------------------------------------------------------------
	// Event hooks
	//

	virtual int OnTakeDamage( const CTakeDamageInfo &info );

	virtual void Event_Killed( const CTakeDamageInfo &info );

	virtual bool BumpWeapon( CFFWeaponBase *pWeapon ); // FF_TODO_WEAPON_STATS: Changed CBaseCombatWeapon to CFFWeaponBase


	// FF Specific Event Handlers (already declared in ff_bot_manager.h, mirrored here)
	void OnFFRestartRound( IGameEvent *event );
	void OnPlayerChangeClass( IGameEvent *event );
	void OnDisguiseLost( IGameEvent *event );
	void OnCloakLost( IGameEvent *event );
	void OnBuildDispenser( IGameEvent *event );
	void OnBuildSentryGun( IGameEvent *event );
	void OnBuildDetpack( IGameEvent *event );
	void OnBuildManCannon( IGameEvent *event );
	void OnDispenserKilled( IGameEvent *event );
	void OnDispenserDismantled( IGameEvent *event );
	void OnDispenserDetonated( IGameEvent *event );
	void OnDispenserSabotaged( IGameEvent *event );
	void OnSentryGunKilled( IGameEvent *event );
	void OnSentryGunDismantled( IGameEvent *event );
	void OnSentryGunDetonated( IGameEvent *event );
	void OnSentryGunUpgraded( IGameEvent *event );
	void OnSentryGunSabotaged( IGameEvent *event );
	void OnDetpackDetonated( IGameEvent *event );
	void OnManCannonDetonated( IGameEvent *event );

	void OnPlayerFootstep( IGameEvent *event );
	void OnPlayerRadio( IGameEvent *event );
	void OnPlayerDeath( IGameEvent *event );
	void OnPlayerFallDamage( IGameEvent *event );
	void OnRoundEnd( IGameEvent *event );
	void OnRoundStart( IGameEvent *event );
	void OnDoorMoving( IGameEvent *event );
	void OnBreakProp( IGameEvent *event );
	void OnBreakBreakable( IGameEvent *event );
	void OnWeaponFire( IGameEvent *event );
	void OnWeaponFireOnEmpty( IGameEvent *event );
	void OnWeaponReload( IGameEvent *event );
	// void OnWeaponZoom( IGameEvent *event ); // FF Specific
	void OnBulletImpact( IGameEvent *event );
	void OnHEGrenadeDetonate( IGameEvent *event );
	// void OnFlashbangDetonate( IGameEvent *event ); // FF_TODO_GAME_MECHANIC: FF Specific
	// void OnSmokeGrenadeDetonate( IGameEvent *event ); // FF_TODO_GAME_MECHANIC: FF Specific
	void OnGrenadeBounce( IGameEvent *event );
	void OnNavBlocked( IGameEvent *event );

	void OnEnteredNavArea( CNavArea *newArea );						///< invoked when bot enters a nav area // Changed CCSNavArea to CNavArea

private:
	#define IS_FOOTSTEP true
	void OnAudibleEvent( IGameEvent *event, CBasePlayer *player, float range, PriorityType priority, bool isHostile, bool isFootstep = false, const Vector *actualOrigin = NULL );	///< Checks if the bot can hear the event

private:
	friend class CFFBotManager; // Changed

	/// @todo Get rid of these
	friend class AttackState;
	friend class BuyState; // FF BuyState will be different

	Vector m_eyePosition;

	// ResetValues, BotDeathThink, DebugDisplay are fine

	char m_name[64];
	void DebugDisplay( void ) const;

	//- behavior properties ------------------------------------------------------------------------------------------
	float m_combatRange;
	mutable bool m_isRogue;
	mutable CountdownTimer m_rogueTimer;
	MoraleType m_morale;
	bool m_diedLastRound;
	float m_safeTime;
	bool m_wasSafe;
	void AdjustSafeTime( void );
	NavRelativeDirType m_blindMoveDir;
	bool m_blindFire;
	CountdownTimer m_surpriseTimer;

	bool m_isFollowing;
	CHandle< CFFPlayer > m_leader;									///< the ID of who we are following // Changed
	float m_followTimestamp;
	float m_allowAutoFollowTime;
	// FF_TODO_WEAPON_STATS: CS specific weapon states, remove or adapt
	// bool m_hasPrimaryWeapon;
	// bool m_hasSecondaryWeapon;
	// bool m_hasBomb;
	// bool m_hasDefuseKit;
	// bool m_hasNightVision;
	// bool m_hasShield;

	CountdownTimer m_hurryTimer;
	CountdownTimer m_alertTimer;
	CountdownTimer m_sneakTimer;
	CountdownTimer m_panicTimer;


	// instances of each possible behavior state, to avoid dynamic memory allocation during runtime
	IdleState				m_idleState;
	HuntState				m_huntState;
	AttackState				m_attackState;
	InvestigateNoiseState	m_investigateNoiseState;
	BuyState				m_buyState; // FF BuyState
	MoveToState				m_moveToState;
	// CS States removed: FetchBombState, PlantBombState, DefuseBombState, EscapeFromBombState
	// FF_TODO_AI_BEHAVIOR: FF Objective States to be added here
	HideState				m_hideState;
	FollowState				m_followState;
	UseEntityState			m_useEntityState;
	OpenDoorState			m_openDoorState;
	class CaptureObjectiveState m_captureObjectiveState; // FF_TODO_LUA: Added state instance
	HealTeammateState		m_healTeammateState;     // Medic healing state
	BuildSentryState		m_buildSentryState;      // Engineer building state
	BuildDispenserState		m_buildDispenserState;   // Engineer building dispenser state
	RepairBuildableState	m_repairBuildableState;  // Engineer repairing state
	FindResourcesState		m_findResourcesState;    // Engineer finding resources state
	GuardSentryState		m_guardSentryState;      // Engineer guarding sentry state
	InfiltrateState			m_infiltrateState;       // Spy infiltration state
	class RetreatState		m_retreatState;          // State for retreating

	// Engineer buildable selection
	enum BuildableType { BUILDABLE_NONE, BUILDABLE_SENTRY, BUILDABLE_DISPENSER /*, BUILDABLE_TELE_ENTRANCE, BUILDABLE_TELE_EXIT */ };
	BuildableType m_selectedBuildableType;
	CountdownTimer m_cycleBuildableCooldown;

	void SetState( BotState *state );
	BotState *m_state;
	float m_stateTimestamp;
	bool m_isAttacking;
	bool m_isOpeningDoor;

public: // FF_TODO_LUA: Made public to be callable from IdleState etc.
	void CaptureObjective(const CFFBotManager::LuaObjectivePoint* objective);

	// Medic behavior
	bool IsMedic(void) const;
	CFFPlayer* FindNearbyInjuredTeammate(float maxRange = 1000.0f, float healthRatioThreshold = 0.98f);
	void StartHealing(CFFPlayer* target);

	// Engineer behavior
	bool IsEngineer(void) const;
	bool HasSentry(void) const; // Checks if engineer already has a sentry deployed
	void TryToBuildSentry(const Vector *location = NULL);
	bool HasDispenser(void) const; // Checks if engineer already has a dispenser deployed
	void TryToBuildDispenser(const Vector *location = NULL);
	CBaseEntity* FindNearbyDamagedFriendlyBuildable(float maxRange = 800.0f);
	void TryToRepairBuildable(CBaseEntity* targetBuildable = NULL);
	CBaseEntity* FindResourceSource(float maxRange = 2000.0f);
	void TryToFindResources();
	bool TryToGuardSentry( CBaseEntity *sentryToGuard = NULL );		// tell an Engineer bot to guard its sentry (or a specified one)

	// Engineer buildable selection methods
	void CycleSelectedBuildable();
	void SelectSpecificBuildable(BuildableType type);
	BuildableType GetSelectedBuildable() const;
	const char* GetSelectedBuildableName() const; // For logging

	// Spy behavior
	bool IsSpy() const;
	CBaseEntity* FindSpyTarget(float maxRange = 3000.0f); // Can target buildings or players
	void TryToInfiltrate();
	bool IsBehind(const CBaseEntity* target) const; // Conceptual check if bot is behind target
	void StartSabotaging(CBaseEntity* pBuilding); // Conceptual: Bot initiates sabotage C++ call

	// Scout behavior
	bool IsScout(void) const;
	void TryDoubleJump(void);

	// Retreat behavior
	void TryToRetreat(const CTakeDamageInfo *info = NULL);
	bool IsRetreating(void) const;


	// Notification methods called by CFFBotManager (or states)
public:
	void NotifyBuildingSapped(CBaseEntity *sappedBuilding, bool isSapped);
	void NotifyBuildingUpgraded(CBaseEntity *building, int newLevel);    // Game event must pass new level
	void NotifyBuildingDestroyed(CBaseEntity *building);
	void NotifyBuildingBuilt(CBaseEntity* newBuilding, BuildableType type);     // Called when construction is complete by game event
	void NotifyBuildingPlacementStarted(BuildableType type); // Called by bot when it initiates blueprint placement

	// Engineer buildable status helpers
	void SetBuildingLevel(CBaseEntity* pBuilding, int level); // Sets bot's belief of level
	// bool IsOwnBuildingInProgress(BuildableType type) const; // To be removed, direct check on entity preferred.


private: // Moved scout members here to group with other private bot logic
	CountdownTimer m_doubleJumpCooldown;
	bool m_isAttemptingDoubleJump;
	int  m_doubleJumpPhase; // 0 = ready, 1 = first jump pressed, 2 = first jump released, 3 = second jump pressed
	CountdownTimer m_doubleJumpPhaseTimer;

	// Pyro specific
	CountdownTimer m_airblastCooldown; // Already present from previous Pyro work
	CountdownTimer m_projectileScanTimer; // For ScanForNearbyProjectiles

	// Persistent handles to own buildables & their status
	CHandle<CBaseEntity> m_sentryGun;    // Using CBaseEntity, cast to CFFSentryGun if needed
	CHandle<CBaseEntity> m_dispenser;  // Using CBaseEntity, cast to CFFDispenser if needed
	// CHandle<CBaseEntity> m_teleEntrance; // FF_TODO_CLASS_ENGINEER: Teleporter buildable handling
	// CHandle<CBaseEntity> m_teleExit;     // FF_TODO_CLASS_ENGINEER: Teleporter buildable handling
	int m_sentryLevel;    // Bot's belief of its sentry's level, updated by events
	int m_dispenserLevel; // Bot's belief of its dispenser's level, updated by events
	// bool m_sentryIsBuilding;    // Removed: States will check entity->IsBuilt() or equivalent
	// bool m_dispenserIsBuilding; // Removed: States will check entity->IsBuilt() or equivalent

	CHandle<CBaseEntity> m_sappedBuildingHandle; // Which of my buildings is currently known to be sapped
	bool m_hasSappedBuilding;              // True if any of my buildings are known to be sapped, updated by events

	CountdownTimer m_weaponSwitchCheckTimer; // Timer for periodic weapon selection checks

public: // Re-declare static consts as public if they were meant to be, or move to .cpp if private
	static const int ENGINEER_LOW_CELL_THRESHOLD = 50; // FF_TODO_CLASS_ENGINEER: Verify game value
	static const int ENGINEER_MAX_CELLS = 200; // Max desired cells for an Engineer bot - FF_TODO_CLASS_ENGINEER: Verify game value
	static const float FLAMETHROWER_EFFECTIVE_RANGE = 350.0f; // FF_TODO_WEAPON_STATS: Verify game value
	static const float MINIGUN_SPINUP_TIME = 0.87f; // FF_TODO_WEAPON_STATS: Verify game value
	static const float MINIGUN_EFFECTIVE_RANGE = 1200.0f; // FF_TODO_WEAPON_STATS: Verify game value
	static const float MINIGUN_SPINDOWN_DELAY = 3.0f; // FF_TODO_CLASS_HEAVY: This might be replaced by MINIGUN_SUSTAINED_FIRE_LOST_SIGHT_DURATION logic or other game mechanic
	static const float SCOUT_MAX_SPEED = 400.0f; // FF_TODO_CLASS_SCOUT: Verify game value
	static const float HEAVY_MAX_SPEED = 230.0f; // FF_TODO_CLASS_HEAVY: Verify actual Heavy speed
	static const float HEAVY_SPUNUP_MAX_SPEED = 110.0f; // FF_TODO_CLASS_HEAVY: Verify actual spun-up Heavy speed
	static const float RETREAT_HEALTH_THRESHOLD_PERCENT = 0.3f; // Retreat if health is below 30% - FF_TODO_AI_BEHAVIOR: Tune this value
	static const float MELEE_COMBAT_RANGE = 100.0f;      // Range for preferring melee - FF_TODO_WEAPON_STATS: Tune this value
	static const float LONG_COMBAT_RANGE = 1500.0f;     // Range for preferring long-range weapons (like sniper rifle) - FF_TODO_WEAPON_STATS: Tune this value


private:

	TaskType m_task;
	EHANDLE m_taskEntity;

	//- navigation ---------------------------------------------------------------------------------------------------
	Vector m_goalPosition;
	EHANDLE m_goalEntity; // Changed CBaseEntity*
	void MoveTowardsPosition( const Vector &pos );
	void MoveAwayFromPosition( const Vector &pos );
	void StrafeAwayFromPosition( const Vector &pos );
	void StuckCheck( void );
	CNavArea *m_currentArea;										///< the nav area we are standing on // Changed CCSNavArea to CNavArea
	CNavArea *m_lastKnownArea;										///< the last area we were in // Changed CCSNavArea to CNavArea
	EHANDLE m_avoid;												///< higher priority player we need to make way for // Changed CBaseEntity*
	float m_avoidTimestamp;
	bool m_isStopping;
	bool m_hasVisitedEnemySpawn;
	IntervalTimer m_stillTimer;

	//- path navigation data ----------------------------------------------------------------------------------------
	enum { MAX_PATH_LENGTH = 256 };
	struct ConnectInfo
	{
		CNavArea *area;												///< the area along the path // Changed CCSNavArea to CNavArea
		NavTraverseType how;
		Vector pos;
		const CNavLadder *ladder;
	}
	m_path[ MAX_PATH_LENGTH ];
	int m_pathLength;
	int m_pathIndex;
	float m_areaEnteredTimestamp;
	void BuildTrivialPath( const Vector &goal );

	CountdownTimer m_repathTimer;

	bool ComputePathPositions( void );
	void SetupLadderMovement( void );
	void SetPathIndex( int index );
	void DrawPath( void );
	int FindOurPositionOnPath( Vector *close, bool local = false ) const;
	int FindPathPoint( float aheadRange, Vector *point, int *prevIndex = NULL );
	bool FindClosestPointOnPath( const Vector &pos, int startIndex, int endIndex, Vector *close ) const;
	bool IsStraightLinePathWalkable( const Vector &goal ) const;
	void ComputeLadderAngles( float *yaw, float *pitch );

	mutable CountdownTimer m_avoidFriendTimer;
	mutable bool m_isFriendInTheWay;
	CountdownTimer m_politeTimer;
	bool m_isWaitingBehindFriend;

	#define ONLY_JUMP_DOWN true
	bool DiscontinuityJump( float ground, bool onlyJumpDown = false, bool mustJump = false );

	enum LadderNavState // This can likely stay as is
	{
		APPROACH_ASCENDING_LADDER,
		APPROACH_DESCENDING_LADDER,
		FACE_ASCENDING_LADDER,
		FACE_DESCENDING_LADDER,
		MOUNT_ASCENDING_LADDER,
		MOUNT_DESCENDING_LADDER,
		ASCEND_LADDER,
		DESCEND_LADDER,
		DISMOUNT_ASCENDING_LADDER,
		DISMOUNT_DESCENDING_LADDER,
		MOVE_TO_DESTINATION,
	}
	m_pathLadderState;
	bool m_pathLadderFaceIn;
	const CNavLadder *m_pathLadder;
	bool UpdateLadderMovement( void );
	NavRelativeDirType m_pathLadderDismountDir;
	float m_pathLadderDismountTimestamp;
	float m_pathLadderEnd;
	void ComputeLadderEndpoint( bool ascending );
	float m_pathLadderTimestamp;

	CountdownTimer m_mustRunTimer;
	CountdownTimer m_waitTimer;

	void UpdateTravelDistanceToAllPlayers( void );
	CountdownTimer m_updateTravelDistanceTimer;
	float m_playerTravelDistance[ MAX_PLAYERS ];
	unsigned char m_travelDistancePhase;

	//- game scenario mechanisms -------------------------------------------------------------------------------------
	CFFBotGameState m_gameState;										///< our current knowledge about the state of the scenario // Changed

	// byte m_hostageEscortCount;										///< the number of hostages we're currently escorting - FF No Hostages
	// void UpdateHostageEscortCount( void );
	// float m_hostageEscortCountTimestamp;

	int m_desiredTeam;
	bool m_hasJoined;

	// bool m_isWaitingForHostage; // FF No Hostages
	// CountdownTimer m_inhibitWaitingForHostageTimer;
	// CountdownTimer m_waitForHostageTimer;

	//- listening mechanism ------------------------------------------------------------------------------------------
	Vector m_noisePosition;
	float m_noiseTravelDistance;
	float m_noiseTimestamp;
	CNavArea *m_noiseArea;											///< the nav area containing the noise // Changed CCSNavArea to CNavArea
	PriorityType m_noisePriority;
	bool UpdateLookAtNoise( void );
	CountdownTimer m_noiseBendTimer;
	Vector m_bentNoisePosition;
	bool m_bendNoisePositionValid;

	//- "looking around" mechanism -----------------------------------------------------------------------------------
	float m_lookAroundStateTimestamp;
	float m_lookAheadAngle;
	float m_forwardAngle;
	float m_inhibitLookAroundTimestamp;

	enum LookAtSpotState // This can likely stay
	{
		NOT_LOOKING_AT_SPOT,
		LOOK_TOWARDS_SPOT,
		LOOK_AT_SPOT,
		NUM_LOOK_AT_SPOT_STATES
	}
	m_lookAtSpotState;
	Vector m_lookAtSpot;
	PriorityType m_lookAtSpotPriority;
	float m_lookAtSpotDuration;
	float m_lookAtSpotTimestamp;
	float m_lookAtSpotAngleTolerance;
	bool m_lookAtSpotClearIfClose;
	bool m_lookAtSpotAttack;
	const char *m_lookAtDesc;
	void UpdateLookAt( void );
	void UpdatePeripheralVision();
	float m_peripheralTimestamp;

	enum { MAX_APPROACH_POINTS = 16 };
	struct ApproachPoint
	{
		Vector m_pos;
		CNavArea *m_area; // Changed CCSNavArea to CNavArea
	};

	ApproachPoint m_approachPoint[ MAX_APPROACH_POINTS ];
	unsigned char m_approachPointCount;
	Vector m_approachPointViewPosition;

	CBaseEntity * FindEntitiesOnPath( float distance, CPushAwayEnumerator *enumerator, bool checkStuck );

	IntervalTimer m_viewSteadyTimer;

	bool BendLineOfSight( const Vector &eye, const Vector &target, Vector *bend, float angleLimit = 135.0f ) const;
	bool FindApproachPointNearestPath( Vector *pos );
	bool FindGrenadeTossPathTarget( Vector *pos );
	enum GrenadeTossState // This can likely stay
	{
		NOT_THROWING,
		START_THROW,
		THROW_LINED_UP,
		FINISH_THROW,
	};
	GrenadeTossState m_grenadeTossState;
	CountdownTimer m_tossGrenadeTimer;
	const CNavArea *m_initialEncounterArea;							///< area where we think we will initially encounter the enemy // Changed CCSNavArea to CNavArea
	void LookForGrenadeTargets( void );
	void UpdateGrenadeThrow( void );
	CountdownTimer m_isAvoidingGrenade;


	SpotEncounter *m_spotEncounter;
	float m_spotCheckTimestamp;

	enum { MAX_CHECKED_SPOTS = 64 };
	struct HidingSpotCheckInfo
	{
		HidingSpot *spot;
		float timestamp;
	}
	m_checkedHidingSpot[ MAX_CHECKED_SPOTS ];
	int m_checkedHidingSpotCount;

	//- view angle mechanism -----------------------------------------------------------------------------------------
	float m_lookPitch;
	float m_lookPitchVel;
	float m_lookYaw;
	float m_lookYawVel;

	//- aim angle mechanism -----------------------------------------------------------------------------------------
	Vector m_aimOffset;
	Vector m_aimOffsetGoal;
	float m_aimOffsetTimestamp;
	float m_aimSpreadTimestamp;
	void SetAimOffset( float accuracy );
	void UpdateAimOffset( void );
	Vector m_aimSpot;

	struct PartInfo // This can likely stay
	{
		Vector m_headPos;
		Vector m_gutPos;
		Vector m_feetPos;
		Vector m_leftSidePos;
		Vector m_rightSidePos;
		int m_validFrame;
	};
	static PartInfo m_partInfo[ MAX_PLAYERS ];
	void ComputePartPositions( CFFPlayer *player );					///< compute part positions from bone location // Changed

	//- attack state data --------------------------------------------------------------------------------------------
	DispositionType m_disposition;
	CountdownTimer m_ignoreEnemiesTimer;
	CHandle< CFFPlayer > m_enemy;							///< our current enemy // Changed
	bool m_isEnemyVisible;
	unsigned char m_visibleEnemyParts;
	Vector m_lastEnemyPosition;
	float m_lastSawEnemyTimestamp;
	float m_firstSawEnemyTimestamp;
	float m_currentEnemyAcquireTimestamp;
	float m_enemyDeathTimestamp;
	float m_friendDeathTimestamp;
	bool m_isLastEnemyDead;
	int m_nearbyEnemyCount;
	// unsigned int m_enemyPlace;										///< the location where we saw most of our enemies - FF_TODO_GAME_MECHANIC: FF Place definition
	Place m_enemyPlace; // Keep Place for now, will need FF_TODO_GAME_MECHANIC: FF specific definition

	struct WatchInfo
	{
		float timestamp;
		bool isEnemy;
	}
	m_watchInfo[ MAX_PLAYERS ];
	// CHandle< CFFPlayer > m_bomber;							///< points to bomber if we can see him - FF No Bomb // Changed

	int m_nearbyFriendCount;
	CHandle< CFFPlayer > m_closestVisibleFriend;			///< the closest friend we can see // Changed
	CHandle< CFFPlayer > m_closestVisibleHumanFriend;		///< the closest human friend we can see // Changed

	IntervalTimer m_attentionInterval;

	CHandle< CFFPlayer > m_attacker;						///< last enemy that hurt us (may not be same as m_enemy) // Changed
	float m_attackedTimestamp;

	int m_lastVictimID;
	bool m_isAimingAtEnemy;
	bool m_isRapidFiring;
	IntervalTimer m_equipTimer;
	CountdownTimer m_zoomTimer;
	bool DoEquip( CFFWeaponBase *gun );								///< equip the given item // FF_TODO_WEAPON_STATS: Changed CBasePlayerWeapon to CFFWeaponBase

	void ReloadCheck( void );
	// void SilencerCheck( void );										// FF_TODO_WEAPON_STATS: CS specific, remove or adapt

	float m_fireWeaponTimestamp;

	bool m_isEnemySniperVisible;
	CountdownTimer m_sawEnemySniperTimer;
	
	//- reaction time system -----------------------------------------------------------------------------------------
	enum { MAX_ENEMY_QUEUE = 20 };
	struct ReactionState
	{
		CHandle<CFFPlayer> player; // Changed
		bool isReloading;
		// bool isProtectedByShield; // FF No Shield
	}
	m_enemyQueue[ MAX_ENEMY_QUEUE ];
	byte m_enemyQueueIndex;
	byte m_enemyQueueCount;
	byte m_enemyQueueAttendIndex;

	CFFPlayer *FindMostDangerousThreat( void );						///< return most dangerous threat in my field of view (feeds into reaction time queue) // Changed


	//- stuck detection ---------------------------------------------------------------------------------------------
	bool m_isStuck;
	float m_stuckTimestamp;
	Vector m_stuckSpot;
	NavRelativeDirType m_wiggleDirection;
	CountdownTimer m_wiggleTimer;
	CountdownTimer m_stuckJumpTimer;

	//- Lua Objective Data Accessors --------------------------------------------------------------------------------
public:
	int GetLuaObjectivePointCount() const;
	const CFFBotManager::LuaObjectivePoint* GetLuaObjectivePoint(int index) const;
	const CUtlVector<CFFBotManager::LuaObjectivePoint>& GetAllLuaObjectivePoints() const;
	const CFFBotManager::LuaObjectivePoint* GetClosestLuaObjectivePoint(const Vector &pos, int teamAffiliation = FF_TEAM_NEUTRAL, float maxDist = -1.0f) const;

	int GetLuaPathPointCount() const;
	const CFFBotManager::LuaPathPoint* GetLuaPathPoint(int index) const;
	const CUtlVector<CFFBotManager::LuaPathPoint>& GetAllLuaPathPoints() const;
	const CFFBotManager::LuaPathPoint* GetClosestLuaPathPoint(const Vector &pos, float maxDist = -1.0f) const; // Path points usually aren't team affiliated

private:
	enum { MAX_VEL_SAMPLES = 10 };	
	float m_avgVel[ MAX_VEL_SAMPLES ];
	int m_avgVelIndex;
	int m_avgVelCount;
	Vector m_lastOrigin;

	//- radio --------------------------------------------------------------------------------------------------------
	RadioType m_lastRadioCommand;									// FF RadioType
	float m_lastRadioRecievedTimestamp;
	float m_lastRadioSentTimestamp;
	CHandle< CFFPlayer > m_radioSubject;							///< who issued the radio message // Changed
	Vector m_radioPosition;
	void RespondToRadioCommands( void );
	bool IsRadioCommand( RadioType event ) const;

	float m_voiceEndTimestamp;

	FFBotChatter m_chatter;									///< chatter mechanism // Changed

public:
	// Weapon selection
	CFFWeaponBase* SelectBestWeaponForSituation(CFFPlayer* pEnemy = NULL, float flEnemyDist = -1.0f);

	// Pyro Airblast
	void ScanForNearbyProjectiles();
	void TryToAirblast(CBaseEntity* pTargetProjectile = NULL); // Modified signature
};


//
// Inlines
//
// FF_TODO_WEAPON_STATS: Changed return type to CFFWeaponBase
inline CFFWeaponBase *CFFBot::GetEquippedWeapon( void ) const // Changed CCSBot to CFFBot
{
	return static_cast<CFFWeaponBase *>( GetActiveWeapon() );
}

inline bool CFFBot::IsJumping( void ) const // Changed CCSBot to CFFBot
{
	return (GetFlags() & FL_DUCKING) ? false : (GetGroundEntity() == NULL && GetWaterLevel() == 0);
}

inline bool CFFBot::IsOnLadder( void ) const // Changed CCSBot to CFFBot
{
	return (GetMoveType() == MOVETYPE_LADDER);
}

inline bool CFFBot::IsUsingLadder( void ) const // Changed CCSBot to CFFBot
{
	return (m_ladder != NULL);
}

inline CNavLadder *CFFBot::GetCurrentLadder( void ) const // Changed CCSBot to CFFBot
{
	return m_ladder;
}

inline bool CFFBot::IsAttacking( void ) const // Changed CCSBot to CFFBot
{
	return (m_fireWeaponTimestamp > 0.0f && m_fireWeaponTimestamp >= gpGlobals->curtime - 0.2f);
}

inline bool CFFBot::IsAtApproachPoint( void ) const // Changed CCSBot to CFFBot
{
	return (m_approachPointCount && IsAtApproximatePosition( m_approachPoint[ m_approachPointCount-1 ] ));
}

inline bool CFFBot::IsAtViewpoint( void ) const // Changed CCSBot to CFFBot
{
	return (m_approachPointCount && IsAtApproximatePosition( m_approachPoint[ m_approachPointViewPositionIndex ] ));
}

inline const Vector &CFFBot::GetApproachPoint( void ) const // Changed CCSBot to CFFBot
{
	return m_approachPoint[ m_approachPointCount-1 ];
}

inline bool CFFBot::IsAtEnemy( void ) const // Changed CCSBot to CFFBot
{
	if (GetTask() == TASK_ATTACK && GetTaskEntity())
		return IsAtApproximatePosition( GetTaskEntity()->GetAbsOrigin() );

	if (GetEnemy() == NULL)
		return true;

	return IsAtApproximatePosition( GetEnemy()->GetAbsOrigin() );
}

inline bool CFFBot::IsAtGoal( void ) const // Changed CCSBot to CFFBot
{
	return IsAtApproximatePosition( m_goalPosition );
}

inline bool CFFBot::IsAtHidingSpot( void ) const // Changed CCSBot to CFFBot
{
	if (m_hidingSpot == NULL)
		return false;

	return IsAtApproximatePosition( *m_hidingSpot->GetPosition() );
}

inline bool CFFBot::IsCamping( void ) const // Changed CCSBot to CFFBot
{
	return (GetTask() == TASK_CAMP || GetTask() == TASK_GUARD_SPOT);
}

inline const Vector *CFFBot::GetHidingSpot( void ) const // Changed CCSBot to CFFBot
{
	if (m_hidingSpot == NULL)
		return NULL;

	return m_hidingSpot->GetPosition();
}

inline bool CFFBot::IsScared( void ) const // Changed CCSBot to CFFBot
{
	return (m_morale == AFRAID || m_morale == TERRIFIED);
}

inline bool CFFBot::IsSurprised( void ) const // Changed CCSBot to CFFBot
{
	return m_surpriseTimer.HasStarted() && !m_surpriseTimer.IsElapsed();
}

inline bool CFFBot::IsHunting( void ) const // Changed CCSBot to CFFBot
{
	return (GetTask() == TASK_SEEK_ENEMY || GetTask() == TASK_HUNT);
}

inline bool CFFBot::IsFollowing( void ) const // Changed CCSBot to CFFBot
{
	return m_isFollowing;
}

inline CBasePlayer *CFFBot::GetFollowLeader( void ) const // Changed CCSBot to CFFBot
{
	return m_leader;
}

inline bool CFFBot::IsOnAttackApproach( void ) const // Changed CCSBot to CFFBot
{
	return (GetTask() == TASK_GOTO_ENEMY || GetTask() == TASK_ATTACK);
}

inline bool CFFBot::IsOpeningDoor( void ) const // Changed CCSBot to CFFBot
{
	return m_isOpeningDoor;
}

// FF: Bomb-related inlines removed

inline void CFFBot::SetMorale( MoraleEntityType type ) // Changed CCSBot to CFFBot
{
	m_morale = type;
}

inline void CFFBot::IncreaseMorale( void ) // Changed CCSBot to CFFBot
{
	if (m_morale < NUM_MORALE_TYPES-1)
		m_morale = (MoraleEntityType)(m_morale+1);
}

inline void CFFBot::DecreaseMorale( void ) // Changed CCSBot to CFFBot
{
	if (m_morale > 0)
		m_morale = (MoraleEntityType)(m_morale-1);
}

inline bool CFFBot::IsTask( BotTaskType task, CBaseEntity *entity ) const // Changed CCSBot to CFFBot
{
	return (m_task == task && m_taskEntity == entity);
}

inline bool CFFBot::IsTask( BotTaskType task, const Vector *pos ) const // Changed CCSBot to CFFBot
{
	return (m_task == task && m_taskPosition == *pos);
}

inline Place CFFBot::GetPlace( void ) const // Changed CCSBot to CFFBot
{
	return m_place;
}

inline void CFFBot::SetPlace( Place place ) // Changed CCSBot to CFFBot
{
	m_place = place;
}

#endif // _FF_BOT_H_ // Changed from _CS_BOT_H_

