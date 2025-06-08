//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A new bot state for capturing Lua-defined objectives.
//
//=============================================================================//

#ifndef FF_BOT_CAPTURE_OBJECTIVE_H
#define FF_BOT_CAPTURE_OBJECTIVE_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot
// Note: BotState is defined in ff_bot.h, so no separate bot_state.h needed if ff_bot.h is included.
// #include "../bot_state.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot is attempting to capture a Lua-defined objective.
 */
class CaptureObjectiveState : public BotState
{
public:
	CaptureObjectiveState(void);
	virtual ~CaptureObjectiveState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const		{ return "CaptureObjective"; }

	void SetObjective(const CFFBotManager::LuaObjectivePoint* objective);

private:
	const CFFBotManager::LuaObjectivePoint* m_targetObjective;
	// CountdownTimer m_captureTimer;		// Timer to simulate capture duration - REMOVED
	bool m_isAtObjective;				// True if bot has reached the objective
	CountdownTimer m_repathTimer;		// Timer to periodically check path if stuck
	CountdownTimer m_checkObjectiveStatusTimer; // Timer to periodically check objective status
	bool m_isDefending;                 // True if the bot believes its team holds the objective and is defending
};

#endif // FF_BOT_CAPTURE_OBJECTIVE_H
