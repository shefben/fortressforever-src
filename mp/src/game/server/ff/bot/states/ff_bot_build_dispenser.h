//========= Fortress Forever - Bot Engineer Build Dispenser State ============//
//
// Purpose: A new bot state for Engineers to build Dispensers.
//
//=============================================================================//

#ifndef FF_BOT_BUILD_DISPENSER_H
#define FF_BOT_BUILD_DISPENSER_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When an Engineer bot is building a Dispenser.
 */
class BuildDispenserState : public BotState
{
public:
	BuildDispenserState(void);
	virtual ~BuildDispenserState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

	// Method for CFFBot to explicitly set a build location
	void SetBuildLocation(const Vector &location);

private:
	Vector m_buildLocation;         // Target location to build the dispenser
	bool m_isBuilding;            // True if currently in the "hitting with spanner" phase
	bool m_isAtBuildLocation;     // True if bot has reached the build location

	CHandle<CBaseEntity> m_dispenserBeingBuilt; // Handle to the dispenser entity while it's being built

	CountdownTimer m_buildProgressTimer;  // Timer to simulate initial build duration
	CountdownTimer m_repathTimer;         // Timer to periodically check path if stuck
	CountdownTimer m_waitForBlueprintTimer; // Timer to wait for blueprint to spawn after command

	// Upgrade phase members
	bool m_isUpgrading;                   // True if currently in the upgrading phase
	int m_targetUpgradeLevel;             // Target level to upgrade to
	int m_currentUpgradeLevel;            // Current level of the buildable
	CountdownTimer m_upgradeProgressTimer;  // Timer for each upgrade level's duration
};

#endif // FF_BOT_BUILD_DISPENSER_H
