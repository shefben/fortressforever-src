//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_escort_squad_leader.h
// Escort the squad leader to their destination
// Michael Booth, Octoboer 2011

#ifndef FF_BOT_ESCORT_SQUAD_LEADER_H
#define FF_BOT_ESCORT_SQUAD_LEADER_H


#include "Path/NextBotPathFollow.h"
#include "bot/behavior/ff_bot_melee_attack.h"


//-----------------------------------------------------------------------------
class CFFBotEscortSquadLeader : public Action< CFFBot >
{
public:
	CFFBotEscortSquadLeader( Action< CFFBot > *actionToDoAfterSquadDisbands = NULL );
	virtual ~CFFBotEscortSquadLeader() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual const char *GetName( void ) const	{ return "EscortSquadLeader"; };

private:
	Action< CFFBot > *m_actionToDoAfterSquadDisbands;
	CFFBotMeleeAttack m_meleeAttackAction;

	PathFollower m_formationPath;
	CountdownTimer m_pathTimer;

	const Vector &GetFormationForwardVector( CFFBot *me );
	Vector m_formationForward;
};


//-----------------------------------------------------------------------------
class CFFBotWaitForOutOfPositionSquadMember : public Action< CFFBot >
{
public:
	virtual ~CFFBotWaitForOutOfPositionSquadMember() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "WaitForOutOfPositionSquadMember"; };

private:
	CountdownTimer m_waitTimer;
};


#endif // FF_BOT_ESCORT_SQUAD_LEADER_H
