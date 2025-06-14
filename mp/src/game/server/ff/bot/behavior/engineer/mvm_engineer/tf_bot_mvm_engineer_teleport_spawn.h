//========= Copyright Valve Corporation, All rights reserved. ============//
//
//
//
//=============================================================================

#ifndef FF_BOT_MVM_ENGINEER_TELEPORT_SPAWN_H
#define FF_BOT_MVM_ENGINEER_TELEPORT_SPAWN_H

class CBaseTFBotHintEntity;

class CFFBotMvMEngineerTeleportSpawn : public Action< CFFBot >
{
public:
	CFFBotMvMEngineerTeleportSpawn( CBaseTFBotHintEntity* pHint, bool bFirstTeleportSpawn );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "MvMEngineerTeleportSpawn"; };

private:
	CountdownTimer m_teleportDelay;
	CHandle< CBaseTFBotHintEntity > m_hintEntity;
	bool m_bFirstTeleportSpawn;
};

#endif // FF_BOT_MVM_ENGINEER_TELEPORT_SPAWN_H
