//========= Fortress Forever Bot =============================================//
//
// CFFBotHealTeammate — Action that interrupts the CTF objective to walk a
// medic over to a wounded ally and apply medkit heals until the ally is full
// (or out of reach / dies / no longer wounded).
//
// Suspended from CFFBotMainAction::Update when class==medic and a wounded
// teammate is found within search range.
//
//===========================================================================//

#ifndef FF_BOT_MEDIC_H
#define FF_BOT_MEDIC_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFPlayer;

class CFFBotHealTeammate : public Action< CFFBot >
{
public:
	CFFBotHealTeammate( CFFPlayer *target );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;
	virtual const char *GetName( void ) const OVERRIDE { return "HealTeammate"; }

private:
	EHANDLE        m_target;
	PathFollower   m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_giveUpTimer;	// hard cap so we don't get stuck pursuing forever
};

#endif // FF_BOT_MEDIC_H
