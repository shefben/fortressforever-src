//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_defend_point_block_capture.h
// Move to and defend current point from capture
// Michael Booth, February 2009

#ifndef FF_BOT_DEFEND_POINT_BLOCK_CAPTURE_H
#define FF_BOT_DEFEND_POINT_BLOCK_CAPTURE_H


class CFFBotDefendPointBlockCapture : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual EventDesiredResult< CFFBot > OnTerritoryContested( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryCaptured( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );

	virtual QueryResultType			ShouldHurry( const INextBot *me ) const;							// are we in a hurry?
	virtual QueryResultType			ShouldRetreat( const INextBot *me ) const;

	virtual const char *GetName( void ) const	{ return "BlockCapture"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CTeamControlPoint *m_point;
	CTFNavArea *m_defenseArea;

	bool IsPointSafe( CFFBot *me );
};


#endif // FF_BOT_DEFEND_POINT_BLOCK_CAPTURE_H
