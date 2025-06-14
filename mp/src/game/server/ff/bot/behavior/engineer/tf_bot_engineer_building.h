//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_building.h
// At building location, constructing buildings
// Michael Booth, May 2010

#ifndef FF_BOT_ENGINEER_BUILDING_H
#define FF_BOT_ENGINEER_BUILDING_H

class CFFBotHintSentrygun;


class CFFBotEngineerBuilding : public Action< CFFBot >
{
public:
	CFFBotEngineerBuilding( void );
	CFFBotEngineerBuilding( CFFBotHintSentrygun *sentryBuildHint );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryCaptured( CFFBot *me, int territoryID );

	virtual const char *GetName( void ) const	{ return "EngineerBuilding"; };

private:
	CountdownTimer m_searchTimer;
	CountdownTimer m_getAmmoTimer;
	CountdownTimer m_repathTimer;
	CountdownTimer m_buildTeleporterExitTimer;

	int m_sentryTriesLeft;

	CountdownTimer m_dispenserRetryTimer;
	CountdownTimer m_teleportExitRetryTimer;

	PathFollower m_path;

	CHandle< CFFBotHintSentrygun > m_sentryBuildHint;

	bool m_hasBuiltSentry;

	enum NearbyMetalType
	{
		NEARBY_METAL_UNKNOWN,
		NEARBY_METAL_NONE,
		NEARBY_METAL_EXISTS
	};

	NearbyMetalType m_nearbyMetalStatus;

	CountdownTimer m_territoryRangeTimer;
	bool m_isSentryOutOfPosition;
	bool CheckIfSentryIsOutOfPosition( CFFBot *me ) const;

	void UpgradeAndMaintainBuildings( CFFBot *me );
	bool IsMetalSourceNearby( CFFBot *me ) const;
};


#endif // FF_BOT_ENGINEER_BUILDING_H
