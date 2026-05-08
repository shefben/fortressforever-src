//========= Fortress Forever Bot =============================================//
//
// CFFBotVision — IVision subclass for FF.
//
// Phase 4: filters the candidate set to FF players, applies spy-disguise and
// spy-cloak rules:
//   - A correctly-disguised enemy spy (disguise team == bot's team) is treated
//     as IGNORED — the bot's brain doesn't register them as a threat. Will be
//     upgraded later to "until they hurt a friend".
//   - A cloaked enemy is only "noticed" within close range (~96u). Outside
//     that, IsVisibleEntityNoticed returns false and the bot doesn't see them.
//
//===========================================================================//

#ifndef FF_BOT_VISION_H
#define FF_BOT_VISION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotVisionInterface.h"

class CFFBotVision : public IVision
{
public:
	CFFBotVision( INextBot *bot ) : IVision( bot ) {}

	virtual void  CollectPotentiallyVisibleEntities( CUtlVector< CBaseEntity * > *potentiallyVisible ) OVERRIDE;
	virtual bool  IsIgnored( CBaseEntity *subject ) const OVERRIDE;
	virtual bool  IsVisibleEntityNoticed( CBaseEntity *subject ) const OVERRIDE;

	virtual float GetMaxVisionRange( void ) const OVERRIDE  { return 3000.0f; }
	virtual float GetMinRecognizeTime( void ) const OVERRIDE { return 0.2f; }   // 200ms reaction
	virtual float GetDefaultFieldOfView( void ) const OVERRIDE { return 90.0f; }

	// Custom threat selection: rank known entities by class threat, flag
	// carrier status, distance, recency. Default IVision picks closest
	// visible — that misses high-value targets (medic > distant scout, the
	// guy carrying our flag > anyone else).
	virtual const CKnownEntity *GetPrimaryKnownThreat( bool onlyVisibleThreats = false ) const OVERRIDE;
};

#endif // FF_BOT_VISION_H
