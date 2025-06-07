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

#ifndef BOT_H
#define BOT_H

#include "cbase.h"
#include "in_buttons.h"
#include "movehelper_server.h"
#include "mathlib/mathlib.h"

#include "bot_manager.h" // Assumed to be ff_bot_manager.h via include paths
#include "bot_util.h"
#include "bot_constants.h"
#include "nav_mesh.h"
#include "gameinterface.h"
// #include "weapon_csbase.h" // CS-specific, FF will use CBasePlayerWeapon or specific FF weapon headers
#include "ff_weapon_base.h" // FF_WEAPONS: Added for CFFWeaponBase and FFWeaponID
#include "shared_util.h"
#include "util.h"
#include "shareddefs.h" // Contains general team defs like TEAM_UNASSIGNED, TEAM_SPECTATOR
#include "../../server/ff/ff_player.h" // Added CFFPlayer include
#include "../../server/ff/ff_shareddefs.h" // For FF_TEAM_* if defined there, or ff_bot_manager.h for now

#include "tier0/vprof.h"

class BotProfile;
class CFFPlayer; // Forward declaration
class CFFBot;   // Forward declaration
class CFFWeaponBase; // FF_WEAPONS: Forward declaration


extern bool AreBotsAllowed();


//--------------------------------------------------------------------------------------------------------
// BOTPORT: Convert everything to assume "origin" means "feet"

//
// Utility function to get "centroid" or center of player or player equivalent
//
inline Vector GetCentroid( const CBaseEntity *player )
{
	Vector centroid = player->GetAbsOrigin();

	const Vector &mins = player->WorldAlignMins();
	const Vector &maxs = player->WorldAlignMaxs();

	centroid.z += (maxs.z - mins.z)/2.0f;

	return centroid;
}


CBasePlayer* ClientPutInServerOverride_Bot( edict_t *pEdict, const char *playername );

/// @todo Remove this nasty hack - CreateFakeClient() calls CBot::Spawn, which needs the profile
extern const BotProfile *g_botInitProfile;
extern int g_botInitTeam;
extern int g_nClientPutInServerOverrides;

//--------------------------------------------------------------------------------------------------------
template < class T > T * CreateBot( const BotProfile *profile, int team )
{
	if ( !AreBotsAllowed() )
		return NULL;

	if ( UTIL_ClientsInGame() >= gpGlobals->maxClients )
	{
		CONSOLE_ECHO( "Unable to create bot: Server is full (%d/%d clients).\n", UTIL_ClientsInGame(), gpGlobals->maxClients );
		return NULL;
	}

	char botName[64];
	UTIL_ConstructBotNetName( botName, 64, profile );

	g_nClientPutInServerOverrides = 0;
	ClientPutInServerOverride( ClientPutInServerOverride_Bot );
	
	g_botInitProfile = profile;
	g_botInitTeam = team;
	edict_t *botEdict = engine->CreateFakeClient( botName );

	ClientPutInServerOverride( NULL );
	Assert( g_nClientPutInServerOverrides == 1 );


	if ( botEdict == NULL )
	{
		CONSOLE_ECHO( "Unable to create bot: CreateFakeClient() returned null.\n" );
		return NULL;
	}

	T *bot = dynamic_cast< T * >( CBaseEntity::Instance( botEdict ) );

	if ( bot == NULL )
	{
		Assert( false );
		Error( "Could not allocate and bind entity to bot edict.\n" );
		return NULL;
	}

	bot->ClearFlags();
	bot->AddFlag( FL_CLIENT | FL_FAKECLIENT );

	return bot;
}

//----------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------
/**
 * The base bot class from which bots for specific games are derived
 * A template is needed here because the CBot class must be derived from CBasePlayer, 
 * but also may need to be derived from a more specific player class, such as CFFPlayer
 */
template < class PlayerType >
class CBot : public PlayerType
{
public:
	DECLARE_CLASS( CBot, PlayerType );

	CBot( void );
	virtual ~CBot();
	virtual bool Initialize( const BotProfile *profile, int team );

	unsigned int GetID( void ) const		{ return m_id; }

