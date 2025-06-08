//========= Fortress Forever - Bot Engineer Guard Sentry State ============//
//
// Purpose: A bot state for Engineers to guard their Sentry Guns.
//
//=============================================================================//

#ifndef FF_BOT_GUARD_SENTRY_H
#define FF_BOT_GUARD_SENTRY_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When an Engineer bot is guarding its Sentry Gun.
 */
class GuardSentryState : public BotState
{
public:
	GuardSentryState(void);
	virtual ~GuardSentryState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

	// Note: Target sentry is implicitly the bot's own sentry gun.
	// No SetTargetSentry needed as it will be fetched via me->GetSentryGun().

private:
	CHandle<CBaseEntity> m_sentryToGuard;       // Handle to the sentry gun being guarded
	CountdownTimer m_scanForThreatsTimer;   // Timer to periodically scan for enemies or sentry damage
	Vector m_guardSpot;                   // The position where the bot will stand to guard
	CountdownTimer m_repathTimer;           // Timer to periodically repath if stuck
	bool m_isAtGuardSpot;
	CountdownTimer m_tendSentryTimer;       // Timer for periodic "tending" to the sentry
};

#endif // FF_BOT_GUARD_SENTRY_H
