//========= Fortress Forever Bot =============================================//
//
// CFFBotAutoTagger — heuristic auto-tagger that runs after CFFBotTagger.
//
// CFFBotTagger handles entity-derived tags (spawn rooms, flag/cap, resupply
// areas) by inspecting live entities. CFFBotAutoTagger goes further and
// derives tags from the GEOMETRY itself, so maps without per-map nav_edit
// work still get useful semantic tagging:
//
//   FF_NAV_WATER             — feet-level water (wading)
//   FF_NAV_UNDERWATER        — midbody+ water (must swim)
//   FF_NAV_HIGH_GROUND       — area is elevated above its local neighborhood
//   FF_NAV_AUTO_SNIPER_SPOT  — high-ground area with LOS to enemy ingress
//   FF_NAV_AUTO_SENTRY_SPOT  — area near our flag with LOS down a corridor
//   FF_NAV_CHOKE             — narrow area between larger ones
//   FF_NAV_NEAR_LADDER       — adjacent to a ladder
//
// All tags are non-persistent (re-stamped every map load) so they stay in
// sync with the live entity world and are correct after map-script changes.
//
//===========================================================================//

#ifndef FF_BOT_AUTOTAG_H
#define FF_BOT_AUTOTAG_H
#ifdef _WIN32
#pragma once
#endif

class CFFNavMesh;

namespace CFFBotAutoTagger
{
	// Walk every nav area and apply heuristic tags. Call AFTER
	// CFFBotTagger::TagAreasFromEntities so spawn-room / flag / cap
	// tags are already in place (the heuristics use them).
	void TagAllAreas( CFFNavMesh *mesh );
}

#endif // FF_BOT_AUTOTAG_H
