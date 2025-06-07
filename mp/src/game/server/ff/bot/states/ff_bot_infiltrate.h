//========= Fortress Forever - Bot Spy Infiltrate State ============//
//
// Purpose: A bot state for Spies to infiltrate, cloak, and target enemies.
//
//=============================================================================//

#ifndef FF_BOT_INFILTRATE_H
#define FF_BOT_INFILTRATE_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When a Spy bot is infiltrating enemy lines.
 */
class InfiltrateState : public BotState
{
public:
	InfiltrateState(void);
	virtual ~InfiltrateState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

private:
	void SelectNewSpyTarget( CFFBot *me ); // Helper to find and set a target

	CHandle<CBaseEntity> m_targetEnemyBuilding;
	CHandle<CFFPlayer> m_targetEnemyPlayer;

	bool m_isCloaked;
	CountdownTimer m_actionTimer;       // Timer for various actions like sapping duration, time between target re-evaluations
	CountdownTimer m_repathTimer;         // Timer to periodically repath if stuck
	bool m_isAtTarget;              // True if bot has reached the target's vicinity

	// Constants for behavior
	const float SAP_DURATION = 2.0f;
	const float BACKSTAB_DURATION = 1.0f; // Time to "complete" a backstab attempt
	const float CLOAK_UNCLOAK_TIME = 1.0f; // Time it takes to cloak/uncloak
	const float TARGET_REACQUIRE_TIME = 5.0f; // How often to re-evaluate target if current one is lost/invalid
	const float INTERACTION_RANGE = 100.0f; // Range for sapping/backstabbing
    const float MOVEMENT_CLOAK_DIST_THRESHOLD = 300.0f; // If further than this from target, try to cloak while moving
};

#endif // FF_BOT_INFILTRATE_H
