//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Defines the state for a bot following a specific teammate.
//
//=============================================================================//

#ifndef FF_BOT_FOLLOW_TEAMMATE_H
#define FF_BOT_FOLLOW_TEAMMATE_H

#include "bot/ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * State for a bot to follow a specific teammate.
 */
class FollowTeammateState : public BotState
{
public:
	FollowTeammateState( void );
	virtual ~FollowTeammateState() { }

	virtual void OnEnter( CFFBot *bot );
	virtual void OnUpdate( CFFBot *bot );
	virtual void OnExit( CFFBot *bot );
	virtual const char *GetName( void ) const		{ return "FollowTeammate"; }

	void SetPlayerToFollow(CFFPlayer* pPlayer);
	CFFPlayer* GetPlayerToFollow( void ) const { return m_playerToFollow.Get(); }

private:
	CHandle<CFFPlayer> m_playerToFollow;
	CountdownTimer m_repathTimer;        // Timer to repath if stuck or target moves far
	float m_desiredFollowDistance;       // How close the bot tries to stay
};

#endif // FF_BOT_FOLLOW_TEAMMATE_H
