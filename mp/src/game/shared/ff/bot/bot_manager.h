//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#ifndef BASE_CONTROL_H
#define BASE_CONTROL_H

#pragma warning( disable : 4530 )					// STL uses exceptions, but we are not compiling with them - ignore warning

// Ensure CBasePlayer is known, usually from cbase.h or player.h
#include "cbaseplayer.h" // Or equivalent that defines CBasePlayer
#include "utllinkedlist.h" // For CUtlLinkedList
#include "usercmd.h"      // For CCommand
#include "gamestringpool.h" // For IGameSystem
#include "interval.h"     // For IntervalTimer

extern float g_BotUpkeepInterval;					///< duration between bot upkeeps
extern float g_BotUpdateInterval;					///< duration between bot updates
const int g_BotUpdateSkipCount = 2;					///< number of upkeep periods to skip update

class CNavArea; // Forward declaration

// FF_TODO: Review these grenade constants. They might be CS-specific.
// If FF has different grenade types or radii, these need to be adapted or made generic.
enum DefaultGrenadeRadii // Renamed for clarity if these are defaults
{
	DEFAULT_HEGrenadeRadius = 115, // Assuming this might be a generic explosive radius
	DEFAULT_SmokeGrenadeRadius = 155,
	DEFAULT_FlashbangGrenadeRadius = 115
};

//--------------------------------------------------------------------------------------------------------------
class CBaseGrenade;

/**
 * An ActiveGrenade is a representation of a grenade in the world
 */
class ActiveGrenade
{
public:
	ActiveGrenade( CBaseGrenade *grenadeEntity );

	void OnEntityGone( void );
	void Update( void );
	bool IsValid( void ) const	;
	
	bool IsEntity( CBaseGrenade *grenade ) const		{ return (grenade == m_entity) ? true : false; }
	CBaseGrenade *GetEntity( void ) const				{ return m_entity; }

	const Vector &GetDetonationPosition( void ) const	{ return m_detonationPosition; }
	const Vector &GetPosition( void ) const;
	bool IsSmoke( void ) const							{ return m_isSmoke; }
	bool IsFlashbang( void ) const						{ return m_isFlashbang; }
	CBaseGrenade *GetGrenade( void ) { return m_entity; }
	float GetRadius( void ) const { return m_radius; }
	void SetRadius( float radius ) { m_radius = radius; }

private:
	CHandle<CBaseGrenade> m_entity; // Changed to EHANDLE for safety
	Vector m_detonationPosition;
	float m_dieTimestamp;
	bool m_isSmoke;
	bool m_isFlashbang;
	float m_radius;
};

typedef CUtlLinkedList<ActiveGrenade *> ActiveGrenadeList;


//--------------------------------------------------------------------------------------------------------------
/**
 * This class manages all active bots, propagating events to them and updating them.
 */
class CBotManager : public IGameSystem // For AddDebugMessage to be compatible with engine
{
public:
	CBotManager();
	virtual ~CBotManager();

	CBasePlayer *AllocateAndBindBotEntity( edict_t *ed );
	virtual CBasePlayer *AllocateBotEntity( void ) = 0;

	virtual void ClientDisconnect( CBaseEntity *entity ) = 0;
	virtual bool ClientCommand( CBasePlayer *player, const CCommand &args ) = 0;

	virtual void ServerActivate( void ) = 0;
	virtual void ServerDeactivate( void ) = 0;
	virtual bool ServerCommand( const char * pcmd ) = 0;

	virtual void RestartRound( void );
	virtual void StartFrame( void );

	virtual unsigned int GetPlayerPriority( CBasePlayer *player ) const = 0;
	

	void AddGrenade( CBaseGrenade *grenade );
	void RemoveGrenade( CBaseGrenade *grenade );
	void SetGrenadeRadius( CBaseGrenade *grenade, float radius );
	void ValidateActiveGrenades( void );
	void DestroyAllGrenades( void );
	bool IsLineBlockedBySmoke( const Vector &from, const Vector &to, float grenadeBloat = 1.0f );
	bool IsInsideSmokeCloud( const Vector *pos );

	template < typename T >
	bool ForEachGrenade( T &func )
	{
		int it = m_activeGrenadeList.Head();
		while( it != m_activeGrenadeList.InvalidIndex() )
		{
			ActiveGrenade *ag = m_activeGrenadeList[ it ];
			int current = it;
			it = m_activeGrenadeList.Next( it );
			if (!ag->IsValid()) { m_activeGrenadeList.Remove( current ); delete ag; continue; }
			else { if (func( ag ) == false) return false; }
		}
		return true;
	}

	enum { MAX_DBG_MSG_SIZE = 1024 };
	struct DebugMessage
	{
		char m_string[ MAX_DBG_MSG_SIZE ];
		IntervalTimer m_age;
	};

	int GetDebugMessageCount( void ) const;
	const DebugMessage *GetDebugMessage( int which = 0 ) const;
	void ClearDebugMessages( void );
	void AddDebugMessage( const char *msg );


private:
	ActiveGrenadeList m_activeGrenadeList;

	enum { MAX_DBG_MSGS = 6 };
	DebugMessage m_debugMessage[ MAX_DBG_MSGS ];
	int m_debugMessageCount;
	int m_currentDebugMessage;

	IntervalTimer m_frameTimer;
};


inline CBasePlayer *CBotManager::AllocateAndBindBotEntity( edict_t *ed )
{
	CBaseEntity::s_Edict = ed; // Correct way to set edict for allocation
	return AllocateBotEntity();
}

inline int CBotManager::GetDebugMessageCount( void ) const
{
	return m_debugMessageCount;
}

inline const CBotManager::DebugMessage *CBotManager::GetDebugMessage( int which ) const
{
	if (which >= m_debugMessageCount || which < 0) // Added bounds check
		return NULL;

	int i = (m_currentDebugMessage - which + MAX_DBG_MSGS) % MAX_DBG_MSGS; // Ensure positive index

	return &m_debugMessage[ i ];
}

extern CBotManager *TheBots;

#endif
