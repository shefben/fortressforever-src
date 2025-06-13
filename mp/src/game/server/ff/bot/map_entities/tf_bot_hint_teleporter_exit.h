//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_hint_teleporter_exit.h
// Designer-placed hint for bot teleporter exit placement
// Michael Booth, May 2010

#ifndef FF_BOT_HINT_TELEPORTER_EXIT_H
#define FF_BOT_HINT_TELEPORTER_EXIT_H

#include "ff_bot_hint_entity.h"

class CTFBotHintTeleporterExit : public CBaseTFBotHintEntity
{
	DECLARE_CLASS( CTFBotHintTeleporterExit, CBaseTFBotHintEntity );
public:
	DECLARE_DATADESC();

	CTFBotHintTeleporterExit( void );
	virtual ~CTFBotHintTeleporterExit() { }

	virtual HintType GetHintType() const OVERRIDE { return HINT_TELEPORTER_EXIT; }
};

#endif // FF_BOT_HINT_TELEPORTER_EXIT_H
