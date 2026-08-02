//========= Fortress Forever Bot =============================================//
//
// FFBotLearnedLinks — automatically learned nav connections.
//
// FIX 11 in the navigation overhaul: a belt-and-braces layer that repairs nav
// meshes on maps we cannot re-author.
//
// The problem it solves: nav generation is imperfect on already-shipped FF
// maps. Doorways that were shut at generation time, gaps that need a running
// jump, water transitions, ramps the sampler skipped — all produce nav areas
// that are adjacent in the world but NOT connected in the graph. A* then
// refuses to route through them, so bots either take absurd detours or find no
// path at all and stand around looking broken.
//
// The fix requires no mapper entities and no hand-editing, which matches the
// project's stated constraint that bots must work with already-created maps:
//
//   1. Watch every player — human or bot — that is actually moving.
//   2. Sample their position roughly every 64 units of travel.
//   3. When two consecutive samples land in nav areas that are NOT connected
//      in the graph, that is direct evidence a traversable route exists that
//      the mesh does not know about. Record it as a candidate.
//   4. After a candidate has been observed CONFIRMATIONS times, commit it as a
//      real one-way nav connection.
//   5. Persist confirmed links to a per-map sidecar so the knowledge survives
//      map changes and server restarts.
//
// Humans playing the map effectively teach the bots its geometry. Bots that
// get through by luck (a bunny hop that cleared a gap) teach each other.
//
//===========================================================================//

#ifndef FF_BOT_LEARNED_LINKS_H
#define FF_BOT_LEARNED_LINKS_H
#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;

namespace FFBotLearnedLinks
{
	// Called from CFFNavMesh::OnServerActivate, after nav areas exist and
	// after entity tagging. Loads the sidecar for the current map and applies
	// every confirmed link to the live nav graph.
	void OnMapLoad( void );

	// Called every server frame from FFBotManager_Tick. Samples player motion
	// and promotes candidates. Cheap: one nav lookup per moving player per
	// ~64 units travelled, not per frame.
	void Update( void );

	// Write confirmed links to the per-map sidecar. Called automatically when
	// new links are committed (throttled) and from the console command.
	void Save( void );

	// Drop all learned links for this map, in memory and on disk. Exposed for
	// the ff_bot_links_clear console command — needed when a map is updated
	// and old links no longer make sense.
	void Clear( void );

	// Diagnostics for ff_bot_links_report.
	void PrintReport( void );

	// Forget any in-flight sampling state for a player (on death / disconnect
	// / teleport), so a teleport across the map isn't recorded as a link.
	void ForgetPlayer( CBasePlayer *player );
}

#endif // FF_BOT_LEARNED_LINKS_H
