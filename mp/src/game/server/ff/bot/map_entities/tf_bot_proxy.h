//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_proxy.h
// A Hammer entity that spawns a TFBot and relays events to/from it
// Michael Booth, November 2009

#ifndef FF_BOT_PROXY_H
#define FF_BOT_PROXY_H


class CFFBot;
class CFFBotActionPoint;


class CFFBotProxy : public CPointEntity
{
	DECLARE_CLASS( CFFBotProxy, CPointEntity );
public:
	DECLARE_DATADESC();

	CFFBotProxy( void );
	virtual ~CFFBotProxy() { }

	void Think( void );

	// Input
	void InputSetTeam( inputdata_t &inputdata );
	void InputSetClass( inputdata_t &inputdata );
	void InputSetMovementGoal( inputdata_t &inputdata );
	void InputSpawn( inputdata_t &inputdata );
	void InputDelete( inputdata_t &inputdata );

	void OnInjured( void );
	void OnKilled( void );
	void OnAttackingEnemy( void );
	void OnKilledEnemy( void );

protected:
	// Output
	COutputEvent m_onSpawned;
	COutputEvent m_onInjured;
	COutputEvent m_onKilled;
	COutputEvent m_onAttackingEnemy;
	COutputEvent m_onKilledEnemy;

	char m_botName[64];
	char m_className[64];
	char m_teamName[64];

	string_t m_spawnOnStart;
	string_t m_actionPointName;
	float m_respawnInterval;

	CHandle< CFFBot > m_bot;
	CHandle< CFFBotActionPoint > m_moveGoal;
};


#endif // FF_BOT_PROXY_H
