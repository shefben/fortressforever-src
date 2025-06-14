//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_retreat.cpp
// Retreat towards our spawn to find another patient
// Michael Booth, May 2009

#ifndef FF_BOT_MEDIC_RETREAT_H
#define FF_BOT_MEDIC_RETREAT_H

#include "Path/NextBotChasePath.h"

class CFFBotMedicRetreat : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );
	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;

	virtual const char *GetName( void ) const	{ return "Retreat"; };

private:
	PathFollower m_path;
	CountdownTimer m_lookAroundTimer;
};

#endif // FF_BOT_MEDIC_RETREAT_H
