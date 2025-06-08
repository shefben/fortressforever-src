//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef FF_BOT_BUILD_TELE_ENTRANCE_H
#define FF_BOT_BUILD_TELE_ENTRANCE_H

#include "bot/ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * State for an Engineer bot to build a Teleporter Entrance.
 */
class BuildTeleEntranceState : public BotState
{
public:
	BuildTeleEntranceState( void );
	virtual ~BuildTeleEntranceState() { }

	virtual void OnEnter( CFFBot *bot );
	virtual void OnUpdate( CFFBot *bot );
	virtual void OnExit( CFFBot *bot );
	virtual const char *GetName( void ) const		{ return "BuildTeleporterEntrance"; }

	// FF_TODO_CLASS_ENGINEER: Add methods if needed, e.g., SetBuildLocation if bot should build at a specific spot.
	// void SetBuildLocation(const Vector *location) { m_buildLocation = location ? *location : vec3_invalid; }

private:
	CHandle<CBaseEntity> m_teleEntrance; // Handle to the teleporter entrance being built
	CountdownTimer m_actionTimer;        // For timing blueprint placement, hitting, etc.
	bool m_isBlueprintPlaced;            // True once the blueprint is down
	// Vector m_buildLocation;           // FF_TODO_CLASS_ENGINEER: If a specific location is desired.
};

#endif // FF_BOT_BUILD_TELE_ENTRANCE_H
