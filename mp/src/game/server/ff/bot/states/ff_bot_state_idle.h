#ifndef FF_BOT_STATE_IDLE_H
#define FF_BOT_STATE_IDLE_H

#include "../ff_bot.h" // For BotState and CFFBot declaration

//--------------------------------------------------------------------------------------------------------------
/**
 * The state is invoked when a bot has nothing to do, or has finished what it was doing.
 * A bot never stays in this state - it is the main action selection mechanism.
 */
class IdleState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot );
	virtual void OnUpdate( CFFBot *bot );
	virtual const char *GetName( void ) const		{ return "Idle"; }
};

#endif // FF_BOT_STATE_IDLE_H
