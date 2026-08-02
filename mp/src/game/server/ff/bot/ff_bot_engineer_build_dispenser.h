//========= Fortress Forever Bot =============================================//
//
// CFFBotEngineerBuildDispenser — find a spot near the engineer's sentry,
// path there, fire Command_BuildDispenser.
//
// FF dispensers are most useful when adjacent to the sentry so the engineer
// can swing the spanner between both, repairing/restocking the SG while
// also healing/ammoing teammates. Placement target: 80–150u from SG with
// LOS to it.
//
//===========================================================================//

#ifndef FF_BOT_ENGINEER_BUILD_DISPENSER_H
#define FF_BOT_ENGINEER_BUILD_DISPENSER_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBotEngineerBuildDispenser : public Action< CFFBot >
{
public:
	CFFBotEngineerBuildDispenser( void );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "EngineerBuildDispenser"; }

private:
	Vector m_buildLocation;
	bool m_haveBuildLocation;
	bool m_buildIssued;
	int m_triesLeft;

	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_searchTimer;

	bool ChooseBuildLocation( CFFBot *me );
};

#endif // FF_BOT_ENGINEER_BUILD_DISPENSER_H
