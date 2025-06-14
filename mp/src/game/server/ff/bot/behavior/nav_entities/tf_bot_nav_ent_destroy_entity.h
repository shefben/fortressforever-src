//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_nav_ent_destroy_entity.h
// Destroy the given entity, under nav entity control
// Michael Booth, September 2009

#ifndef FF_BOT_NAV_ENT_DESTROY_ENTITY_H
#define FF_BOT_NAV_ENT_DESTROY_ENTITY_H

#include "Path/NextBotPathFollow.h"
#include "NextBot/NavMeshEntities/func_nav_prerequisite.h"
#include "ff_weapon_pipebomblauncher.h"

class CFFBotNavEntDestroyEntity : public Action< CFFBot >
{
public:
	CFFBotNavEntDestroyEntity( const CFuncNavPrerequisite *prereq );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual const char *GetName( void ) const	{ return "NavEntDestroyEntity"; };

private:
	CHandle< CFuncNavPrerequisite > m_prereq;
	PathFollower m_path;				// how we get to the target
	CountdownTimer m_repathTimer;
	bool m_wasIgnoringEnemies;

	void DetonateStickiesWhenSet( CFFBot *me, CTFPipebombLauncher *stickyLauncher ) const;
	bool m_isReadyToLaunchSticky;
};


#endif // FF_BOT_NAV_ENT_DESTROY_ENTITY_H
