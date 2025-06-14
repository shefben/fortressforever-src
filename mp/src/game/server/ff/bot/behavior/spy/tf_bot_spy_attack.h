//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_attack.h
// Backstab or pistol, as appropriate
// Michael Booth, June 2010

#ifndef FF_BOT_SPY_ATTACK_H
#define FF_BOT_SPY_ATTACK_H

#include "Path/NextBotChasePath.h"


//-------------------------------------------------------------------------------
class CFFBotSpyAttack : public Action< CFFBot >
{
public:
	CFFBotSpyAttack( CFFPlayer *victim );
	virtual ~CFFBotSpyAttack() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info );
	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL );

	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?
	virtual QueryResultType ShouldHurry( const INextBot *me ) const;							// are we in a hurry?
	virtual QueryResultType ShouldAttack( const INextBot *meBot, const CKnownEntity *them ) const;
	virtual QueryResultType IsHindrance( const INextBot *me, CBaseEntity *blocker ) const;		// use this to signal the enemy we are focusing on, so we dont avoid them

	virtual const CKnownEntity *	SelectMoreDangerousThreat( const INextBot *me, 
															   const CBaseCombatCharacter *subject,
															   const CKnownEntity *threat1, 
															   const CKnownEntity *threat2 ) const;	// return the more dangerous of the two threats to 'subject', or NULL if we have no opinion

	virtual const char *GetName( void ) const	{ return "SpyAttack"; };

private:
	CHandle< CFFPlayer > m_victim;
	ChasePath m_path;
	bool m_isCoverBlown;
	CountdownTimer m_chuckleTimer;
};


#endif // FF_BOT_SPY_ATTACK_H
