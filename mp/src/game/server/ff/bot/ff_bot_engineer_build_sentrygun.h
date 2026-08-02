//========= Fortress Forever Bot =============================================//
//
// CFFBotEngineerBuildSentryGun — pick a smart sentry placement, path there,
// orient toward enemy approach, fire Command_BuildSentryGun.
//
// Replaces the simpler "build at current position" path in
// FFBotClass::UpdateEngineer with a targeted action that:
//   1) Scores FF_NAV_SENTRY_SPOT areas (mapper hints) for distance to own
//      flag/cap and inverse distance to enemy spawn.
//   2) Falls back to "near own flag" when no hints exist.
//   3) Aims toward the enemy invasion vector before kicking the build —
//      FF sentries auto-rotate but the initial facing does matter for the
//      first few shots.
//
// FF differences from TFBot:
//   - Build trigger is Command_BuildSentryGun() (a console-style command),
//     not StartBuildingObjectOfType + a builder weapon.
//   - Resource is AMMO_CELLS, not TF_AMMO_METAL.
//   - No teleporter/dispenser flow — those are separate actions.
//
//===========================================================================//

#ifndef FF_BOT_ENGINEER_BUILD_SENTRYGUN_H
#define FF_BOT_ENGINEER_BUILD_SENTRYGUN_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"
#include "ff_nav_area.h"

class CFFBotEngineerBuildSentryGun : public Action< CFFBot >
{
public:
	CFFBotEngineerBuildSentryGun( void );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "EngineerBuildSentryGun"; }

private:
	Vector m_buildLocation;
	bool m_haveBuildLocation;
	bool m_buildIssued;
	int m_triesLeft;

	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_searchTimer;

	bool ChooseBuildLocation( CFFBot *me );
	bool IsLocationFreeOfFriendlySentries( const Vector &where, int myTeam ) const;
};

#endif // FF_BOT_ENGINEER_BUILD_SENTRYGUN_H
