//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_seek_and_destroy.h
// Roam the environment, attacking victims
// Michael Booth, January 2010

#ifndef FF_BOT_SEEK_AND_DESTROY_H
#define FF_BOT_SEEK_AND_DESTROY_H

#include "Path/NextBotChasePath.h"


//
// Roam around the map attacking enemies
//
class CFFBotSeekAndDestroy : public Action< CFFBot >
{
public:
	CFFBotSeekAndDestroy( float duration = -1.0f );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;					// is it time to retreat?
	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?

	virtual EventDesiredResult< CFFBot > OnTerritoryCaptured( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryContested( CFFBot *me, int territoryID );

	virtual const char *GetName( void ) const	{ return "SeekAndDestroy"; };

private:
	CTFNavArea *m_goalArea;
	CTFNavArea *ChooseGoalArea( CFFBot *me );
	bool m_isPointLocked;

	PathFollower m_path;
	CountdownTimer m_repathTimer;
	void RecomputeSeekPath( CFFBot *me );

	CountdownTimer m_giveUpTimer;
};


#endif // FF_BOT_SEEK_AND_DESTROY_H
