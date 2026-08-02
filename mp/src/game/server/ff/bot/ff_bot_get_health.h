//========= Fortress Forever Bot =============================================//
//
// CFFBotGetHealth — pick up nearby health. Mid-level reusable action.
//
// Ported from hl2_src tf_bot_get_health with FF entity adaptations:
//   - Friendly dispensers (FF_Dispenser) instead of obj_dispenser*
//   - Dropped health (ff_item_healthdrop, ff_item_backpack) instead of
//     item_healthkit*
//   - Static map health (info_ff_script with kBackPack_Health goalType)
//     instead of func_regenerate
//
//===========================================================================//

#ifndef FF_BOT_GET_HEALTH_H
#define FF_BOT_GET_HEALTH_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBotGetHealth : public Action< CFFBot >
{
public:
	CFFBotGetHealth( void );

	static bool IsPossible( CFFBot *me );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;

	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "GetHealth"; }

private:
	PathFollower m_path;
	CHandle< CBaseEntity > m_health;
	bool m_isGoalDispenser;
};

#endif // FF_BOT_GET_HEALTH_H
