//========= Fortress Forever Bot =============================================//
//
// CFFBotSpyInfiltrate — top-level spy behavior. Find a hiding spot near
// enemy spawn exits, wait for victims, suspend to attack/sap on opportunity.
//
// Ported from hl2_src tf_bot_spy_infiltrate. Differences:
//   - Uses traceline LOS to filter "visible from enemy spawn exit" since FF
//     nav lacks PVS data.
//   - FF cloak/disguise per-tick is already driven by FFBotClass::UpdateSpy;
//     this action only commands directional movement and high-level
//     suspend-to-attack/sap decisions.
//   - No TFGameRules::InSetup → FF rounds don't have a setup phase.
//
//===========================================================================//

#ifndef FF_BOT_SPY_INFILTRATE_H
#define FF_BOT_SPY_INFILTRATE_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"
#include "ff_nav_area.h"

class CFFBotSpyInfiltrate : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;
	virtual ActionResult< CFFBot > OnResume( CFFBot *me, Action< CFFBot > *interruptingAction ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "SpyInfiltrate"; }

private:
	CFFNavArea *m_hideArea;
	CountdownTimer m_findHidingSpotTimer;
	CountdownTimer m_waitTimer;
	CountdownTimer m_repathTimer;
	PathFollower m_path;

	bool FindHidingSpot( CFFBot *me );
};

#endif // FF_BOT_SPY_INFILTRATE_H
