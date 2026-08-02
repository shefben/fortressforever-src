//========= Fortress Forever Bot =============================================//
//
// CFFBotEngineerMaintain — engineer in-place upkeep on sentry+dispenser.
//
// Stations the engineer between own sentry and own dispenser, swinging the
// spanner to upgrade/repair both. Includes spy paranoia (periodic
// rear-arc check) and exits when buildings are missing (so a parent can
// rebuild) or under heavy fire (parent retreats).
//
//===========================================================================//

#ifndef FF_BOT_ENGINEER_MAINTAIN_H
#define FF_BOT_ENGINEER_MAINTAIN_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBotEngineerMaintain : public Action< CFFBot >
{
public:
	CFFBotEngineerMaintain( void );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "EngineerMaintain"; }

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_spyCheckTimer;	// throttle 180° turn-around
	bool m_isCheckingForSpies;
};

#endif // FF_BOT_ENGINEER_MAINTAIN_H
