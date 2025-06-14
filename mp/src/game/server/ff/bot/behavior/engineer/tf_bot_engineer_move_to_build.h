//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_move_to_build.h
// Engineer moving into position to build
// Michael Booth, February 2009

#ifndef FF_BOT_ENGINEER_MOVE_TO_BUILD_H
#define FF_BOT_ENGINEER_MOVE_TO_BUILD_H

#include "Path/NextBotPathFollow.h"

class CFFBotHintSentrygun;


class CFFBotEngineerMoveToBuild : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );

	virtual const char *GetName( void ) const	{ return "EngineerMoveToBuild"; };

private:
	CHandle< CFFBotHintSentrygun > m_sentryBuildHint;
	Vector m_sentryBuildLocation;

	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CUtlVector< CTFNavArea * > m_sentryAreaVector;
	float m_totalSurfaceArea;
	void CollectBuildAreas( CFFBot *me );

	void SelectBuildLocation( CFFBot *me );
	CountdownTimer m_fallBackTimer;
};

#endif // FF_BOT_ENGINEER_MOVE_TO_BUILD_H
