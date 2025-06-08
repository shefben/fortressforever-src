#ifndef FF_BOT_STATE_FOLLOW_H
#define FF_BOT_STATE_FOLLOW_H

#include "../ff_bot.h" // For BotState, CFFBot, CFFPlayer, Vector
#include "utlcommon.h"  // For CountdownTimer, IntervalTimer

class FollowState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot );
	virtual void OnUpdate( CFFBot *bot );
	virtual void OnExit( CFFBot *bot );
	virtual const char *GetName( void ) const { return "Follow"; }

	void SetLeader( CFFPlayer *player ) { m_leader = player; }

private:
	CHandle< CFFPlayer > m_leader;
	Vector m_lastLeaderPos;
	bool m_isStopped;
	float m_stoppedTimestamp;

	enum LeaderMotionStateType
	{
		INVALID,
		STOPPED,
		WALKING,
		RUNNING
	};
	LeaderMotionStateType m_leaderMotionState;
	IntervalTimer m_leaderMotionStateTime;

	bool m_isSneaking;
	float m_lastSawLeaderTime;
	CountdownTimer m_repathInterval;
	IntervalTimer m_walkTime;
	bool m_isAtWalkSpeed;
	float m_waitTime;
	CountdownTimer m_idleTimer;

	void ComputeLeaderMotionState( float leaderSpeed );
};

#endif // FF_BOT_STATE_FOLLOW_H
