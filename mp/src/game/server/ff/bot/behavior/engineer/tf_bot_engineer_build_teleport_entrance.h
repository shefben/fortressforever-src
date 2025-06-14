//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build_teleport_entrance.h
// Engineer building a teleport entrance right outside of the spawn room
// Michael Booth, May 2009

#ifndef FF_BOT_ENGINEER_BUILD_TELEPORT_ENTRANCE_H
#define FF_BOT_ENGINEER_BUILD_TELEPORT_ENTRANCE_H

class CFFBotEngineerBuildTeleportEntrance : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );

	virtual const char *GetName( void ) const	{ return "EngineerBuildTeleportEntrance"; };

private:
	PathFollower m_path;
};

#endif // FF_BOT_ENGINEER_BUILD_TELEPORT_ENTRANCE_H
