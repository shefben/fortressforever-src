#ifndef FF_BOT_STATE_HIDE_H
#define FF_BOT_STATE_HIDE_H

#include "../ff_bot.h" // For BotState, CFFBot, CNavArea, Vector
#include "utlcommon.h"  // For CountdownTimer

class HideState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot );
	virtual void OnUpdate( CFFBot *bot );
	virtual void OnExit( CFFBot *bot );
	virtual const char *GetName( void ) const { return "Hide"; }

	void SetHidingSpot( const Vector &pos ) { m_hidingSpot = pos; }
	const Vector &GetHidingSpot( void ) const { return m_hidingSpot; }
	void SetSearchArea( CNavArea *area ) { m_searchFromArea = area; }
	void SetSearchRange( float range ) { m_range = range; }
	void SetDuration( float time ) { m_duration = time; }
	void SetHoldPosition( bool hold ) { m_isHoldingPosition = hold; }
	bool IsAtSpot( void ) const { return m_isAtSpot; }
	float GetHideTime( void ) const;

private:
	CNavArea *m_searchFromArea;
	float m_range;
	Vector m_hidingSpot;
	bool m_isLookingOutward;
	bool m_isAtSpot;
	float m_duration;
	CountdownTimer m_hideTimer;
	bool m_isHoldingPosition;
	float m_holdPositionTime;
	bool m_heardEnemy;
	float m_firstHeardEnemyTime;
	int m_retry;
	Vector m_leaderAnchorPos;
	bool m_isPaused;
	CountdownTimer m_pauseTimer;
};

#endif // FF_BOT_STATE_HIDE_H
