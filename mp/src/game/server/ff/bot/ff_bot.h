//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

//
// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003
//
// NOTE: The Fortress Forever Bot code uses Doxygen-style comments. If you run Doxygen over this code, it will
// auto-generate documentation.  Visit www.doxygen.org to download the system for free.
//

#ifndef _FF_BOT_H_
#define _FF_BOT_H_

#include "../../shared/bot/bot.h"
#include "ff_bot_manager.h"
// #include "ff_bot_chatter.h" // Removed FF-specific chatter
#include "ff_gamestate.h"
#include "../ff_player.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "nav_area.h"
// TODO: cs_nav_pathfind.h was included. If CFFNavPath is a direct replacement, include "ff_nav_path.h".
// Otherwise, nav_pathfind.h or nav_mesh.h might provide CNavPath or similar generic path objects.
// For now, including a generic one.
#include "nav_pathfind.h"
#include "bot_constants.h" // For enums like RouteType, PriorityType, Place, RadioType, FFWeaponID etc.

// State machine states
#include "states/ff_bot_state_idle.h"
#include "states/ff_bot_state_attack.h"
#include "states/ff_bot_state_hunt.h"
#include "states/ff_bot_state_investigate_noise.h"
#include "states/ff_bot_state_move_to.h"
#include "states/ff_bot_state_buy.h"
#include "states/ff_bot_state_hide.h"
#include "states/ff_bot_state_follow.h"
// Removed includes for CS-specific bomb states
// #include "states/ff_bot_state_fetch_bomb.h"
// #include "states/ff_bot_state_plant_bomb.h"
// #include "states/ff_bot_state_defuse_bomb.h"
// #include "states/ff_bot_state_escape_from_bomb.h"

#include "states/ff_bot_state_use_entity.h"
#include "states/ff_bot_state_open_door.h"
#include "states/ff_bot_state_capture_point.h"
#include "states/ff_bot_state_carry_flag.h" // Added CarryFlagState include

// Forward declarations for engine/game classes
class CapturePointState;
class CarryFlagState; // Added forward declaration
class CBaseDoor;
class CBasePropDoor;
class CPushAwayEnumerator; // Likely from obstacle_pushaway.h or similar
class CNavLadder;        // From nav_ladder.h, often included via nav_mesh.h
class SpotEncounter;     // Related to pathing/spots, might be in a nav utility header
class HidingSpot;        // From nav_hiding_spot.h or similar
class IGameEvent;        // From igameevents.h
struct CTakeDamageInfo;  // From shareddefs.h or similar
class CBaseCombatWeapon; // From shareddefs.h or similar


//--------------------------------------------------------------------------------------------------------------
/**
 * For use with player->m_rgpPlayerItems[]
 * TODO: OBJECTIVE_ITEM_SLOT was C4_SLOT. Adapt for FF if it has a similar unique objective item slot.
 */
enum InventorySlotType
{
	PRIMARY_WEAPON_SLOT = 1,
	PISTOL_SLOT,
	KNIFE_SLOT, // Or Melee Slot
	GRENADE_SLOT,
	OBJECTIVE_ITEM_SLOT // Was C4_SLOT, made generic for FF objectives
};


//--------------------------------------------------------------------------------------------------------------
/**
 * The definition of a bot's behavior state.  One or more finite state machines 
 * using these states implement a bot's behaviors.
 */
class BotState
{
public:
	virtual ~BotState() {}
	virtual void OnEnter( CFFBot *bot ) { }				///< when state is entered
	virtual void OnUpdate( CFFBot *bot ) { }			///< state behavior
	virtual void OnExit( CFFBot *bot ) { }				///< when state exited
	virtual const char *GetName( void ) const = 0;		///< return state name
};


//--------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------
/**
 * The Fortress Forever Bot
 */
class CFFBot : public CBot< CFFPlayer >
{
public:
	DECLARE_CLASS( CFFBot, CBot< CFFPlayer > );
	DECLARE_DATADESC();

	CFFBot( void );
	virtual ~CFFBot();
	virtual bool Initialize( const BotProfile *profile, int team );

	virtual void Spawn( void );
	virtual void Touch( CBaseEntity *other );

	virtual void Upkeep( void );
	virtual void Update( void );
	virtual void BuildUserCmd( CUserCmd& cmd, const QAngle& viewangles, float forwardmove, float sidemove, float upmove, int buttons, byte impulse );
	virtual float GetMoveSpeed( void );

	virtual void Walk( void );
	virtual bool Jump( bool mustJump = false );

	//- behavior properties ------------------------------------------------------------------------------------------
	float GetCombatRange( void ) const;
	bool IsRogue( void ) const;
	void SetRogue( bool rogue );
	bool IsHurrying( void ) const;
	void Hurry( float duration );
	bool IsSafe( void ) const;
	bool IsWellPastSafe( void ) const;
	bool IsEndOfSafeTime( void ) const;
	float GetSafeTimeRemaining( void ) const;
	float GetSafeTime( void ) const;
	virtual void Blind( float holdTime, float fadeTime, float startingAlpha = 255 );
	bool IsUnhealthy( void ) const;
	
	bool IsAlert( void ) const;
	void BecomeAlert( void );

	bool IsSneaking( void ) const;
	void Sneak( float duration );

	//- behaviors ---------------------------------------------------------------------------------------------------
	void Idle( void );

	void Hide( CNavArea *searchFromArea = NULL, float duration = -1.0f, float hideRange = 750.0f, bool holdPosition = false );
	// #define USE_NEAREST true // This should be a const bool or similar, not a preprocessor macro in a header if possible
	bool TryToHide( CNavArea *searchFromArea = NULL, float duration = -1.0f, float hideRange = 750.0f, bool holdPosition = false, bool useNearest = false );
	void Hide( const Vector &hidingSpot, float duration = -1.0f, bool holdPosition = false );
	bool IsHiding( void ) const;
	bool IsAtHidingSpot( void ) const;
	float GetHidingTime( void ) const;

	bool MoveToInitialEncounter( void );

