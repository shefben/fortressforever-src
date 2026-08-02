//========= Fortress Forever Bot =============================================//
//
// CFFBotSpySap — actively sabotage enemy buildings.
//
// FF sabotage works passively: a stationary spy looking at an enemy buildable
// within 100u runs a 3-second timer (CFFPlayer::SpySabotageThink), and the
// building flips to sabotaged. This action drives the spy to that exact
// stationary-look state, then chains to the next building or releases.
//
// Ported from hl2_src tf_bot_spy_sap (which used a sapper weapon entity).
// FF doesn't have a separate sapper item; the trigger is positional.
//
//===========================================================================//

#ifndef FF_BOT_SPY_SAP_H
#define FF_BOT_SPY_SAP_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBuildableObject;

class CFFBotSpySap : public Action< CFFBot >
{
public:
	CFFBotSpySap( CFFBuildableObject *sapTarget = NULL );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const OVERRIDE;
	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType IsHindrance( const INextBot *me, CBaseEntity *blocker ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "SpySap"; }

	// Static helper: find nearest enemy buildable that can still be sabotaged.
	// Used both at OnStart (target = NULL constructor) and during Update to
	// chain after a sabotage completes.
	static CFFBuildableObject *FindNearestSappableTarget( CFFBot *me );

private:
	CHandle< CFFBuildableObject > m_sapTarget;
	CountdownTimer m_repathTimer;
	PathFollower m_path;
};

#endif // FF_BOT_SPY_SAP_H
