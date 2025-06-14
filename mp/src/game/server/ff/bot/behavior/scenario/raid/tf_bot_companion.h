//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_companion.h
// Teammate bots for Raid mode
// Michael Booth, October 2009

#ifndef FF_BOT_COMPANION_H
#define FF_BOT_COMPANION_H

#ifdef FF_RAID_MODE

#include "Path/NextBotPathFollow.h"
#include "Path/NextBotChasePath.h"

//
// Friendly teammate bots
//
class CFFBotCompanion : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual const char *GetName( void ) const	{ return "Companion"; };

private:
	ChasePath m_path;
	CTFPlayer *GetLeader( void );
};


//
// Friendly defenders of the base
//
class CFFBotGuardian : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual const char *GetName( void ) const	{ return "Guardian"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
};

#endif // FF_RAID_MODE

#endif // FF_BOT_COMPANION_H
