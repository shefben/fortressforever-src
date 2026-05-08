//========= Fortress Forever Bot =============================================//
//
// FFBotPersistence — read/write `.ffnav` sidecar files. The stock `.nav`
// file holds geometry; the `.ffnav` holds FF-specific semantic tags that
// are otherwise inferred per-load (sniper hints, sentry hints, water/
// backdoor flags, choke points, near-door tags, class masks).
//
// Format (little-endian):
//   uint32  magic       = 'FFNV'
//   uint32  version     = current schema version (bumped when format changes)
//   uint32  entry_count
//   per-entry:
//     uint32 area_id
//     uint32 ff_tags
//     uint16 class_mask
//
// Loaded at OnServerActivate. Saved by `ff_nav_save` console command after
// running inference + heuristics. If load fails or version mismatches,
// inference re-runs and overwrites the file.
//
//===========================================================================//

#ifndef FF_BOT_PERSISTENCE_H
#define FF_BOT_PERSISTENCE_H
#ifdef _WIN32
#pragma once
#endif

class CFFNavMesh;

namespace FFBotPersistence
{
	// Try to load `<mapname>.ffnav` and apply tags to the mesh. Returns true
	// on success. False means the file is missing, corrupt, or out-of-date
	// version — caller should re-run inference and Save() afterwards.
	bool Load( CFFNavMesh *mesh );

	// Walk the mesh and write tagged areas to `<mapname>.ffnav`.
	bool Save( CFFNavMesh *mesh );
}

#endif // FF_BOT_PERSISTENCE_H
