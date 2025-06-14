//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_taunt.h
// Stand still and play a taunt animation
// Michael Booth, November 2009

#ifndef FF_BOT_TAUNT_H
#define FF_BOT_TAUNT_H


//-----------------------------------------------------------------------------
class CFFBotTaunt : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "Taunt"; };

private:
	CountdownTimer m_tauntTimer;
	CountdownTimer m_tauntEndTimer;
	bool m_didTaunt;
};


#endif // FF_BOT_TAUNT_H
