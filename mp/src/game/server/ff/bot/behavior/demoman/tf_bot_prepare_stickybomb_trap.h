//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_prepare_stickybomb_trap.h
// Place stickybombs to create a deadly trap
// Michael Booth, July 2010

#ifndef FF_BOT_PREPARE_STICKYBOMB_TRAP_H
#define FF_BOT_PREPARE_STICKYBOMB_TRAP_H

class CFFBotPrepareStickybombTrap : public Action< CFFBot >
{
public:
	CFFBotPrepareStickybombTrap( void );
	virtual ~CFFBotPrepareStickybombTrap( );

	static bool IsPossible( CFFBot *me );	// Return true if this Action has what it needs to perform right now

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual ActionResult< CFFBot >	OnSuspend( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info );

	virtual QueryResultType			ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?

	virtual const char *GetName( void ) const	{ return "PrepareStickybombTrap"; };

	struct BombTargetArea
	{
		CTFNavArea *m_area;
		int m_count;
	};

private:
	bool m_isFullReloadNeeded;

	CTFNavArea *m_myArea;

	CUtlVector< BombTargetArea > m_bombTargetAreaVector;
	void InitBombTargetAreas( CFFBot *me );
	CountdownTimer m_launchWaitTimer;
};

#endif // FF_BOT_PREPARE_STICKYBOMB_TRAP_H
