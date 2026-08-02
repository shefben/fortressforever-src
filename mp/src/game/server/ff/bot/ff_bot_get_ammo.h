//========= Fortress Forever Bot =============================================//
//
// CFFBotGetAmmo — pick up nearby ammo. Mid-level reusable action.
//
// Ported from hl2_src tf_bot_get_ammo with FF entity adaptations:
//   - Friendly dispensers (FF_Dispenser) instead of obj_dispenser*
//   - Dropped backpacks (ff_item_backpack) instead of tf_ammo_pack
//   - Static map ammo (info_ff_script with kBackPack_Ammo / kBackPack_Full
//     / kBackPack_Grenades goalType) instead of func_regenerate
//
//===========================================================================//

#ifndef FF_BOT_GET_AMMO_H
#define FF_BOT_GET_AMMO_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBotGetAmmo : public Action< CFFBot >
{
public:
	CFFBotGetAmmo( void );

	// Probe: returns true if an ammo source exists within reach. Caches the
	// best candidate in a per-frame static so OnStart can reuse it without
	// re-scanning on the same tick.
	static bool IsPossible( CFFBot *me );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;

	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "GetAmmo"; }

private:
	PathFollower m_path;
	CHandle< CBaseEntity > m_ammo;
	bool m_isGoalDispenser;
};

#endif // FF_BOT_GET_AMMO_H