	virtual bool IsBot( void ) const { return true; }	
	virtual bool IsNetClient( void ) const { return false; }

	virtual void Spawn( void );

	virtual void Upkeep( void ) = 0;
	virtual void Update( void ) = 0;


	virtual void Run( void );
	virtual void Walk( void );
	virtual bool IsRunning( void ) const		{ return m_isRunning; }
	
	virtual void Crouch( void );
	virtual void StandUp( void );
	bool IsCrouching( void ) const	{ return m_isCrouching; }

	void PushPostureContext( void );
	void PopPostureContext( void );

	virtual void MoveForward( void );
	virtual void MoveBackward( void );
	virtual void StrafeLeft( void );
	virtual void StrafeRight( void );

	#define MUST_JUMP true
	virtual bool Jump( bool mustJump = false );
	bool IsJumping( void );
	float GetJumpTimestamp( void ) const	{ return m_jumpTimestamp; }

	virtual void ClearMovement( void );

	const Vector &GetViewVector( void );


	//------------------------------------------------------------------------------------
	// Weapon interface
	//
	virtual void UseEnvironment( void );
	virtual void PrimaryAttack( void );
	virtual void ClearPrimaryAttack( void );
	virtual void TogglePrimaryAttack( void );
	virtual void SecondaryAttack( void );
	virtual void Reload( void );

	float GetActiveWeaponAmmoRatio( void ) const;
	bool IsActiveWeaponClipEmpty( void ) const;
	bool IsActiveWeaponOutOfAmmo( void ) const;
	bool IsActiveWeaponRecoilHigh( void ) const;
	bool IsUsingScope( void );

	CFFWeaponBase *GetActiveFFWeapon( void ) const; // FF_WEAPONS: Changed return type from CBasePlayerWeapon*


	//------------------------------------------------------------------------------------
	// Event hooks
	//

	virtual int OnTakeDamage( const CTakeDamageInfo &info )
	{
		return PlayerType::OnTakeDamage( info );
	}

	virtual void Event_Killed( const CTakeDamageInfo &info )
	{ 
		PlayerType::Event_Killed( info );
	}

	bool IsEnemy( CBaseEntity *ent ) const;
	int GetEnemiesRemaining( void ) const;
	int GetFriendsRemaining( void ) const;

	bool IsPlayerFacingMe( CFFPlayer *enemy ) const;			// Changed CBasePlayer to CFFPlayer
	bool IsPlayerLookingAtMe( CFFPlayer *enemy, float cosTolerance = 0.9f ) const;		// Changed CBasePlayer to CFFPlayer
	bool IsLookingAtPosition( const Vector &pos, float angleTolerance = 20.0f ) const;

	bool IsLocalPlayerWatchingMe( void ) const;

	void PrintIfWatched( PRINTF_FORMAT_STRING const char *format, ... ) const;

	virtual void UpdatePlayer( void );
	virtual void BuildUserCmd( CUserCmd& cmd, const QAngle& viewangles, float forwardmove, float sidemove, float upmove, int buttons, byte impulse );
	virtual void SetModel( const char *modelName );

	int Save( CSave &save )	const						{ return 0; }
	int Restore( CRestore &restore ) const	{ return 0; }
	virtual void Think( void ) { }

	const BotProfile *GetProfile( void ) const		{ return m_profile; }

	virtual bool ClientCommand( const CCommand &args );
	virtual int Cmd_Argc( void );
	virtual char *Cmd_Argv( int argc );

private:
	CUtlVector< char * > m_args;

protected:
	const BotProfile *m_profile;

private:
	friend class CFFBotManager; // Changed CBotManager to CFFBotManager

	unsigned int m_id;

	CUserCmd m_userCmd;
	bool m_isRunning;
	bool m_isCrouching;
	float m_forwardSpeed;
	float m_strafeSpeed;
	float m_verticalSpeed;
	int m_buttonFlags;

	float m_jumpTimestamp;

	Vector m_viewForward;
	struct PostureContext
	{
		bool isRunning;
		bool isCrouching;
	};
	enum { MAX_POSTURE_STACK = 8 };
	PostureContext m_postureStack[ MAX_POSTURE_STACK ];
	int m_postureStackIndex;

