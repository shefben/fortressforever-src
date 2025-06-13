//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_taunt.h
// Stand still and play a taunt animation
// Michael Booth, November 2009

#ifndef FF_BOT_TAUNT_H
#define FF_BOT_TAUNT_H


//-----------------------------------------------------------------------------
class CTFBotTaunt : public Action< CTFBot >
{
public:
	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "Taunt"; };

private:
	CountdownTimer m_tauntTimer;
	CountdownTimer m_tauntEndTimer;
	bool m_didTaunt;
};


#endif // FF_BOT_TAUNT_H
