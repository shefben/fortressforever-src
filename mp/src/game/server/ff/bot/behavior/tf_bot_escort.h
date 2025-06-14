//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_escort.cpp
// Move near an entity and protect it
// Michael Booth, April 2011

#ifndef FF_BOT_ESCORT_H
#define FF_BOT_ESCORT_H

#include "Path/NextBotChasePath.h"

class CFFBotEscort : public Action< CFFBot >
{
public:
	CFFBotEscort( CBaseEntity *who );
	virtual ~CFFBotEscort() { }

	void SetWho( CBaseEntity *who );
	CBaseEntity *GetWho( void ) const;

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?

	virtual EventDesiredResult< CFFBot > OnCommandApproach( CFFBot *me, const Vector &pos, float range );

	virtual const char *GetName( void ) const	{ return "Escort"; }

private:
	CHandle< CBaseEntity > m_who;
	PathFollower m_pathToWho;
	CountdownTimer m_vocalizeTimer;
	CountdownTimer m_repathTimer;
};

#endif // FF_BOT_ESCORT_H