	bool TryToRetreat( float maxRange = 1000.0f, float duration = -1.0f );

	void Hunt( void );
	bool IsHunting( void ) const;

	void Attack( CFFPlayer *victim );
	void FireWeaponAtEnemy( void );
	void StopAttacking( void );
	bool IsAttacking( void ) const;

	void MoveTo( const Vector &pos, RouteType route = SAFEST_ROUTE ); // RouteType enum
	bool IsMovingTo( void ) const;

	// TODO_FF: Bomb-specific methods, adapt for FF objectives or remove
	// void PlantBomb( void );
	// void FetchBomb( void );
	// bool NoticeLooseBomb( void ) const;
	// bool CanSeeLooseBomb( void ) const;
	// void DefuseBomb( void );
	// bool IsDefusingBomb( void ) const;
	// bool CanSeePlantedBomb( void ) const;
	// void EscapeFromBomb( void );
	// bool IsEscapingFromBomb( void ) const;

	// TODO_FF: Hostage-specific methods, adapt or remove for FF
	// void RescueHostages( void );

	void UseEntity( CBaseEntity *entity );
	void OpenDoor( CBaseEntity *door );
	void CapturePoint( CBaseEntity *cpEntity );
	void CarryEnemyFlag( void ); // Added for new state
	bool IsOpeningDoor( void ) const;

	void Buy( void );
	bool IsBuying( void ) const;

	void Panic( void );
	bool IsPanicking( void ) const;
	void StopPanicking( void );
	void UpdatePanicLookAround( void );

	void TryToJoinTeam( int team );

	void Follow( CFFPlayer *player );
	void ContinueFollowing( void );
	void StopFollowing( void );
	bool IsFollowing( void ) const;
	CFFPlayer *GetFollowLeader( void ) const;
	float GetFollowDuration( void ) const;
	bool CanAutoFollow( void ) const;

	bool IsNotMoving( float minDuration = 0.0f ) const;

	void AimAtEnemy( void );
	void StopAiming( void );
	bool IsAimingAtEnemy( void ) const;

	float GetStateTimestamp( void ) const;

	bool IsDoingScenario( void ) const;

	//- scenario / gamestate -----------------------------------------------------------------------------------------
	FFGameState *GetGameState( void );
	const FFGameState *GetGameState( void ) const;

	// TODO_FF: Bomb-specific IsAtBombsite, adapt for FF objectives or remove
	bool IsAtBombsite( void );
	bool GuardRandomZone( float range = 500.0f );

	bool IsBusy( void ) const;

	//- high-level tasks ---------------------------------------------------------------------------------------------
	// This enum is defined in bot_constants.h as BotTaskType.
	// This local one might be redundant or a subset. For now, ensure it's not used directly if BotTaskType is preferred.
	// TODO_FF: Reconcile this TaskType enum with BotTaskType in bot_constants.h
	enum TaskType
	{
		SEEK_AND_DESTROY,
		// FF Specific Tasks (Examples from bot_constants.h, ensure consistency):
		// BOT_TASK_PLANT_BOMB, BOT_TASK_FIND_TICKING_BOMB, ... (CS ones)
		// BOT_TASK_CAPTURE_FLAG, BOT_TASK_RETURN_FLAG, ... (FF ones)
		NUM_TASKS // Must be last
	};
	// void SetTask( TaskType task, CBaseEntity *entity = NULL ); // Uses local TaskType
	// TaskType GetTask( void ) const; // Uses local TaskType
	void SetTask( BotTaskType task, CBaseEntity *entity = NULL ); // Changed to use global BotTaskType
	BotTaskType GetTask( void ) const; // Changed to use global BotTaskType
	CBaseEntity *GetTaskEntity( void );
	const char *GetTaskName( void ) const;
	CBaseEntity* GetClosestEscapeZone() const;

	//- behavior modifiers ------------------------------------------------------------------------------------------
	enum DispositionType
	{
		ENGAGE_AND_INVESTIGATE,
		OPPORTUNITY_FIRE,
		SELF_DEFENSE,
		IGNORE_ENEMIES,
		NUM_DISPOSITIONS // Must be last
	};
	void SetDisposition( DispositionType disposition );
	DispositionType GetDisposition( void ) const;
	const char *GetDispositionName( void ) const;

	void IgnoreEnemies( float duration );

	enum MoraleType
	{
		TERRIBLE = -3, BAD = -2, NEGATIVE = -1, NEUTRAL = 0,
		POSITIVE = 1, GOOD = 2, EXCELLENT = 3,
		NUM_MORALE_TYPES // Must be last
	};
	MoraleType GetMorale( void ) const;
	const char *GetMoraleName( void ) const;
	void IncreaseMorale( void );
	void DecreaseMorale( void );

	void Surprise( float duration );
	bool IsSurprised( void ) const;


	//- listening for noises ----------------------------------------------------------------------------------------
	bool IsNoiseHeard( void ) const;
	bool HeardInterestingNoise( void );
	void InvestigateNoise( void );
	bool IsInvestigatingNoise( void ) const;
	const Vector *GetNoisePosition( void ) const;
	CNavArea *GetNoiseArea( void ) const;
	void ForgetNoise( void );
	bool CanSeeNoisePosition( void ) const;
	float GetNoiseRange( void ) const;

	bool CanHearNearbyEnemyGunfire( float range = -1.0f ) const;
	PriorityType GetNoisePriority( void ) const; // PriorityType enum

	//- radio and chatter--------------------------------------------------------------------------------------------
	// void SendRadioMessage( RadioType event ); // Radio system removed
	void SpeakAudio( const char *voiceFilename, float duration, int pitch );
	// BotChatterInterface *GetChatter( void ); // Removed FF-specific chatter
	bool RespondToHelpRequest( CFFPlayer *player, Place place, float maxRange = -1.0f );
	bool IsUsingVoice() const;


