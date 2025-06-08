//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Bot state for defending a Lua-defined objective.
//
//=============================================================================//

#ifndef FF_BOT_DEFEND_OBJECTIVE_H
#define FF_BOT_DEFEND_OBJECTIVE_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For BotState and CFFBot
#include "../ff_bot_manager.h" // For CFFBotManager::LuaObjectivePoint

class DefendObjectiveState : public BotState
{
public:
	DefendObjectiveState(void);
	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const { return "DefendObjective"; }

	void SetObjectiveToDefend(const CFFBotManager::LuaObjectivePoint* pObjective);

private:
	const CFFBotManager::LuaObjectivePoint* m_objectiveToDefend;
	Vector m_defenseSpot;         // The specific spot the bot will try to hold near the objective
	CountdownTimer m_repathTimer; // For retrying path if stuck
	CountdownTimer m_scanTimer;   // For periodically looking around
	bool m_isAtDefenseSpot;       // True if the bot has reached its chosen defense spot
};

#endif // FF_BOT_DEFEND_OBJECTIVE_H
