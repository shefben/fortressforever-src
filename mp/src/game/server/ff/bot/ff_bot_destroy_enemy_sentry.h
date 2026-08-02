//========= Fortress Forever Bot =============================================//
//
// CFFBotDestroyEnemySentry — locate and take down an enemy sentry from a
// position outside its kill zone.
//
// Ported from hl2_src tf_bot_destroy_enemy_sentry. The TF version relied
// heavily on PVS-precomputed nav visibility (ForAllPotentiallyVisibleAreas)
// and TF-specific area markers; FF nav lacks both, so this version uses
// traceline LOS and a simpler attack-spot search.
//
// Class restriction: only classes with credible long-range AOE / hitscan
// can clear sentries solo (Soldier RPG, Demoman GL, HWGuy AC, Scout SS).
// Engineer/Medic/Civilian skip; Spy uses sap behavior; Sniper would need
// LOS routing this action doesn't yet do.
//
//===========================================================================//

#ifndef FF_BOT_DESTROY_ENEMY_SENTRY_H
#define FF_BOT_DESTROY_ENEMY_SENTRY_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFSentryGun;

class CFFBotDestroyEnemySentry : public Action< CFFBot >
{
public:
	CFFBotDestroyEnemySentry( void );
	CFFBotDestroyEnemySentry( CFFSentryGun *targetSentry );

	static bool IsPossible( CFFBot *me );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "DestroyEnemySentry"; }

private:
	CHandle< CFFSentryGun > m_targetSentry;
	Vector m_safeAttackSpot;
	bool m_hasSafeAttackSpot;
	bool m_isAttackingSentry;

	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CFFSentryGun *FindClosestEnemySentry( CFFBot *me ) const;
	void ComputeSafeAttackSpot( CFFBot *me );
};

#endif // FF_BOT_DESTROY_ENEMY_SENTRY_H
