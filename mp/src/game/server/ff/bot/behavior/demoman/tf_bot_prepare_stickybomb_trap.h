//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_prepare_stickybomb_trap.h
// Place stickybombs to create a deadly trap
// Michael Booth, July 2010

#ifndef FF_BOT_PREPARE_STICKYBOMB_TRAP_H
#define FF_BOT_PREPARE_STICKYBOMB_TRAP_H

class CTFBotPrepareStickybombTrap : public Action< CTFBot >
{
public:
	CTFBotPrepareStickybombTrap( void );
	virtual ~CTFBotPrepareStickybombTrap( );

	static bool IsPossible( CTFBot *me );	// Return true if this Action has what it needs to perform right now

	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );
	virtual void					OnEnd( CTFBot *me, Action< CTFBot > *nextAction );

	virtual ActionResult< CTFBot >	OnSuspend( CTFBot *me, Action< CTFBot > *interruptingAction );

	virtual EventDesiredResult< CTFBot > OnInjured( CTFBot *me, const CTakeDamageInfo &info );

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
	void InitBombTargetAreas( CTFBot *me );
	CountdownTimer m_launchWaitTimer;
};

#endif // FF_BOT_PREPARE_STICKYBOMB_TRAP_H
