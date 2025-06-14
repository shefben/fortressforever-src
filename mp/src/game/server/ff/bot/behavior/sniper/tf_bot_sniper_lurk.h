//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_sniper_lurk.h
// Move into position and wait for victims
// Michael Booth, October 2009

#ifndef FF_BOT_SNIPER_LURK_H
#define FF_BOT_SNIPER_LURK_H

#include "Path/NextBotPathFollow.h"

class CFFBotHint;

class CFFBotSniperLurk : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );
	virtual ActionResult< CFFBot >	OnSuspend( CFFBot *me, Action< CFFBot > *interruptingAction );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	// Snipers choose their targets a bit differently
	virtual const CKnownEntity *	SelectMoreDangerousThreat( const INextBot *me, 
															   const CBaseCombatCharacter *subject,
															   const CKnownEntity *threat1, 
															   const CKnownEntity *threat2 ) const;	// return the more dangerous of the two threats to 'subject', or NULL if we have no opinion

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?
	virtual QueryResultType ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?

	virtual const char *GetName( void ) const	{ return "SniperLurk"; };

private:
	CountdownTimer m_boredTimer;
	CountdownTimer m_repathTimer;
	PathFollower m_path;
	int m_failCount;

	Vector m_homePosition;			// where we want to snipe from
	bool m_isHomePositionValid;
	bool m_isAtHome;
	bool FindNewHome( CFFBot *me );
	CountdownTimer m_findHomeTimer;
	bool m_isOpportunistic;

	CUtlVector< CHandle< CFFBotHint > > m_hintVector;
	CHandle< CFFBotHint > m_priorHint;
	bool FindHint( CFFBot *me );
};

#endif // FF_BOT_SNIPER_LURK_H