	void ResetCommand( void );

protected:
	virtual float GetMoveSpeed( void );
};


//-----------------------------------------------------------------------------------------------------------
// Inlines
//-----------------------------------------------------------------------------------------------------------
template < class T >
inline void CBot<T>::SetModel( const char *modelName ) { BaseClass::SetModel( modelName ); }
template < class T >
inline float CBot<T>::GetMoveSpeed( void ) { return this->MaxSpeed(); }
template < class T >
inline void CBot<T>::Run( void ) { m_isRunning = true; }
template < class T >
inline void CBot<T>::Walk( void ) { m_isRunning = false; }
template < class T >
inline bool CBot<T>::IsActiveWeaponRecoilHigh( void ) const
{
	const QAngle &angles = const_cast< CBot<T> * >( this )->GetPunchAngle();
	const float highRecoil = -1.5f; // This might be game-specific
	return (angles.x < highRecoil);
}
template < class T >
inline void CBot<T>::PushPostureContext( void )
{ if (m_postureStackIndex == MAX_POSTURE_STACK) { PrintIfWatched( "PushPostureContext() overflow error!\n" ); return; }
  m_postureStack[ m_postureStackIndex ].isRunning = m_isRunning; m_postureStack[ m_postureStackIndex ].isCrouching = m_isCrouching; ++m_postureStackIndex; }
template < class T >
inline void CBot<T>::PopPostureContext( void )
{ if (m_postureStackIndex == 0) { PrintIfWatched( "PopPostureContext() underflow error!\n" ); m_isRunning = true; m_isCrouching = false; return; }
  --m_postureStackIndex; m_isRunning = m_postureStack[ m_postureStackIndex ].isRunning; m_isCrouching = m_postureStack[ m_postureStackIndex ].isCrouching; }

template < class T >
inline bool CBot<T>::IsPlayerFacingMe( CFFPlayer *other ) const // Changed CBasePlayer to CFFPlayer
{ Vector toOther = other->GetAbsOrigin() - this->GetAbsOrigin(); Vector otherForward; AngleVectors( other->EyeAngles() + other->GetPunchAngle(), &otherForward );
  return (DotProduct( otherForward, toOther ) < 0.0f); }
template < class T >
inline bool CBot<T>::IsPlayerLookingAtMe( CFFPlayer *other, float cosTolerance ) const // Changed CBasePlayer to CFFPlayer
{ Vector toOther = other->GetAbsOrigin() - this->GetAbsOrigin(); toOther.NormalizeInPlace(); Vector otherForward; AngleVectors( other->EyeAngles() + other->GetPunchAngle(), &otherForward );
  return (DotProduct( otherForward, toOther ) < -cosTolerance); }

template < class T >
inline const Vector &CBot<T>::GetViewVector( void ) { AngleVectors( this->EyeAngles() + this->GetPunchAngle(), &m_viewForward ); return m_viewForward; }
template < class T >
inline bool CBot<T>::IsLookingAtPosition( const Vector &pos, float angleTolerance ) const
{ CBot< T > *me = const_cast< CBot< T > * >( this ); Vector to = pos - me->EyePosition(); QAngle idealAngles; VectorAngles( to, idealAngles );
  QAngle viewAngles = me->EyeAngles(); float deltaYaw = AngleNormalize( idealAngles.y - viewAngles.y ); float deltaPitch = AngleNormalize( idealAngles.x - viewAngles.x );
  return (fabs( deltaYaw ) < angleTolerance && abs( deltaPitch ) < angleTolerance); }

template < class PlayerType >
inline CBot< PlayerType >::CBot( void ) { static unsigned int nextID = 1; if (nextID == 0) ++nextID; m_id = nextID++; m_profile = NULL; m_postureStackIndex = 0; }
template < class PlayerType >
inline CBot< PlayerType >::~CBot( void ) { }
template < class PlayerType >
inline bool CBot< PlayerType >::Initialize( const BotProfile *profile, int team ) { m_profile = profile; return true; }
template < class PlayerType >
inline void CBot< PlayerType >::Spawn( void )
{ if (m_profile == NULL) Initialize( g_botInitProfile, g_botInitTeam ); PlayerType::Spawn();
  this->AddFlag( FL_CLIENT | FL_FAKECLIENT ); this->SetThink( NULL );
  m_isRunning = true; m_isCrouching = false; m_postureStackIndex = 0; m_jumpTimestamp = 0.0f; ResetCommand(); }