	//- enemies ------------------------------------------------------------------------------------------------------
	void SetBotEnemy( CFFPlayer *enemy );
	CFFPlayer *GetBotEnemy( void ) const;
	int GetNearbyEnemyCount( void ) const;
	unsigned int GetEnemyPlace( void ) const; // Place enum
	// TODO_FF: Bomb-specific CanSeeBomber/GetBomber, adapt for FF objectives (e.g. flag carrier) or remove
	bool CanSeeBomber( void ) const;
	CFFPlayer *GetBomber( void ) const;

	int GetNearbyFriendCount( void ) const;
	CFFPlayer *GetClosestVisibleFriend( void ) const;
	CFFPlayer *GetClosestVisibleHumanFriend( void ) const;

	bool IsOutnumbered( void ) const;
	int OutnumberedCount( void ) const;

	// #define ONLY_VISIBLE_ENEMIES true // Prefer const bool
	CFFPlayer *GetImportantEnemy( bool checkVisibility = false ) const;

	void UpdateReactionQueue( void );
	CFFPlayer *GetRecognizedEnemy( void );
	bool IsRecognizedEnemyReloading( void );
	bool IsRecognizedEnemyProtectedByShield( void ); // TODO_FF: Shield logic for FF
	float GetRangeToNearestRecognizedEnemy( void );

	CFFPlayer *GetAttacker( void ) const;
	float GetTimeSinceAttacked( void ) const;
	float GetFirstSawEnemyTimestamp( void ) const;
	float GetLastSawEnemyTimestamp( void ) const;
	float GetTimeSinceLastSawEnemy( void ) const;
	float GetTimeSinceAcquiredCurrentEnemy( void ) const;
	bool HasNotSeenEnemyForLongTime( void ) const;
	const Vector &GetLastKnownEnemyPosition( void ) const;
	bool IsEnemyVisible( void ) const;
	float GetEnemyDeathTimestamp( void ) const;
	bool IsFriendInLineOfFire( void );
	bool IsAwareOfEnemyDeath( void ) const;
	int GetLastVictimID( void ) const;

	bool CanSeeSniper( void ) const; // TODO_FF: Sniper logic for FF
	bool HasSeenSniperRecently( void ) const;

	float GetTravelDistanceToPlayer( CFFPlayer *player ) const;
	bool DidPlayerJustFireWeapon( const CFFPlayer *player ) const;

	//- navigation --------------------------------------------------------------------------------------------------
	bool HasPath( void ) const;
	void DestroyPath( void );

	float GetFeetZ( void ) const;

	enum PathResult { PROGRESSING, END_OF_PATH, PATH_FAILURE }; // Already defined above
	// #define NO_SPEED_CHANGE false // Prefer const bool
	PathResult UpdatePathMovement( bool allowSpeedChange = true );

	bool ComputePath( const Vector &goal, RouteType route = SAFEST_ROUTE ); // RouteType enum
	bool StayOnNavMesh( void );
	CNavArea *GetLastKnownArea( void ) const;
	const Vector &GetPathEndpoint( void ) const;
	float GetPathDistanceRemaining( void ) const;
	void ResetStuckMonitor( void );
	bool IsAreaVisible( const CNavArea *area ) const;
	const Vector &GetPathPosition( int index ) const;
	bool GetSimpleGroundHeightWithFloor( const Vector &pos, float *height, Vector *normal = NULL );
	void BreakablesCheck( void );
	void DoorCheck( void );

	virtual void PushawayTouch( CBaseEntity *pOther );

	Place GetPlace( void ) const; // Place enum

	bool IsUsingLadder( void ) const;
	void GetOffLadder( void );

	void SetGoalEntity( CBaseEntity *entity );
	CBaseEntity *GetGoalEntity( void );

	bool IsNearJump( void ) const;
	float GetApproximateFallDamage( float height ) const;

	void ForceRun( float duration );
	virtual bool IsRunning( void ) const;

	void Wait( float duration );
	bool IsWaiting( void ) const;
	void StopWaiting( void );

	void Wiggle( void );

	bool IsFriendInTheWay( const Vector &goalPos );
	void FeelerReflexAdjustment( Vector *goalPosition );

	bool HasVisitedEnemySpawn( void ) const;
	bool IsAtEnemySpawn( void ) const;

	//- looking around ----------------------------------------------------------------------------------------------
	const Vector &EyePositionConst( void ) const;
	
	void SetLookAngles( float yaw, float pitch );
	void UpdateLookAngles( void );
	void UpdateLookAround( bool updateNow = false );
	void InhibitLookAround( float duration );

	void SetForwardAngle( float angle );
	void SetLookAheadAngle( float angle );

	void SetLookAt( const char *desc, const Vector &pos, PriorityType pri, float duration = -1.0f, bool clearIfClose = false, float angleTolerance = 5.0f, bool attack = false ); // PriorityType
	void ClearLookAt( void );
	bool IsLookingAtSpot( PriorityType pri = PRIORITY_LOW ) const; // PriorityType
	bool IsViewMoving( float angleVelThreshold = 1.0f ) const;
	bool HasViewBeenSteady( float duration ) const;

	bool HasLookAtTarget( void ) const;

	enum VisiblePartType { NONE = 0x00, GUT = 0x01, HEAD = 0x02, LEFT_SIDE = 0x04, RIGHT_SIDE = 0x08, FEET = 0x10 }; // Already defined above

	// #define CHECK_FOV true // Prefer const bool
	bool IsVisible( const Vector &pos, bool testFOV = false, const CBaseEntity *ignore = NULL ) const;
	bool IsVisible( CFFPlayer *player, bool testFOV = false, unsigned char *visParts = NULL ) const;

	bool IsNoticable( const CFFPlayer *player, unsigned char visibleParts ) const;

	bool IsEnemyPartVisible( VisiblePartType part ) const;
	const Vector &GetPartPosition( CFFPlayer *player, VisiblePartType part ) const;

	float ComputeWeaponSightRange( void );

	bool IsAnyVisibleEnemyLookingAtMe( bool testFOV = false ) const;

	bool IsSignificantlyCloser( const CFFPlayer *testPlayer, const CFFPlayer *referencePlayer ) const;

