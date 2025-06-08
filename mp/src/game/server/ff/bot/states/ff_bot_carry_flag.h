//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Bot state for carrying a flag to a capture point.
//
//=============================================================================//

#ifndef FF_BOT_CARRY_FLAG_H
#define FF_BOT_CARRY_FLAG_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For BotState and CFFBot
#include "../ff_bot_manager.h" // For CFFBotManager::LuaObjectivePoint

class CarryFlagState : public BotState
{
public:
	CarryFlagState(void);
	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const { return "CarryFlag"; }

	void SetCaptureTarget(const CFFBotManager::LuaObjectivePoint* capturePoint);

private:
	const CFFBotManager::LuaObjectivePoint* m_captureTargetPoint;
	bool m_isAtCapturePoint;
	CountdownTimer m_repathTimer;
	// CountdownTimer m_captureInteractionTimer; // If a "use" action is needed at cap point
};

#endif // FF_BOT_CARRY_FLAG_H
