//========= Fortress Forever Bot =============================================//
//
// CFFBotTagger — bridges FF's live entity world onto the loaded nav mesh.
//
// FF maps don't carry hand-tagged nav attributes (TFBot's `nav_edit
// nav_set_attribute SPAWN_ROOM_BLUE` model doesn't exist for FF). So the
// tagger walks gEntList at OnServerActivate (after Lua's setup pass) and
// stamps the FF_NAV_SPAWN_ROOM_* / FF_NAV_FLAG_* / FF_NAV_CAP_* / FF_NAV_HAS_*
// / FF_NAV_HUNTED_ESCAPE bits onto the corresponding CFFNavArea instances.
//
// Also runs at OnRoundRestart in case Lua moved goal entities.
//
// Mapper-set persistent attributes (FF_NAV_SNIPER_SPOT, FF_NAV_SENTRY_SPOT,
// FF_NAV_NO_SPAWNING, FF_NAV_UNBLOCKABLE, FF_NAV_HUNTED_ESCAPE) come from
// the .nav file via CFFNavArea::Load and survive — the tagger only stamps
// non-persistent entity-derived bits.
//
//===========================================================================//

#ifndef FF_BOT_TAGGER_H
#define FF_BOT_TAGGER_H
#ifdef _WIN32
#pragma once
#endif

class CFFNavMesh;

class CFFBotTagger
{
public:
	// Stamp nav areas from FF entities, then call ComputeIncursionDistances /
	// ComputeInvasionAreas / CollectAndMarkSpawnRoomExits in order. Single
	// entry point for both server activate and round restart.
	static void TagAreasFromEntities( CFFNavMesh *mesh );
};

#endif // FF_BOT_TAGGER_H