template < class PlayerType >
inline void CBot< PlayerType >::MoveForward( void ) { m_forwardSpeed = GetMoveSpeed(); SETBITS( m_buttonFlags, IN_FORWARD ); CLEARBITS( m_buttonFlags, IN_BACK ); }
template < class PlayerType >
inline void CBot< PlayerType >::MoveBackward( void ) { m_forwardSpeed = -GetMoveSpeed(); SETBITS( m_buttonFlags, IN_BACK ); CLEARBITS( m_buttonFlags, IN_FORWARD ); }
template < class PlayerType >
inline void CBot< PlayerType >::StrafeLeft( void ) { m_strafeSpeed = -GetMoveSpeed(); SETBITS( m_buttonFlags, IN_MOVELEFT ); CLEARBITS( m_buttonFlags, IN_MOVERIGHT ); }
template < class PlayerType >
inline void CBot< PlayerType >::StrafeRight( void ) { m_strafeSpeed = GetMoveSpeed(); SETBITS( m_buttonFlags, IN_MOVERIGHT ); CLEARBITS( m_buttonFlags, IN_MOVELEFT ); }
template < class PlayerType >
inline bool CBot< PlayerType >::Jump( bool mustJump )
{ if (IsJumping() || IsCrouching()) return false;
  if (!mustJump) { const float minJumpInterval = 0.9f; if (gpGlobals->curtime - m_jumpTimestamp < minJumpInterval) return false; }
  const float sanityInterval = 0.3f; if (gpGlobals->curtime - m_jumpTimestamp < sanityInterval) return false;
  SETBITS( m_buttonFlags, IN_JUMP ); m_jumpTimestamp = gpGlobals->curtime; return true; }
template < class PlayerType >
void CBot< PlayerType >::ClearMovement( void ) { m_forwardSpeed = 0.0; m_strafeSpeed = 0.0; m_verticalSpeed	= 100.0; m_buttonFlags &= ~(IN_FORWARD | IN_BACK | IN_LEFT | IN_RIGHT | IN_JUMP); }
template < class PlayerType >
inline bool CBot< PlayerType >::IsJumping( void ) { if (gpGlobals->curtime - m_jumpTimestamp > 3.0f) return false; if (gpGlobals->curtime - m_jumpTimestamp < 0.9f) return true;
  if (FBitSet( this->GetFlags(), FL_ONGROUND )) return false; return true; }
template < class PlayerType >
inline void CBot< PlayerType >::Crouch( void ) { m_isCrouching = true; }
template < class PlayerType >
inline void CBot< PlayerType >::StandUp( void ) { m_isCrouching = false; }
template < class PlayerType >
inline void CBot< PlayerType >::UseEnvironment( void ) { SETBITS( m_buttonFlags, IN_USE ); }
template < class PlayerType >
inline void CBot< PlayerType >::PrimaryAttack( void ) { SETBITS( m_buttonFlags, IN_ATTACK ); }
template < class PlayerType >
inline void CBot< PlayerType >::ClearPrimaryAttack( void ) { CLEARBITS( m_buttonFlags, IN_ATTACK ); }
template < class PlayerType >
inline void CBot< PlayerType >::TogglePrimaryAttack( void ) { if (FBitSet( m_buttonFlags, IN_ATTACK )) CLEARBITS( m_buttonFlags, IN_ATTACK ); else SETBITS( m_buttonFlags, IN_ATTACK ); }
template < class PlayerType >
inline void CBot< PlayerType >::SecondaryAttack( void ) { SETBITS( m_buttonFlags, IN_ATTACK2 ); }
template < class PlayerType >
inline void CBot< PlayerType >::Reload( void ) { SETBITS( m_buttonFlags, IN_RELOAD ); }