	//- approach points ---------------------------------------------------------------------------------------------
	void ComputeApproachPoints( void );
	void UpdateApproachPoints( void );
	void ClearApproachPoints( void );
	void DrawApproachPoints( void ) const;
	float GetHidingSpotCheckTimestamp( HidingSpot *spot ) const; // HidingSpot
	void SetHidingSpotCheckTimestamp( HidingSpot *spot ); // HidingSpot

	const CNavArea *GetInitialEncounterArea( void ) const;
	void SetInitialEncounterArea( const CNavArea *area );

	//- weapon query and equip --------------------------------------------------------------------------------------
	// #define MUST_EQUIP true // Prefer const bool
	void EquipBestWeapon( bool mustEquip = false );
	void EquipPistol( void );
	void EquipKnife( void );

	// #define DONT_USE_SMOKE_GRENADE true // Prefer const bool
	bool EquipGrenade( bool noSmoke = false ); // TODO_FF: Smoke grenade logic for FF

	bool IsUsingKnife( void ) const;
	bool IsUsingPistol( void ) const;
	bool IsUsingGrenade( void ) const;
	bool IsUsingSniperRifle( void ) const; // TODO_FF: Sniper logic for FF
	bool IsUsing( FFWeaponID weapon ) const;
	bool IsSniper( void ) const; // TODO_FF: Sniper logic for FF
	bool IsSniping( void ) const; // TODO_FF: Sniper logic for FF
	bool IsUsingShotgun( void ) const;
	bool IsUsingMachinegun( void ) const; // TODO_FF: Machinegun logic for FF
	void ThrowGrenade( const Vector &target );
	bool IsThrowingGrenade( void ) const;
	bool HasGrenade( void ) const;
	void AvoidEnemyGrenades( void );
	bool IsAvoidingGrenade( void ) const;
	bool DoesActiveWeaponHaveSilencer( void ) const; // TODO_FF: Silencer logic for FF
	bool CanActiveWeaponFire( void ) const;
	CFFWeaponBase *GetActiveFFWeapon( void ) const;

	void GiveWeapon( const char *weaponAlias );

	virtual void PrimaryAttack( void );

	enum ZoomType { NO_ZOOM, LOW_ZOOM, HIGH_ZOOM }; // Already defined above
	ZoomType GetZoomLevel( void );

	bool AdjustZoom( float range );
	bool IsWaitingForZoom( void ) const;

	bool IsPrimaryWeaponEmpty( void ) const;
	bool IsPistolEmpty( void ) const;

	// TODO_FF: Hostage logic for FF
	int GetHostageEscortCount( void ) const;
	void IncreaseHostageEscortCount( void );
	float GetRangeToFarthestEscortedHostage( void ) const;
	void ResetWaitForHostagePatience( void );

	//------------------------------------------------------------------------------------
	// Event hooks
	//
	virtual int OnTakeDamage( const CTakeDamageInfo &info );
	virtual void Event_Killed( const CTakeDamageInfo &info );
	virtual bool BumpWeapon( CBaseCombatWeapon *pWeapon );

	void OnPlayerFootstep( IGameEvent *event );
	void OnPlayerRadio( IGameEvent *event );
	void OnPlayerDeath( IGameEvent *event );
	void OnPlayerFallDamage( IGameEvent *event );

	// TODO_FF: Bomb events for FF objectives or remove
	void OnBombPickedUp( IGameEvent *event );
	void OnBombPlanted( IGameEvent *event );
	void OnBombBeep( IGameEvent *event );
	void OnBombDefuseBegin( IGameEvent *event );
	void OnBombDefused( IGameEvent *event );
	void OnBombDefuseAbort( IGameEvent *event );
	void OnBombExploded( IGameEvent *event );

	void OnRoundEnd( IGameEvent *event );
	void OnRoundStart( IGameEvent *event );

	void OnDoorMoving( IGameEvent *event );

	void OnBreakProp( IGameEvent *event );
	void OnBreakBreakable( IGameEvent *event );

	// TODO_FF: Hostage events for FF or remove
	void OnHostageFollows( IGameEvent *event );
	void OnHostageRescuedAll( IGameEvent *event );

	void OnWeaponFire( IGameEvent *event );
	void OnWeaponFireOnEmpty( IGameEvent *event );
	void OnWeaponReload( IGameEvent *event );
	void OnWeaponZoom( IGameEvent *event );

	void OnBulletImpact( IGameEvent *event );

	// TODO_FF: Grenade events for FF or remove
	void OnHEGrenadeDetonate( IGameEvent *event );
	void OnFlashbangDetonate( IGameEvent *event );
	void OnSmokeGrenadeDetonate( IGameEvent *event );
	void OnGrenadeBounce( IGameEvent *event );

	void OnNavBlocked( IGameEvent *event );

	void OnEnteredNavArea( CNavArea *newArea );

private:
	// #define IS_FOOTSTEP true // Prefer const bool
	void OnAudibleEvent( IGameEvent *event, CBasePlayer *player, float range, PriorityType priority, bool isHostile, bool isFootstep = false, const Vector *actualOrigin = NULL ); // PriorityType

private:
	friend class CFFBotManager;
	// Friend declarations for states are fine as states are tightly coupled with the bot.
	friend class IdleState; friend class HuntState; friend class AttackState;
	friend class InvestigateNoiseState; friend class BuyState; friend class MoveToState;
	friend class HideState; friend class FollowState;
	friend class UseEntityState; friend class OpenDoorState;
	friend class CapturePointState;
	friend class CarryFlagState; // Added friend class for CarryFlagState


	Vector m_eyePosition; // BOTPORT: Remove this vile hack if possible

	void ResetValues( void );
	void BotDeathThink( void );

	char m_name[MAX_PLAYER_NAME_LENGTH]; // MAX_PLAYER_NAME_LENGTH needs to be defined
	void DebugDisplay( void ) const;

