//========= Fortress Forever Bot =============================================//
//
// CFFBotMoveToVantagePoint — find and move to a nearby nav area with LOS
// to at least one enemy player. Used by snipers / lookouts to position.
//
// Ported from hl2_src tf_bot_move_to_vantage_point. The TF version delegates
// to CTFBot::FindVantagePoint which uses precomputed nav-area visibility;
// FF nav lacks PVS data, so this implementation does its own BFS + LOS
// trace inside the action.
//
//===========================================================================//

#ifndef FF_BOT_MOVE_TO_VANTAGE_POINT_H
#define FF_BOT_MOVE_TO_VANTAGE_POINT_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"
#include "ff_nav_area.h"

class CFFBotMoveToVantagePoint : public Action< CFFBot >
{
public:
	CFFBotMoveToVantagePoint( float maxTravelDistance = 2000.0f );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "MoveToVantagePoint"; }

private:
	float m_maxTravelDistance;
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CFFNavArea *m_vantageArea;
};

#endif // FF_BOT_MOVE_TO_VANTAGE_POINT_H