template < class PlayerType >
inline float CBot< PlayerType >::GetActiveWeaponAmmoRatio( void ) const
{ CBasePlayerWeapon *weapon = const_cast<CBot<PlayerType>*>(this)->GetActiveWeapon(); // Use CBasePlayerWeapon
  if (weapon == NULL) return 0.0f; if (weapon->Clip1() < 0) return 1.0f; return (float)weapon->Clip1() / (float)weapon->GetMaxClip1(); }
template < class PlayerType >
inline bool CBot< PlayerType >::IsActiveWeaponClipEmpty( void ) const
{ CBasePlayerWeapon *gun = const_cast<CBot<PlayerType>*>(this)->GetActiveWeapon(); if (gun && gun->Clip1() == 0) return true; return false; }
template < class PlayerType >
inline bool CBot< PlayerType >::IsActiveWeaponOutOfAmmo( void ) const
{ CBasePlayerWeapon *weapon = const_cast<CBot<PlayerType>*>(this)->GetActiveWeapon(); if (weapon == NULL) return true; return !weapon->HasAnyAmmo(); }
template < class PlayerType >
inline bool CBot< PlayerType >::IsUsingScope( void ) { if (this->GetFOV() < this->GetDefaultFOV()) return true; return false; }

template < class PlayerType >
inline CFFWeaponBase *CBot<PlayerType>::GetActiveFFWeapon( void ) const // FF_WEAPONS: Changed return type
{ return static_cast<CFFWeaponBase *>(const_cast<CBot<PlayerType>*>(this)->GetActiveWeapon()); }


template < class PlayerType >
inline void CBot< PlayerType >::BuildUserCmd( CUserCmd& cmd, const QAngle& viewangles, float forwardmove, float sidemove, float upmove, int buttons, byte impulse )
{ Q_memset( &cmd, 0, sizeof( cmd ) ); cmd.command_number = gpGlobals->tickcount; cmd.forwardmove = forwardmove; cmd.sidemove = sidemove; cmd.upmove = upmove;
  cmd.buttons = buttons; cmd.impulse = impulse; VectorCopy( viewangles, cmd.viewangles ); cmd.random_seed = random->RandomInt( 0, 0x7fffffff ); }
template < class PlayerType >
inline void CBot< PlayerType >::UpdatePlayer( void )
{ if (m_isCrouching) SETBITS( m_buttonFlags, IN_DUCK ); else if (!m_isRunning) SETBITS( m_buttonFlags, IN_SPEED );
  if ( this->IsEFlagSet(EFL_BOT_FROZEN) ) { m_buttonFlags = 0; m_forwardSpeed = 0; m_strafeSpeed = 0; m_verticalSpeed = 0; }
  this->BuildUserCmd( m_userCmd, this->EyeAngles(), m_forwardSpeed, m_strafeSpeed, m_verticalSpeed, m_buttonFlags, 0 );
  this->ProcessUsercmds( &m_userCmd, 1, 1, 0, false ); }
template < class PlayerType >
inline void CBot< PlayerType >::ResetCommand( void ) { m_forwardSpeed = 0.0; m_strafeSpeed = 0.0; m_verticalSpeed	= 100.0; m_buttonFlags = 0; }

template < class PlayerType >
inline bool CBot< PlayerType >::ClientCommand( const CCommand &args )
{ int i; for ( i=0; i<m_args.Count(); ++i ) delete[] m_args[i]; m_args.RemoveAll();
  const char *cmd = args.GetCommandString();
  while (1) { while (*cmd && *cmd <= ' ' && *cmd != '\n') cmd++; if (*cmd == '\n') { cmd++; break; } if (!*cmd) break;
	cmd = SharedParse (cmd); if (!cmd) break; m_args.AddToTail( CloneString( SharedGetToken() ) ); }
  return PlayerType::ClientCommand( args ); }
template < class PlayerType >
inline int CBot< PlayerType >::Cmd_Argc() { return m_args.Count(); }
template < class PlayerType >
inline char * CBot< PlayerType >::Cmd_Argv( int argc ) { if ( argc < 0 || argc >= m_args.Count() ) return NULL; return m_args[argc]; }

