//========= Copyright Valve Corporation, All rights reserved. ============//
//
//
//
//=============================================================================
#ifndef FF_BOT_HINT_ENGINEER_NEST_H
#define FF_BOT_HINT_ENGINEER_NEST_H

#include "ff_bot_hint_entity.h"

typedef CUtlVector< CHandle< CBaseTFBotHintEntity > > HintVector_t;

class CFFBotHintSentrygun;
class CFFBotHintTeleporterExit;

class CFFBotHintEngineerNest : public CBaseTFBotHintEntity
{
	DECLARE_CLASS( CFFBotHintEngineerNest, CBaseTFBotHintEntity );
public:
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CFFBotHintEngineerNest( void );
	virtual ~CFFBotHintEngineerNest() { }

	virtual void Spawn() OVERRIDE;

	virtual HintType GetHintType() const OVERRIDE { return HINT_ENGINEER_NEST; }

	virtual int UpdateTransmitState()
	{
		return SetTransmitState( FL_EDICT_ALWAYS );
	}

	void HintThink();
	void HintTeleporterThink();

	bool IsStaleNest() const;
	void DetonateStaleNest();

	CFFBotHintSentrygun* GetSentryHint() const;
	CFFBotHintTeleporterExit* GetTeleporterHint() const;
private:
	void DetonateObjectsFromHints( const HintVector_t& hints );
	CBaseTFBotHintEntity* GetHint( const HintVector_t& hints ) const;

	HintVector_t m_sentries;
	HintVector_t m_teleporters;

	CNetworkVar( bool, m_bHasActiveTeleporter );
};

#endif // FF_BOT_HINT_ENGINEER_NEST_H
