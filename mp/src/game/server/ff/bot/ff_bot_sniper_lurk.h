//========= Fortress Forever Bot =============================================//
//
// CFFBotSniperLurk — pick a sniper vantage, hold position, scope/charge/fire.
//
// Ported from hl2_src tf_bot_sniper_lurk + tf_bot_sniper_attack, merged
// into a single action since FF doesn't separate them as cleanly. Uses
// FF_NAV_SNIPER_SPOT-tagged nav areas as preferred home positions; falls
// back to BFS for a vantage area with LOS to enemy players.
//
// Charge/fire is delegated to CFFBotMainAction's per-tick FireWeaponAtEnemy
// + the bot's m_sniperFireState charge-then-release state machine.
//
//===========================================================================//

#ifndef FF_BOT_SNIPER_LURK_H
#define FF_BOT_SNIPER_LURK_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"
#include "ff_nav_area.h"

class CFFBotSniperLurk : public Action< CFFBot >
{
public:
	CFFBotSniperLurk( void );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;
	virtual ActionResult< CFFBot > OnResume( CFFBot *me, Action< CFFBot > *interruptingAction ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "SniperLurk"; }

private:
	Vector m_homePosition;
	bool m_isHomeValid;
	bool m_isAtHome;
	int m_failCount;

	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_boredTimer;

	bool FindNewHome( CFFBot *me );
};

#endif // FF_BOT_SNIPER_LURK_H
