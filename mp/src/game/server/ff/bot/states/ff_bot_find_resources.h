//========= Fortress Forever - Bot Engineer Find Resources State ============//
//
// Purpose: A bot state for Engineers to find resources (ammo/cells).
//
//=============================================================================//

#ifndef FF_BOT_FIND_RESOURCES_H
#define FF_BOT_FIND_RESOURCES_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When an Engineer bot is searching for resources (cells for building).
 */
class FindResourcesState : public BotState
{
public:
	FindResourcesState(void);
	virtual ~FindResourcesState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

private:
	CHandle<CBaseEntity> m_resourceTarget;  // Target dispenser or ammo pack
	CountdownTimer m_searchTimer;         // Timer to periodically search for new resource targets
	CountdownTimer m_repathTimer;         // Timer to periodically repath if stuck
};

#endif // FF_BOT_FIND_RESOURCES_H