template < class PlayerType >
inline bool CBot< PlayerType >::IsEnemy( CBaseEntity *ent ) const
{ if (!ent->IsPlayer() || !ent->IsAlive()) return false;
  CBasePlayer *player = static_cast<CBasePlayer *>( ent );
  // Relies on GetTeamNumber() returning FF_TEAM_* values and FF_TEAM_SPECTATOR being defined
  if (player->GetTeamNumber() == this->GetTeamNumber() || player->GetTeamNumber() == FF_TEAM_SPECTATOR ) return false;
  return true; }
template < class PlayerType >
inline int CBot< PlayerType >::GetEnemiesRemaining( void ) const
{ int count = 0; for ( int i = 1; i <= gpGlobals->maxClients; ++i ) { CBasePlayer *player = UTIL_PlayerByIndex( i );
    if (player == NULL || !IsEnemy( player ) || !player->IsAlive()) continue; count++; } return count; }
template < class PlayerType >
inline int CBot< PlayerType >::GetFriendsRemaining( void ) const
{ int count = 0; for ( int i = 1; i <= gpGlobals->maxClients; ++i ) { CBasePlayer *player = UTIL_PlayerByIndex( i );
    if (player == NULL || IsEnemy( player ) || !player->IsAlive() || player == static_cast<CBaseEntity *>( const_cast<CBot<PlayerType> *>( this ) ) ) continue; count++; } return count; }

template < class PlayerType >
inline bool CBot< PlayerType >::IsLocalPlayerWatchingMe( void ) const
{ if ( engine->IsDedicatedServer() ) return false; CBasePlayer *player = UTIL_GetListenServerHost(); if ( player == NULL ) return false;
  if ( cv_bot_debug_target.GetInt() > 0 ) return this->entindex() == cv_bot_debug_target.GetInt();
  if ( player->IsObserver() || !player->IsAlive() ) { if ( const_cast< CBot< PlayerType > * >(this) == player->GetObserverTarget() ) {
	switch( player->GetObserverMode() ) { case OBS_MODE_IN_EYE: case OBS_MODE_CHASE: return true; } } } return false; }

template < class PlayerType >
inline void CBot< PlayerType >::PrintIfWatched( PRINTF_FORMAT_STRING const char *format, ... ) const
{ if (cv_bot_debug.GetInt() == 0) return;
  if ((IsLocalPlayerWatchingMe() && (cv_bot_debug.GetInt() == 1 || cv_bot_debug.GetInt() == 3)) || (cv_bot_debug.GetInt() == 2 || cv_bot_debug.GetInt() == 4))
  { va_list varg; char buffer[ CBotManager::MAX_DBG_MSG_SIZE ]; const char *name = const_cast< CBot< PlayerType > * >( this )->GetPlayerName();
    va_start( varg, format ); Q_vsnprintf( buffer, sizeof(buffer), format, varg ); va_end( varg ); // Used Q_vsnprintf for safety
    ClientPrint( UTIL_GetListenServerHost(), HUD_PRINTCONSOLE, UTIL_VarArgs( "%s: %s", (name) ? name : "(NULL netname)", buffer ) );
    if (TheBots) TheBots->AddDebugMessage( buffer ); } // Added null check for TheBots
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------

extern void InstallBotControl( void );
extern void RemoveBotControl( void );
extern void Bot_ServerCommand( void );
extern void Bot_RegisterCvars( void );

extern bool IsSpotOccupied( CBaseEntity *me, const Vector &pos );
extern const Vector *FindNearbyHidingSpot( CBaseEntity *me, const Vector &pos, float maxRange = 1000.0f, bool isSniper = false, bool useNearest = false );
extern const Vector *FindRandomHidingSpot( CBaseEntity *me, Place place, bool isSniper = false ); // Place is CS-specific, may need adaptation
extern const Vector *FindNearbyRetreatSpot( CFFBot *me, const Vector &start, float maxRange = 1000.0f, int avoidTeam = 0 ); // Changed CBaseEntity to CFFBot


#endif // BOT_H