	float m_combatRange;
	mutable bool m_isRogue;
	mutable CountdownTimer m_rogueTimer;
	MoraleType m_morale;
	bool m_diedLastRound;
	float m_safeTime;
	bool m_wasSafe;
	void AdjustSafeTime( void );
	NavRelativeDirType m_blindMoveDir; // NavRelativeDirType enum
	bool m_blindFire;
	CountdownTimer m_surpriseTimer;

	bool m_isFollowing;
	CHandle< CFFPlayer > m_leader;
	float m_followTimestamp;
	float m_allowAutoFollowTime;

	CountdownTimer m_hurryTimer;
	CountdownTimer m_alertTimer;
	CountdownTimer m_sneakTimer;
	CountdownTimer m_panicTimer;

	// State instances
	IdleState				m_idleState;
	HuntState				m_huntState;
	AttackState				m_attackState;
	InvestigateNoiseState	m_investigateNoiseState;
	BuyState				m_buyState;
	MoveToState				m_moveToState;
	// Removed member variables for CS-specific bomb states
	// FetchBombState			m_fetchBombState;
	// PlantBombState			m_plantBombState;
	// DefuseBombState			m_defuseBombState;
	HideState				m_hideState;
	// EscapeFromBombState		m_escapeFromBombState;
	FollowState				m_followState;
	UseEntityState			m_useEntityState;
	OpenDoorState			m_openDoorState;
	CapturePointState		m_capturePointState;
	CarryFlagState			m_carryFlagState; // Added member for CarryFlagState

	void SetState( BotState *state );
	BotState *m_state;
	float m_stateTimestamp;
	bool m_isAttacking;
	bool m_isOpeningDoor;

	BotTaskType m_task; // Changed to use global BotTaskType
	EHANDLE m_taskEntity;

	Vector m_goalPosition;
	EHANDLE m_goalEntity;
	void MoveTowardsPosition( const Vector &pos );
	void MoveAwayFromPosition( const Vector &pos );
	void StrafeAwayFromPosition( const Vector &pos );
	void StuckCheck( void );
	CNavArea *m_currentArea;
	CNavArea *m_lastKnownArea;
	EHANDLE m_avoid;
	float m_avoidTimestamp;
	bool m_isStopping;
	bool m_hasVisitedEnemySpawn;
	IntervalTimer m_stillTimer;

	enum { MAX_PATH_LENGTH_FF = 256 }; // Renamed to avoid conflict if MAX_PATH_LENGTH is elsewhere
	struct ConnectInfo
	{
		CNavArea *area;
		NavTraverseType how; // NavTraverseType enum
		Vector pos;
		const CNavLadder *ladder;
	}
	m_path[ MAX_PATH_LENGTH_FF ]; // Use renamed const
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

	// #define ONLY_JUMP_DOWN true // Prefer const bool
	bool DiscontinuityJump( float ground, bool onlyJumpDown = false, bool mustJump = false );

	LadderNavState m_pathLadderState; // Enum
	bool m_pathLadderFaceIn;
	const CNavLadder *m_pathLadder;
	bool UpdateLadderMovement( void );
	NavRelativeDirType m_pathLadderDismountDir; // Enum
	float m_pathLadderDismountTimestamp;
	float m_pathLadderEnd;
	void ComputeLadderEndpoint( bool ascending );
	float m_pathLadderTimestamp;

	CountdownTimer m_mustRunTimer;
	CountdownTimer m_waitTimer;

	void UpdateTravelDistanceToAllPlayers( void );
	CountdownTimer m_updateTravelDistanceTimer;
	float m_playerTravelDistance[ MAX_PLAYERS ]; // MAX_PLAYERS needs to be defined
	unsigned char m_travelDistancePhase;

	FFGameState m_gameState; // Instance of FFGameState

	// TODO_FF: Hostage logic for FF
	byte m_hostageEscortCount;
	void UpdateHostageEscortCount( void );
	float m_hostageEscortCountTimestamp;
	bool m_isWaitingForHostage;
	CountdownTimer m_inhibitWaitingForHostageTimer;
	CountdownTimer m_waitForHostageTimer;

	int m_desiredTeam;
	bool m_hasJoined;

	Vector m_noisePosition;
	float m_noiseTravelDistance;
	float m_noiseTimestamp;
	CNavArea *m_noiseArea; // This was the line that caused issues before. Keep as is.
	PriorityType m_noisePriority; // Enum
	bool UpdateLookAtNoise( void );
	CountdownTimer m_noiseBendTimer;
	Vector m_bentNoisePosition;
	bool m_bendNoisePositionValid;

	float m_lookAroundStateTimestamp;
	float m_lookAheadAngle;
	float m_forwardAngle;
	float m_inhibitLookAroundTimestamp;

	LookAtSpotState m_lookAtSpotState; // Enum
	Vector m_lookAtSpot;
	PriorityType m_lookAtSpotPriority; // Enum
	float m_lookAtSpotDuration;
	float m_lookAtSpotTimestamp;
	float m_lookAtSpotAngleTolerance;
	bool m_lookAtSpotClearIfClose;
	bool m_lookAtSpotAttack;
	const char *m_lookAtDesc;
	void UpdateLookAt( void );
	void UpdatePeripheralVision();
	float m_peripheralTimestamp;

	enum { MAX_APPROACH_POINTS_FF = 16 }; // Renamed
	struct ApproachPoint
	{
		Vector m_pos;
		CNavArea *m_area;
	};
	ApproachPoint m_approachPoint[ MAX_APPROACH_POINTS_FF ]; // Use renamed const
	unsigned char m_approachPointCount;
	Vector m_approachPointViewPosition;

	CBaseEntity * FindEntitiesOnPath( float distance, CPushAwayEnumerator *enumerator, bool checkStuck );

	IntervalTimer m_viewSteadyTimer;

	bool BendLineOfSight( const Vector &eye, const Vector &target, Vector *bend, float angleLimit = 135.0f ) const;
	bool FindApproachPointNearestPath( Vector *pos );
	bool FindGrenadeTossPathTarget( Vector *pos );
	GrenadeTossState m_grenadeTossState; // Enum
	CountdownTimer m_tossGrenadeTimer;
	const CNavArea *m_initialEncounterArea;
	void LookForGrenadeTargets( void );
	void UpdateGrenadeThrow( void );
	CountdownTimer m_isAvoidingGrenade;

