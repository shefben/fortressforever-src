//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_use_teleporter.h
// Ride a friendly teleporter
// Michael Booth, May 2010

#ifndef FF_BOT_USE_TELEPORTER_H
#define FF_BOT_USE_TELEPORTER_H

#include "ff_obj_teleporter.h"
#include "Path/NextBotPathFollow.h"

class CFFBotUseTeleporter : public Action< CFFBot >
{
public:
	enum UseHowType
	{
		USE_IF_READY,
		ALWAYS_USE
	};
	CFFBotUseTeleporter( CObjectTeleporter *teleporter, UseHowType how = USE_IF_READY );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "UseTeleporter"; };

private:
	CHandle< CObjectTeleporter > m_teleporter;		// the teleporter we're trying to use
	UseHowType m_how;

	PathFollower m_path;
	CountdownTimer m_repathTimer;

	bool m_isInTransit;

	bool IsTeleporterAvailable( void ) const;
};


#endif // FF_BOT_USE_TELEPORTER_H
