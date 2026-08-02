//========= Fortress Forever Bot =============================================//
//
// CFFBotSpyAttack — backstab approach + tranq fallback.
//
// When suspended for an enemy, the spy decides between knife (backstab) and
// tranq (suppression) based on positioning and cover state. Behind a victim
// who hasn't seen us → silent backstab. Cover blown (took damage while not
// disguised, or burning/infected) → switch to tranq and retreat to cover.
//
// Ported from hl2_src tf_bot_spy_attack with FF cloak/disguise adaptations.
// FF doesn't have TF's m_Shared.IsStealthed / TF_COND_DISGUISED machinery —
// we use IsCloaked() / IsDisguised() / IsInfected() and a simple
// "recently injured while exposed" check.
//
//===========================================================================//

#ifndef FF_BOT_SPY_ATTACK_H
#define FF_BOT_SPY_ATTACK_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotChasePath.h"
#include "ff_bot.h"

class CFFPlayer;

class CFFBotSpyAttack : public Action< CFFBot >
{
public:
	CFFBotSpyAttack( CFFPlayer *victim = NULL );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;
	virtual ActionResult< CFFBot > OnResume( CFFBot *me, Action< CFFBot > *interruptingAction ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL ) OVERRIDE;

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType IsHindrance( const INextBot *me, CBaseEntity *blocker ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "SpyAttack"; }

private:
	CHandle< CFFPlayer > m_victim;
	ChasePath m_path;
	CountdownTimer m_decloakTimer;
	bool m_isCoverBlown;
};

#endif // FF_BOT_SPY_ATTACK_H
