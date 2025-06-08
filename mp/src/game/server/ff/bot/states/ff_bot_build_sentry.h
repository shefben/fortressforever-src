//========= Fortress Forever - Bot Engineer Build Sentry State ============//
//
// Purpose: A new bot state for Engineers to build Sentry Guns.
//
//=============================================================================//

#ifndef FF_BOT_BUILD_SENTRY_H
#define FF_BOT_BUILD_SENTRY_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When an Engineer bot is building a Sentry Gun.
 */
class BuildSentryState : public BotState
{
public:
	BuildSentryState(void);
	virtual ~BuildSentryState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

	// Method for CFFBot to explicitly set a build location
	void SetBuildLocation(const Vector &location);

private:
	Vector m_buildLocation;         // Target location to build the sentry
	bool m_isBuilding;            // True if currently in the "hitting with spanner" phase
	bool m_isAtBuildLocation;     // True if bot has reached the build location

	// Conceptual: In a real implementation, this would be a CFFBuildableObject* or similar
	CHandle<CBaseEntity> m_sentryBeingBuilt;

	CountdownTimer m_buildProgressTimer;  // Timer to simulate initial build duration
	CountdownTimer m_repathTimer;         // Timer to periodically check path if stuck
	CountdownTimer m_waitForBlueprintTimer; // Timer to wait for blueprint to spawn after command

	// Upgrade phase members
	bool m_isUpgrading;                   // True if currently in the upgrading phase
	int m_targetUpgradeLevel;             // Target level to upgrade to (e.g., 2 or 3)
	int m_currentUpgradeLevel;            // Current level of the buildable (assumed 1 after initial build)
	CountdownTimer m_upgradeProgressTimer;  // Timer for each upgrade level's duration
};

#endif // FF_BOT_BUILD_SENTRY_H
