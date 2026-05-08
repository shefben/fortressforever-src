//========= Fortress Forever Bot =============================================//
//
// FFBotMapIntel — level-init heuristics that stamp gameplay-relevant tags
// on the nav mesh once the entity world is up. Adds:
//
//   - Sniper-hint tags: vantage points (elevated areas with broad LOS).
//   - Sentry-hint tags: chokepoint areas adjacent to spawn rooms.
//   - Water tags: areas whose center is in CONTENTS_WATER.
//   - Backdoor tags: water + low-traffic areas (preferred by spies).
//   - Mancannon tags: areas containing a deployed jump pad.
//
// Plus runtime-updated registries:
//
//   - Enemy-sentry positions (so path cost can penalize areas in their LOS).
//   - Friendly-mancannon positions (slight path-cost preference for spies/
//     scouts who can chain jumps).
//
//===========================================================================//

#ifndef FF_BOT_MAPINTEL_H
#define FF_BOT_MAPINTEL_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlvector.h"
#include "mathlib/vector.h"

class CFFNavMesh;
class CFFNavArea;
class CFFSentryGun;

namespace FFBotMapIntel
{
	// Run all level-init heuristics. Call from CFFBotTagger after the
	// entity world is parsed but the nav mesh is still loaded.
	void RunLevelInit( CFFNavMesh *mesh );

	// Per-frame manager hook (called from FFBotManager_Tick). Refreshes
	// transient registries: live enemy sentries, deployed mancannons.
	void Tick( void );

	// ---- Enemy sentry registry ----------------------------------------
	struct SentryInfo
	{
		Vector pos;
		int    team;
	};

	// Get currently-known enemy sentries for `myTeam`. Pointer remains
	// valid until the next Tick().
	const CUtlVector< SentryInfo > &GetEnemySentries( int myTeam );

	// True if a path through `areaCenter` would expose us to an enemy
	// sentry (within `maxRange`, with traceline LOS).
	bool IsExposedToEnemySentry( int myTeam, const Vector &areaCenter, float maxRange = 1200.0f );

	// Find the nearest area tagged FF_NAV_SNIPER_HINT to `fromPos`. Snipers
	// post up here. Returns NULL when no hints exist on the map.
	::CFFNavArea *FindNearestSniperHint( const Vector &fromPos );

	// Same as FindNearestSniperHint but skips any hint that already has a
	// FRIENDLY sniper standing within `exclusionRadius`. Used so 2+ snipers
	// on the same team don't pile onto the same vantage point.
	::CFFNavArea *FindUnoccupiedSniperHint( int myTeam, const Vector &fromPos,
											float exclusionRadius = 200.0f );

	// Find the nearest sentry-hint area to `fromPos`. Engineers prefer
	// these over the flag's own area for SG placement.
	::CFFNavArea *FindNearestSentryHint( const Vector &fromPos );

	// Same as FindNearestSentryHint but skips any hint that already has a
	// FRIENDLY sentry within `exclusionRadius` (default 300u). Used so
	// 2+ engineers on the same team don't pile up SGs at the same hint.
	::CFFNavArea *FindUnoccupiedSentryHint( int myTeam, const Vector &fromPos,
											float exclusionRadius = 300.0f );

	// Refresh the runtime FF_NAV_INTERCEPT_LANE tag. When an enemy is
	// carrying our flag, paths from the carrier toward our base get tagged
	// so defenders' path cost favors the intercept route. Called from Tick.
	void RefreshInterceptLanes( void );

	// Refresh runtime FF_NAV_DANGER_ZONE / hot-zone promotion. Areas with
	// damageScore > threshold get a transient marker so other systems can
	// cheaply query "is this a known kill zone" without re-decaying scores.
	void RefreshHotZones( void );
}

#endif // FF_BOT_MAPINTEL_H
