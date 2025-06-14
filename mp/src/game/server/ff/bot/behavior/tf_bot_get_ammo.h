//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_get_ammo.h
// Pick up any nearby ammo
// Michael Booth, May 2009

#ifndef FF_BOT_GET_AMMO_H
#define FF_BOT_GET_AMMO_H

#include "ff_powerup.h"

class CFFBotGetAmmo : public Action< CFFBot >
{
public:
	CFFBotGetAmmo( void );

	static bool IsPossible( CFFBot *me );			// return true if this Action has what it needs to perform right now

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?

	virtual const char *GetName( void ) const	{ return "GetAmmo"; };

private:
	PathFollower m_path;
	CHandle< CBaseEntity > m_ammo;
	bool m_isGoalDispenser;
};


#endif // FF_BOT_GET_AMMO_H
