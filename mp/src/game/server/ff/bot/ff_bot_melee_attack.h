//========= Fortress Forever Bot =============================================//
//
// CFFBotMeleeAttack — switch to melee weapon, chase target, swing.
//
// Foundation for spy backstab and engineer wrench-melee. Mirrors
// hl2_src tf_bot_melee_attack with FF per-class melee weapon table.
//
//===========================================================================//

#ifndef FF_BOT_MELEE_ATTACK_H
#define FF_BOT_MELEE_ATTACK_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotChasePath.h"
#include "ff_bot.h"

class CFFBotMeleeAttack : public Action< CFFBot >
{
public:
	CFFBotMeleeAttack( float giveUpRange = -1.0f );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "MeleeAttack"; }

private:
	float m_giveUpRange;
	ChasePath m_path;
};

#endif // FF_BOT_MELEE_ATTACK_H