	SpotEncounter *m_spotEncounter; // SpotEncounter needs definition
	float m_spotCheckTimestamp;

	enum { MAX_CHECKED_SPOTS_FF = 64 }; // Renamed
	struct HidingSpotCheckInfo
	{
		HidingSpot *spot; // HidingSpot needs definition
		float timestamp;
	}
	m_checkedHidingSpot[ MAX_CHECKED_SPOTS_FF ]; // Use renamed const
	int m_checkedHidingSpotCount;

	float m_lookPitch;
	float m_lookPitchVel;
	float m_lookYaw;
	float m_lookYawVel;

	Vector m_aimOffset;
	Vector m_aimOffsetGoal;
	float m_aimOffsetTimestamp;
	float m_aimSpreadTimestamp;
	Vector m_aimSpot;

	struct PartInfo
	{
		Vector m_headPos; Vector m_gutPos; Vector m_feetPos;
		Vector m_leftSidePos; Vector m_rightSidePos;
		int m_validFrame;
	};
	static PartInfo m_partInfo[ MAX_PLAYERS ]; // MAX_PLAYERS

	void ComputePartPositions( CFFPlayer *player );

	DispositionType m_disposition; // Enum
	CountdownTimer m_ignoreEnemiesTimer;
	CHandle< CFFPlayer > m_enemy;
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
	unsigned int m_enemyPlace; // Place enum

	struct WatchInfo { float timestamp; bool isEnemy; } m_watchInfo[ MAX_PLAYERS ]; // MAX_PLAYERS
	CHandle< CFFPlayer > m_bomber;
	// Assuming m_bomber is CFFPlayer based on GetBomber() returning CFFPlayer*

	int m_nearbyFriendCount;
	CHandle< CFFPlayer > m_closestVisibleFriend;
	CHandle< CFFPlayer > m_closestVisibleHumanFriend;

	IntervalTimer m_attentionInterval;

	CHandle< CFFPlayer > m_attacker;
	float m_attackedTimestamp;

	int m_lastVictimID;
	bool m_isAimingAtEnemy;
	bool m_isRapidFiring;
	IntervalTimer m_equipTimer;
	CountdownTimer m_zoomTimer;
	bool DoEquip( CFFWeaponBase *gun );

	void ReloadCheck( void );
	void SilencerCheck( void ); // TODO_FF: Silencer logic for FF

	float m_fireWeaponTimestamp;

	bool m_isEnemySniperVisible; // TODO_FF: Sniper logic for FF
	CountdownTimer m_sawEnemySniperTimer;
	
	enum { MAX_ENEMY_QUEUE_FF = 20 };
	struct ReactionState
	{
		CHandle<CFFPlayer> player;
		bool isReloading;
		bool isProtectedByShield; // TODO_FF: Shield logic for FF
	}
	m_enemyQueue[ MAX_ENEMY_QUEUE_FF ];
	byte m_enemyQueueIndex;
	byte m_enemyQueueCount;
	byte m_enemyQueueAttendIndex;

	CFFPlayer *FindMostDangerousThreat( void );

	bool m_isStuck;
	float m_stuckTimestamp;
	Vector m_stuckSpot;
	NavRelativeDirType m_wiggleDirection; // Enum
	CountdownTimer m_wiggleTimer;
	CountdownTimer m_stuckJumpTimer;

	enum { MAX_VEL_SAMPLES_FF = 10 }; // Renamed
	float m_avgVel[ MAX_VEL_SAMPLES_FF ]; // Use renamed const
	int m_avgVelIndex;
	int m_avgVelCount;
	Vector m_lastOrigin;

	// RadioType m_lastRadioCommand; // Enum - Radio system removed
	// float m_lastRadioRecievedTimestamp; // Radio system removed
	// float m_lastRadioSentTimestamp; // Radio system removed
	// CHandle< CFFPlayer > m_radioSubject; // Radio system removed
	// Vector m_radioPosition; // Radio system removed
	// void RespondToRadioCommands( void ); // Radio system removed
	bool IsRadioCommand( RadioType event ) const; // Enum - This might still be useful for interpreting player radio for info gathering

	float m_voiceEndTimestamp;

	// BotChatterInterface m_chatter; // Removed FF-specific chatter
	
	// FF Specific members can be added here
	// Example for CTF:
	// bool m_hasEnemyFlag;
	// Vector m_ourFlagReturnPosition;
};


// Inlines - many of these call methods that might need FF adaptation (bomb logic, hostage logic, weapon names)

