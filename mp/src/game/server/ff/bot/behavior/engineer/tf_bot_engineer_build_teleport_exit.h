//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build_teleport_exit.h
// Engineer building a teleport exit
// Michael Booth, May 2010

#ifndef FF_BOT_ENGINEER_BUILD_TELEPORT_EXIT_H
#define FF_BOT_ENGINEER_BUILD_TELEPORT_EXIT_H

class CFFBotEngineerBuildTeleportExit : public Action< CFFBot >
{
public:
	CFFBotEngineerBuildTeleportExit( void );
	CFFBotEngineerBuildTeleportExit( const Vector &buildLocation, float buildAngle );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );

	virtual const char *GetName( void ) const	{ return "EngineerBuildTeleportExit"; };

private:
	PathFollower m_path;

	bool m_hasPreciseBuildLocation;
	Vector m_buildLocation;
	float m_buildAngle;

	CountdownTimer m_giveUpTimer;
	CountdownTimer m_repathTimer;
	CountdownTimer m_getAmmoTimer;
	CountdownTimer m_searchTimer;
};

#endif // FF_BOT_ENGINEER_BUILD_TELEPORT_EXIT_H
