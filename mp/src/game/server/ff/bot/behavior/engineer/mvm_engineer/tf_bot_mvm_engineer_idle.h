//========= Copyright Valve Corporation, All rights reserved. ============//
// Michael Booth, September 2012

#ifndef FF_BOT_MVM_ENGINEER_IDLE_H
#define FF_BOT_MVM_ENGINEER_IDLE_H

#include "Path/NextBotPathFollow.h"

class CBaseTFBotHintEntity;
class CFFBotHintSentrygun;
class CFFBotHintTeleporterExit;
class CFFBotHintEngineerNest;

class CFFBotMvMEngineerIdle : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;
	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?
	virtual QueryResultType	ShouldHurry( const INextBot *me ) const;							// are we in a hurry?

	virtual const char *GetName( void ) const	{ return "MvMEngineerIdle"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_sentryInjuredTimer;
	CountdownTimer m_sentryRebuildTimer;
	CountdownTimer m_teleporterRebuildTimer;
	CountdownTimer m_findHintTimer;
	CountdownTimer m_reevaluateNestTimer;

	int m_nTeleportedCount;
	bool m_bTeleportedToHint;
	CHandle< CFFBotHintTeleporterExit > m_teleporterHint;
	CHandle< CFFBotHintSentrygun > m_sentryHint;
	CHandle< CFFBotHintEngineerNest > m_nestHint;

	void TakeOverStaleNest( CBaseTFBotHintEntity* pHint, CFFBot *me );
	bool ShouldAdvanceNestSpot( CFFBot *me );

	void TryToDetonateStaleNest();
	bool m_bTriedToDetonateStaleNest;
};

class CFFBotMvMEngineerHintFinder
{
public:
	static bool FindHint( bool bShouldCheckForBlockingObjects, bool bAllowOutOfRangeNest, CHandle< CFFBotHintEngineerNest >* pFoundNest = NULL );
};


#endif // FF_BOT_MVM_ENGINEER_IDLE_H
