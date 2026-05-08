//========= Fortress Forever Bot =============================================//
//
// CFFBotTagger — runtime stamp pass that maps live FF entities onto the loaded
// nav mesh. Runs at OnServerActivate (after Lua's setup pass), and again at
// OnRoundRestart in case Lua moved any goal entities.
//
// Tags are stored on CFFNavArea (m_ffBotTags). They are NEVER persisted —
// every map load rebuilds them from current entity state.
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
	// Walk gEntList for spawns / info_ff_script goals / func_ff_script triggers
	// and stamp the corresponding nav areas. Caches per-team area lists on the mesh.
	static void TagAreasFromEntities( CFFNavMesh *mesh );
};


#endif // FF_BOT_TAGGER_H
