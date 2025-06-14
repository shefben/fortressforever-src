//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_hint_sentrygun.h
// Designer-placed hint for bot sentry placement
// Michael Booth, October 2009

#ifndef FF_BOT_HINT_SENTRYGUN_H
#define FF_BOT_HINT_SENTRYGUN_H

#include "ff_bot_hint_entity.h"

class CFFPlayer;

class CFFBotHintSentrygun : public CBaseTFBotHintEntity
{
public:
	DECLARE_CLASS( CFFBotHintSentrygun, CBaseTFBotHintEntity );
	DECLARE_DATADESC();

	CFFBotHintSentrygun( void );
	virtual ~CFFBotHintSentrygun() { }

	bool IsSticky() const;
	bool IsInUse() const;

	CFFPlayer *GetPlayerOwner() const;
	void SetPlayerOwner( CFFPlayer *pPlayerOwner );

	void IncrementUseCount();
	void DecrementUseCount();

	void OnSentryGunDestroyed( CBaseEntity *pBaseEntity );

	bool IsAvailableForSelection( CFFPlayer *pRequestingPlayer ) const;

	virtual HintType GetHintType() const OVERRIDE { return HINT_SENTRYGUN; }

private:
	bool m_isSticky;
	int m_iUseCount;
	COutputEvent m_outputOnSentryGunDestroyed;

	CHandle< CFFPlayer > m_playerOwner;
};

inline bool CFFBotHintSentrygun::IsSticky() const
{
	return m_isSticky;
}

inline bool CFFBotHintSentrygun::IsInUse() const
{
	return m_iUseCount != 0;
}

inline CFFPlayer *CFFBotHintSentrygun::GetPlayerOwner() const
{
	return m_playerOwner;
}

inline void CFFBotHintSentrygun::SetPlayerOwner( CFFPlayer *pPlayerOwner )
{
	m_playerOwner = pPlayerOwner;
}

inline void CFFBotHintSentrygun::IncrementUseCount()
{
	++m_iUseCount;
}

inline void CFFBotHintSentrygun::DecrementUseCount()
{
	--m_iUseCount;
}

#endif // FF_BOT_HINT_SENTRYGUN_H
