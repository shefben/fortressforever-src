//========= Fortress Forever Bot =============================================//
//
// CFFBotTagger — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_tagger.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_info_script.h"
#include "ff_door_link.h"
#include "ff_bot_mapintel.h"
#include "ff_bot_persistence.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "nav_mesh.h"

#include "omnibot_interface.h"	// Omnibot::kFlag, kFlagCap, kBackPack_*, TF_TEAM_*

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Convert FF bot-team-flags bitmask (bits indexed by Omnibot's TF_TEAM enum,
// where TF_TEAM_BLUE=1) into a list of FF team numbers (TEAM_BLUE..TEAM_GREEN).
// Returns the number of teams written into outTeams (max 4).
//-----------------------------------------------------------------------------
static int BotTeamFlagsToTeams( int botTeamFlags, int outTeams[ 4 ] )
{
	int n = 0;
	if ( botTeamFlags & ( 1 << Omnibot::TF_TEAM_BLUE )   ) outTeams[ n++ ] = TEAM_BLUE;
	if ( botTeamFlags & ( 1 << Omnibot::TF_TEAM_RED )    ) outTeams[ n++ ] = TEAM_RED;
	if ( botTeamFlags & ( 1 << Omnibot::TF_TEAM_YELLOW ) ) outTeams[ n++ ] = TEAM_YELLOW;
	if ( botTeamFlags & ( 1 << Omnibot::TF_TEAM_GREEN )  ) outTeams[ n++ ] = TEAM_GREEN;
	return n;
}


//-----------------------------------------------------------------------------
// Find the nearest nav area to the given entity. Returns NULL if no mesh /
// no walkable area within ~512u (which most likely means the entity is
// floating or the .nav doesn't cover it).
//-----------------------------------------------------------------------------
static CFFNavArea *AreaForEntity( CBaseEntity *pEnt )
{
	if ( !pEnt || !TheNavMesh || !TheNavMesh->IsLoaded() )
		return NULL;

	CNavArea *area = TheNavMesh->GetNearestNavArea( pEnt->GetAbsOrigin(), false, 512.0f, false, true, TEAM_ANY );
	return static_cast< CFFNavArea * >( area );
}


//-----------------------------------------------------------------------------
void CFFBotTagger::TagAreasFromEntities( CFFNavMesh *mesh )
{
	if ( !mesh || !mesh->IsLoaded() )
	{
		return;
	}

	int spawnTagged = 0;
	int flagTagged = 0;
	int capTagged = 0;
	int resupplyTagged = 0;

	// ---- info_ff_teamspawn → SPAWN_<team>, flood-filled --------------------
	// One spawn point covers a single nav area, but a "spawn room" is much
	// bigger. We flood-fill outward from each spawn-area to all nearby areas
	// to mark the whole room. CFFBotPathCost reads these tags and refuses to
	// route the bot into an enemy spawn — critical because those entrances
	// are one-way doors that physically block wrong-team bots.
	const float FFBOT_SPAWN_ROOM_RADIUS = 600.0f;
	{
		struct SpawnRoomTagger {
			int tag;
			int team;
			CFFNavMesh *mesh;
			int *count;
			bool operator()( CNavArea *a )
			{
				CFFNavArea *ffa = static_cast< CFFNavArea * >( a );
				if ( !ffa->HasFFTag( tag ) )
				{
					ffa->AddFFTag( tag );
					mesh->AddSpawnRoomArea( team, ffa );
					++(*count);
				}
				return true;
			}
		};

		// Per-team spawn count for diagnostics — FF map authors sometimes
		// use validspawn Lua predicates instead of the engine's m_iTeamNum,
		// so a spawn entity's team number may not match its real allocator.
		// Logging the raw count helps spot maps where every teamspawn has
		// team=TEAM_UNASSIGNED (skipped) and we need to fall through to
		// Lua-driven detection.
		int spawnEntsConsidered = 0;
		int spawnEntsSkippedNoTeam = 0;
		int perTeamSpawnEnts[ FF_NAV_TEAM_COUNT ] = { 0, 0, 0, 0 };

		CBaseEntity *pSpot = NULL;
		while ( ( pSpot = gEntList.FindEntityByClassT( pSpot, CLASS_TEAMSPAWN ) ) != NULL )
		{
			++spawnEntsConsidered;
			int team = pSpot->GetTeamNumber();
			int tag  = CFFNavArea::SpawnTagForTeam( team );
			if ( tag == 0 )
			{
				++spawnEntsSkippedNoTeam;
				Msg( "[CFFBotTagger] Spawn '%s' at (%.0f,%.0f,%.0f) has no team — skipping. "
					"FF maps using Lua validspawn won't get tagged via this path.\n",
					STRING( pSpot->GetEntityName() ),
					pSpot->GetAbsOrigin().x, pSpot->GetAbsOrigin().y, pSpot->GetAbsOrigin().z );
				continue;
			}

			++perTeamSpawnEnts[ team - TEAM_BLUE ];

			SpawnRoomTagger tagger = { tag, team, mesh, &spawnTagged };
			TheNavMesh->ForAllAreasInRadius( tagger, pSpot->GetAbsOrigin(), FFBOT_SPAWN_ROOM_RADIUS );
		}
		Msg( "[CFFBotTagger] info_ff_teamspawn: %d total, %d skipped (no team), "
			"per-team B/R/Y/G = %d/%d/%d/%d\n",
			spawnEntsConsidered, spawnEntsSkippedNoTeam,
			perTeamSpawnEnts[ 0 ], perTeamSpawnEnts[ 1 ],
			perTeamSpawnEnts[ 2 ], perTeamSpawnEnts[ 3 ] );
	}

	// ---- Mark FF_NAV_SPAWN_EXIT on spawn-tagged areas adjacent to non-spawn -
	// Mirrors TFBot's CTFNavMesh::CollectAndMarkSpawnRoomExits: any spawn-room
	// area that has at least one non-spawn neighbor is a "threshold" — the
	// actual doorway. CFFBot::Spawn / TryExitSpawnOverride aim toward the
	// nearest threshold area to walk the bot out the right side of the room
	// instead of trying to push through walls.
	int spawnExitTagged = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area->HasFFTag( FF_NAV_SPAWN_ANY ) )
			continue;

		bool isExit = false;
		for ( int d = 0; d < NUM_DIRECTIONS && !isExit; ++d )
		{
			const NavConnectVector *adj = area->GetAdjacentAreas( (NavDirType)d );
			if ( !adj )
				continue;
			for ( int j = 0; j < adj->Count(); ++j )
			{
				CFFNavArea *neighbor = static_cast< CFFNavArea * >( ( *adj )[ j ].area );
				if ( neighbor && !neighbor->HasFFTag( FF_NAV_SPAWN_ANY ) )
				{
					isExit = true;
					break;
				}
			}
		}
		if ( isExit )
		{
			area->AddFFTag( FF_NAV_SPAWN_EXIT );
			// Also publish into per-team exit list. An area belongs to
			// every team whose SPAWN_<team> bit is set.
			for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
			{
				if ( area->HasFFTag( CFFNavArea::SpawnTagForTeam( t ) ) )
					mesh->AddSpawnExitArea( t, area );
			}
			++spawnExitTagged;
		}
	}

	// ---- CFFInfoScript goal entities (flag, cap, backpack, ...) ------------
	{
		CBaseEntity *pEnt = NULL;
		while ( ( pEnt = gEntList.FindEntityByClassT( pEnt, CLASS_INFOSCRIPT ) ) != NULL )
		{
			CFFInfoScript *pScript = static_cast< CFFInfoScript * >( pEnt );
			int goalType = pScript->GetBotGoalType();
			if ( goalType == Omnibot::kNone )
				continue;

			int teams[ 4 ];
			int teamCount = BotTeamFlagsToTeams( pScript->GetBotTeamFlags(), teams );

			CFFNavArea *area = AreaForEntity( pScript );
			if ( !area )
				continue;

			switch ( goalType )
			{
			case Omnibot::kFlag:
				if ( teamCount == 0 )
				{
					// Flag with no team flags — uncommon. Tag with neutral marker on all 4.
					for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
					{
						area->AddFFTag( CFFNavArea::FlagTagForTeam( t ) );
						mesh->AddFlagArea( t, area );
					}
				}
				else
				{
					for ( int i = 0; i < teamCount; ++i )
					{
						area->AddFFTag( CFFNavArea::FlagTagForTeam( teams[ i ] ) );
						mesh->AddFlagArea( teams[ i ], area );
					}
				}
				++flagTagged;
				break;

			case Omnibot::kFlagCap:
				if ( teamCount == 0 )
				{
					for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
					{
						area->AddFFTag( CFFNavArea::CapTagForTeam( t ) );
						mesh->AddCapArea( t, area );
					}
				}
				else
				{
					for ( int i = 0; i < teamCount; ++i )
					{
						area->AddFFTag( CFFNavArea::CapTagForTeam( teams[ i ] ) );
						mesh->AddCapArea( teams[ i ], area );
					}
				}
				++capTagged;
				break;

			case Omnibot::kBackPack_Ammo:
				area->AddFFTag( FF_NAV_RESUPPLY | FF_NAV_AMMO );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;
			case Omnibot::kBackPack_Armor:
				area->AddFFTag( FF_NAV_RESUPPLY | FF_NAV_ARMOR );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;
			case Omnibot::kBackPack_Health:
				area->AddFFTag( FF_NAV_RESUPPLY | FF_NAV_HEALTH );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;
			case Omnibot::kBackPack_Grenades:
				area->AddFFTag( FF_NAV_RESUPPLY | FF_NAV_GRENADES );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;

			case Omnibot::kHuntedEscape:
				area->AddFFTag( FF_NAV_HUNTED_ESCAPE );
				break;

			default:
				break;
			}
		}
	}

	int perTeamSpawnRooms[ FF_NAV_TEAM_COUNT ] = { 0, 0, 0, 0 };
	int perTeamSpawnExits[ FF_NAV_TEAM_COUNT ] = { 0, 0, 0, 0 };
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		const CUtlVector< CFFNavArea * > *rooms = mesh->GetSpawnRoomAreas( t );
		const CUtlVector< CFFNavArea * > *exits = mesh->GetSpawnExitAreas( t );
		if ( rooms ) perTeamSpawnRooms[ t - TEAM_BLUE ] = rooms->Count();
		if ( exits ) perTeamSpawnExits[ t - TEAM_BLUE ] = exits->Count();
	}
	Msg( "[CFFBotTagger] Tagged %d spawn-room area(s) (%d threshold/exits), %d flag, %d cap, %d resupply.\n",
		spawnTagged, spawnExitTagged, flagTagged, capTagged, resupplyTagged );
	Msg( "[CFFBotTagger] Per-team spawn-rooms B/R/Y/G = %d/%d/%d/%d, "
		"per-team spawn-exits B/R/Y/G = %d/%d/%d/%d\n",
		perTeamSpawnRooms[ 0 ], perTeamSpawnRooms[ 1 ],
		perTeamSpawnRooms[ 2 ], perTeamSpawnRooms[ 3 ],
		perTeamSpawnExits[ 0 ], perTeamSpawnExits[ 1 ],
		perTeamSpawnExits[ 2 ], perTeamSpawnExits[ 3 ] );

	// One-way door analysis — must run after the nav mesh is loaded but does
	// not depend on the SPAWN_/FLAG_ tags above. It looks at trigger->door
	// entity I/O links and turns each one-way door into blacklisted nav
	// edges that CFFBotPathCost honors.
	CFFDoorLinkRegistry::Get().Build( mesh );

	// Try persisted .ffnav sidecar first (faster + lets mappers tune
	// hints offline). On miss/version-mismatch, run inference fresh and
	// save back so future loads skip it.
	if ( !FFBotPersistence::Load( mesh ) )
	{
		// Map-intelligence inference: water tagging, sniper hints (vantage
		// points), sentry hints (defensive chokepoints), backdoor tags
		// (water + low-connectivity tunnels for spies), choke-point
		// betweenness, near-door areas. Runs after entity tagging because
		// some heuristics check spawn/flag/water tags above.
		FFBotMapIntel::RunLevelInit( mesh );
		// Persist the freshly-inferred tags so we don't redo the work on
		// next map load.
		FFBotPersistence::Save( mesh );
	}

	// Per-team incursion distances + per-area enemy invasion vectors.
	// Mirrors TFBot's ComputeIncursionDistances + ComputeInvasionAreas.
	// Computed AFTER entity tagging because the flood-fill seeds from
	// spawn-room areas (tagged above). Result drives the bot's "look
	// toward enemy approach" defaults.
	mesh->ComputeIncursionDistances();
}
