//========= Copyright Valve Corporation, All rights reserved. ============//
// Michael Booth, September 2012

#ifndef FF_BOT_MVM_ENGINEER_BUILD_TELEPORTER_H
#define FF_BOT_MVM_ENGINEER_BUILD_TELEPORTER_H

class CFFBotHintTeleporterExit;

class CFFBotMvMEngineerBuildTeleportExit : public Action< CFFBot >
{
public:
	CFFBotMvMEngineerBuildTeleportExit( CFFBotHintTeleporterExit *hint );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "MvMEngineerBuildTeleportExit"; };

private:
	CHandle< CFFBotHintTeleporterExit > m_teleporterBuildHint;

	CountdownTimer m_delayBuildTime;
	CountdownTimer m_repathTimer;
	PathFollower m_path;
};

#endif // FF_BOT_MVM_ENGINEER_BUILD_TELEPORTER_H
