//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_mission_destroy_sentries.h
// Seek and destroy enemy sentries and ignore everything else
// Michael Booth, June 2011

#ifndef FF_BOT_MISSION_DESTROY_SENTRIES_H
#define FF_BOT_MISSION_DESTROY_SENTRIES_H


//-----------------------------------------------------------------------------
class CTFBotMissionDestroySentries : public Action< CTFBot >
{
public:
	CTFBotMissionDestroySentries( CObjectSentrygun *goalSentry = NULL );
	virtual ~CTFBotMissionDestroySentries() { }

	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );
	virtual void					OnEnd( CTFBot *me, Action< CTFBot > *nextAction );

	virtual const char *GetName( void ) const	{ return "MissionDestroySentries"; };

private:
	CHandle< CObjectSentrygun > m_goalSentry;

	CObjectSentrygun *SelectSentryTarget( CTFBot *me );
};


#endif // FF_BOT_MISSION_DESTROY_SENTRIES_H
