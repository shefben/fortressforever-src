//========= Fortress Forever Bot =============================================//
//
// CFFBotDemomanStickyTrap — lay a sticky trap at a choke.
//
// Switches to ff_weapon_pipelauncher, aims at one of the bot's nav-area
// invasion vectors (where enemies enter from), and presses fire to drop
// 4-6 sticky bombs in a tight pattern. Ends when clip is empty or count
// is reached. Detonation is handled by a separate watcher (sticky-trap
// engagement is reactive — when an enemy walks into the trap, the watcher
// presses alt-fire on the pipelauncher).
//
// Ported from hl2_src tf_bot_prepare_stickybomb_trap, simplified by
// dropping TFBot's INextBotReply-based aim-then-fire callback chain in
// favor of a per-tick "aim, wait for head-steady, fire" loop.
//
//===========================================================================//

#ifndef FF_BOT_DEMOMAN_STICKY_TRAP_H
#define FF_BOT_DEMOMAN_STICKY_TRAP_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "ff_bot.h"
#include "ff_nav_area.h"

class CFFBotDemomanStickyTrap : public Action< CFFBot >
{
public:
	CFFBotDemomanStickyTrap( void );

	static bool IsPossible( CFFBot *me );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "DemomanStickyTrap"; }

private:
	CFFNavArea *m_targetArea;	// where enemies will come from
	int m_stickiesPlaced;
	int m_stickyTarget;			// 4-6 stickies for a useful trap
	CountdownTimer m_aimTimer;
	CountdownTimer m_fireGapTimer;
};

#endif // FF_BOT_DEMOMAN_STICKY_TRAP_H
