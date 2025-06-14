//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build.h
// Engineer building his buildings
// Michael Booth, February 2009

#ifndef FF_BOT_ENGINEER_BUILD_H
#define FF_BOT_ENGINEER_BUILD_H

class CFFBotHintTeleporterExit;


class CFFBotEngineerBuild : public Action< CFFBot >
{
public:
	virtual Action< CFFBot > *InitialContainedAction( CFFBot *me );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );

	virtual QueryResultType	ShouldHurry( const INextBot *me ) const;							// are we in a hurry?
	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?

	virtual const char *GetName( void ) const	{ return "EngineerBuild"; };
};


#endif // FF_BOT_ENGINEER_BUILD_H
