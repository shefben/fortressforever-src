//========= Fortress Forever Bot =============================================//
//
// CFFBotBody — PlayerBody subclass with combat-tuned head-aim parameters.
//
// Default PlayerBody uses nb_saccade_speed (1000 deg/s), no subject lead, and
// no tracking re-evaluation. That makes bots aim slowly and miss moving
// targets. We override to:
//   - turn the head ~3x faster (snappy combat aim)
//   - lead moving subjects by ~0.15s (so we shoot where they're going)
//   - re-evaluate tracking every 50ms (fresh prediction on fast strafers)
//
//===========================================================================//

#ifndef FF_BOT_BODY_H
#define FF_BOT_BODY_H
#ifdef _WIN32
#pragma once
#endif

#include "Player/NextBotPlayerBody.h"

class CFFBotBody : public PlayerBody
{
public:
	CFFBotBody( INextBot *bot ) : PlayerBody( bot ) {}

	virtual float GetMaxHeadAngularVelocity( void ) const OVERRIDE { return 3000.0f; }
	virtual float GetHeadAimTrackingInterval( void ) const OVERRIDE { return 0.05f; }

	// We compute projectile-aware leading manually per-tick in
	// CFFBotMainAction (CFFWeaponBase + distance + target velocity ⇒ time-of-
	// flight ⇒ lead position). Disable the body's built-in subject lead so
	// it doesn't double-count.
	virtual float GetHeadAimSubjectLeadTime( void ) const OVERRIDE { return 0.0f; }
};

#endif // FF_BOT_BODY_H
