//========= Fortress Forever - Bot Engineer Repair Buildable State ============//
//
// Purpose: A bot state for Engineers to repair damaged friendly buildables.
//
//=============================================================================//

#ifndef FF_BOT_REPAIR_BUILDABLE_H
#define FF_BOT_REPAIR_BUILDABLE_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When an Engineer bot is repairing a damaged friendly buildable.
 */
class RepairBuildableState : public BotState
{
public:
	RepairBuildableState(void);
	virtual ~RepairBuildableState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

	// Method for CFFBot to explicitly set a buildable to repair
	void SetTargetBuildable(CBaseEntity* buildable);

private:
	CHandle<CBaseEntity> m_targetBuildable; // The buildable entity to repair
	CountdownTimer m_repathTimer;         // Timer to periodically check path if stuck
	CountdownTimer m_checkRepairTimer;    // Timer to periodically check if repair is complete
};

#endif // FF_BOT_REPAIR_BUILDABLE_H
