#ifndef FF_BOT_STATE_BUY_H
#define FF_BOT_STATE_BUY_H

#include "../ff_bot.h" // For BotState, CFFBot

class BuyState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot );
	virtual void OnUpdate( CFFBot *bot );
	virtual void OnExit( CFFBot *bot );
	virtual const char *GetName( void ) const { return "Buy"; }

private:
	bool m_isInitialDelay;
	int m_prefRetries;
	int m_prefIndex;
	int m_retries;
	bool m_doneBuying;
	bool m_buyDefuseKit; // May become FF-specific item
	bool m_buyGrenade;
	bool m_buyShield;    // May become FF-specific item
	bool m_buyPistol;
};

#endif // FF_BOT_STATE_BUY_H