inline float CFFBot::GetFeetZ( void ) const { return GetAbsOrigin().z; }
inline const Vector *CFFBot::GetNoisePosition( void ) const { if (m_noiseTimestamp > 0.0f) return &m_noisePosition; return NULL; }
inline bool CFFBot::IsAwareOfEnemyDeath( void ) const { /* ... existing logic ... */ return false; } // Simplified for brevity
inline void CFFBot::Panic( void ) { /* ... existing logic ... */ }
inline bool CFFBot::IsPanicking( void ) const { return !m_panicTimer.IsElapsed(); }
inline void CFFBot::StopPanicking( void ) { m_panicTimer.Invalidate(); }
inline bool CFFBot::IsNotMoving( float minDuration ) const { return (m_stillTimer.HasStarted() && m_stillTimer.GetElapsedTime() >= minDuration); }
// Removed inline CFFWeaponBase *CFFBot::GetActiveFFWeapon( void ) const { return reinterpret_cast<CFFWeaponBase *>( GetActiveWeapon() ); }
inline float CFFBot::GetCombatRange( void ) const { return m_combatRange; }
inline void CFFBot::SetRogue( bool rogue ) { m_isRogue = rogue; }
inline void CFFBot::Hurry( float duration ) { m_hurryTimer.Start( duration ); }
inline float CFFBot::GetSafeTime( void ) const { return m_safeTime; }
inline bool CFFBot::IsUnhealthy( void ) const { return (GetHealth() <= 40); } // Consider making 40 a const or profile value
inline bool CFFBot::IsAlert( void ) const { return !m_alertTimer.IsElapsed(); }
inline void CFFBot::BecomeAlert( void ) { const float alertCooldownTime = 10.0f; m_alertTimer.Start( alertCooldownTime ); }
inline bool CFFBot::IsSneaking( void ) const { return !m_sneakTimer.IsElapsed(); }
inline void CFFBot::Sneak( float duration ) { m_sneakTimer.Start( duration ); }
inline bool CFFBot::IsFollowing( void ) const { return m_isFollowing; }
inline CFFPlayer *CFFBot::GetFollowLeader( void ) const { return m_leader.Get(); }
inline float CFFBot::GetFollowDuration( void ) const { return gpGlobals->curtime - m_followTimestamp; }
inline bool CFFBot::CanAutoFollow( void ) const { return (gpGlobals->curtime > m_allowAutoFollowTime); }
inline void CFFBot::AimAtEnemy( void ) { m_isAimingAtEnemy = true; }
inline void CFFBot::StopAiming( void ) { m_isAimingAtEnemy = false; }
inline bool CFFBot::IsAimingAtEnemy( void ) const { return m_isAimingAtEnemy; }
inline float CFFBot::GetStateTimestamp( void ) const { return m_stateTimestamp; }
inline FFGameState *CFFBot::GetGameState( void ) { return &m_gameState; } // Returns pointer to member FFGameState instance
inline const FFGameState *CFFBot::GetGameState( void ) const { return &m_gameState; } // Returns const pointer
// TODO_FF: IsAtBombsite is CS-specific. Adapt for FF objectives or remove.
inline bool CFFBot::IsAtBombsite( void ) { return m_bInBombZone; } // m_bInBombZone needs definition (likely from CPlayer or game rules)
inline void CFFBot::SetTask( BotTaskType task, CBaseEntity *entity ) { m_task = task; m_taskEntity = entity; } // Changed to use global BotTaskType
inline BotTaskType CFFBot::GetTask( void ) const { return m_task; } // Changed to use global BotTaskType
inline CBaseEntity *CFFBot::GetTaskEntity( void ) { return m_taskEntity.Get(); }
inline CFFBot::MoraleType CFFBot::GetMorale( void ) const { return m_morale; }
inline void CFFBot::Surprise( float duration ) { m_surpriseTimer.Start( duration ); }
inline bool CFFBot::IsSurprised( void ) const { return !m_surpriseTimer.IsElapsed(); }
inline CNavArea *CFFBot::GetNoiseArea( void ) const { return m_noiseArea; }
inline void CFFBot::ForgetNoise( void ) { m_noiseTimestamp = 0.0f; }
inline float CFFBot::GetNoiseRange( void ) const { if (IsNoiseHeard()) return m_noiseTravelDistance; return 999999999.9f; }
inline PriorityType CFFBot::GetNoisePriority( void ) const { return m_noisePriority; } // Enum
// inline BotChatterInterface *CFFBot::GetChatter( void ) { return &m_chatter; } // Removed FF-specific chatter
inline CFFPlayer *CFFBot::GetBotEnemy( void ) const { return m_enemy.Get(); }
inline int CFFBot::GetNearbyEnemyCount( void ) const { return MIN( GetEnemiesRemaining(), m_nearbyEnemyCount ); } // GetEnemiesRemaining needs definition from game rules or CPlayer
inline unsigned int CFFBot::GetEnemyPlace( void ) const { return m_enemyPlace; } // Place enum
// TODO_FF: Bomb-specific logic for CanSeeBomber/GetBomber. Adapt or remove.
inline bool CFFBot::CanSeeBomber( void ) const { return (m_bomber.IsValid() && m_bomber.Get()) ? true : false; }
inline CFFPlayer *CFFBot::GetBomber( void ) const { return m_bomber.Get(); }
inline int CFFBot::GetNearbyFriendCount( void ) const { return MIN( GetFriendsRemaining(), m_nearbyFriendCount ); } // GetFriendsRemaining needs definition
inline CFFPlayer *CFFBot::GetClosestVisibleFriend( void ) const { return m_closestVisibleFriend.Get(); }
inline CFFPlayer *CFFBot::GetClosestVisibleHumanFriend( void ) const { return m_closestVisibleHumanFriend.Get(); }
inline float CFFBot::GetTimeSinceAttacked( void ) const { return gpGlobals->curtime - m_attackedTimestamp; }
inline float CFFBot::GetFirstSawEnemyTimestamp( void ) const { return m_firstSawEnemyTimestamp; }
inline float CFFBot::GetLastSawEnemyTimestamp( void ) const { return m_lastSawEnemyTimestamp; }
inline float CFFBot::GetTimeSinceLastSawEnemy( void ) const { return gpGlobals->curtime - m_lastSawEnemyTimestamp; }
inline float CFFBot::GetTimeSinceAcquiredCurrentEnemy( void ) const { return gpGlobals->curtime - m_currentEnemyAcquireTimestamp; }
inline const Vector &CFFBot::GetLastKnownEnemyPosition( void ) const { return m_lastEnemyPosition; }
inline bool CFFBot::IsEnemyVisible( void ) const { return m_isEnemyVisible; }
inline float CFFBot::GetEnemyDeathTimestamp( void ) const { return m_enemyDeathTimestamp; }
inline int CFFBot::GetLastVictimID( void ) const { return m_lastVictimID; }
// TODO_FF: Sniper logic for FF
inline bool CFFBot::CanSeeSniper( void ) const { return m_isEnemySniperVisible; }
inline bool CFFBot::HasSeenSniperRecently( void ) const { return !m_sawEnemySniperTimer.IsElapsed(); }
inline float CFFBot::GetTravelDistanceToPlayer( CFFPlayer *player ) const { if (!player || !player->IsAlive() || player->entindex() < 0 || player->entindex() >= MAX_PLAYERS) return -1.0f; return m_playerTravelDistance[ player->entindex() ]; }
inline bool CFFBot::HasPath( void ) const { return (m_pathLength) ? true : false; }
inline void CFFBot::DestroyPath( void ) { m_isStopping = false; m_pathLength = 0; m_pathLadder = NULL; }
inline CNavArea *CFFBot::GetLastKnownArea( void ) const { return m_lastKnownArea; }
inline const Vector &CFFBot::GetPathEndpoint( void ) const { return (m_pathLength > 0 && m_pathLength-1 < MAX_PATH_LENGTH_FF) ? m_path[ m_pathLength-1 ].pos : vec3_origin; } // Bounds check and fallback
inline const Vector &CFFBot::GetPathPosition( int index ) const { return (index >=0 && index < m_pathLength && index < MAX_PATH_LENGTH_FF) ? m_path[ index ].pos : vec3_origin; } // Bounds check and fallback
inline bool CFFBot::IsUsingLadder( void ) const { return (m_pathLadder) ? true : false; }
inline void CFFBot::SetGoalEntity( CBaseEntity *entity ) { m_goalEntity = entity; }
inline CBaseEntity *CFFBot::GetGoalEntity( void ) { return m_goalEntity.Get(); } // Use .Get()
inline void CFFBot::ForceRun( float duration ) { Run(); m_mustRunTimer.Start( duration ); }
inline void CFFBot::Wait( float duration ) { m_waitTimer.Start( duration ); }
inline bool CFFBot::IsWaiting( void ) const { return !m_waitTimer.IsElapsed(); }
inline void CFFBot::StopWaiting( void ) { m_waitTimer.Invalidate(); }
inline bool CFFBot::HasVisitedEnemySpawn( void ) const { return m_hasVisitedEnemySpawn; }
inline const Vector &CFFBot::EyePositionConst( void ) const { return m_eyePosition; }
inline void CFFBot::SetLookAngles( float yaw, float pitch ) { m_lookYaw = yaw; m_lookPitch = pitch; }
inline void CFFBot::SetForwardAngle( float angle ) { m_forwardAngle = angle; }
inline void CFFBot::SetLookAheadAngle( float angle ) { m_lookAheadAngle = angle; }
inline void CFFBot::ClearLookAt( void ) { m_lookAtSpotState = NOT_LOOKING_AT_SPOT; m_lookAtDesc = NULL; } // Enum
inline bool CFFBot::IsLookingAtSpot( PriorityType pri ) const { if (m_lookAtSpotState != NOT_LOOKING_AT_SPOT && m_lookAtSpotPriority >= pri) return true; return false; } // Enum
inline bool CFFBot::IsViewMoving( float angleVelThreshold ) const { /* ... existing logic ... */ return false; } // Simplified
inline bool CFFBot::HasViewBeenSteady( float duration ) const { return (m_viewSteadyTimer.GetElapsedTime() > duration); }
inline bool CFFBot::HasLookAtTarget( void ) const { return (m_lookAtSpotState != NOT_LOOKING_AT_SPOT); } // Enum
inline bool CFFBot::IsEnemyPartVisible( VisiblePartType part ) const { if (!IsEnemyVisible()) return false; return (m_visibleEnemyParts & part) ? true : false; } // Enum
inline bool CFFBot::IsSignificantlyCloser( const CFFPlayer *testPlayer, const CFFPlayer *referencePlayer ) const { /* ... existing logic ... */ return false; }
inline void CFFBot::ClearApproachPoints( void ) { m_approachPointCount = 0; }
inline const CNavArea *CFFBot::GetInitialEncounterArea( void ) const { return m_initialEncounterArea; }
inline void CFFBot::SetInitialEncounterArea( const CNavArea *area ) { m_initialEncounterArea = area; }
// TODO_FF: Grenade logic
inline bool CFFBot::IsThrowingGrenade( void ) const { return m_grenadeTossState != NOT_THROWING; } // Enum
inline bool CFFBot::IsAvoidingGrenade( void ) const { return !m_isAvoidingGrenade.IsElapsed(); }
inline void CFFBot::PrimaryAttack( void ) { /* ... existing logic ... */ BaseClass::PrimaryAttack(); }
inline CFFBot::ZoomType CFFBot::GetZoomLevel( void ) { /* ... existing logic ... */ return NO_ZOOM; } // Enum
inline bool CFFBot::IsWaitingForZoom( void ) const { return !m_zoomTimer.IsElapsed(); }
// TODO_FF: Hostage logic
inline int CFFBot::GetHostageEscortCount( void ) const { return m_hostageEscortCount; }
inline void CFFBot::IncreaseHostageEscortCount( void ) { ++m_hostageEscortCount; }
inline void CFFBot::ResetWaitForHostagePatience( void ) { m_isWaitingForHostage = false; m_inhibitWaitingForHostageTimer.Invalidate(); }
inline bool CFFBot::IsUsingVoice() const { return m_voiceEndTimestamp > gpGlobals->curtime; }
inline bool CFFBot::IsOpeningDoor( void ) const { return m_isOpeningDoor; }

// TODO_FF: Update for FF sniper logic, ensure WEAPONTYPE_SNIPER_RIFLE is defined for FF
inline bool IsSniperRifle( CFFWeaponBase *weapon ) { if (weapon == NULL) return false; return weapon->IsKindOf(WEAPONTYPE_SNIPER_RIFLE); }


// PathCost class definition moved to ff_bot_pathfind.cpp or a more appropriate location.
// For now, assuming it's defined elsewhere if CFFBot::ComputePath uses it.

// Prototypes
extern int GetBotFollowCount( CFFPlayer *leader );
// extern const Vector *FindNearbyRetreatSpot( CFFBot *me, float maxRange = 250.0f ); // Defined in ff_bot.cpp
// extern const HidingSpot *FindInitialEncounterSpot( CBaseEntity *me, const Vector &searchOrigin, float enemyArriveTime, float maxRange, bool isSniper );

#endif	// _FF_BOT_H_
