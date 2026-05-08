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
#include "shareddefs.h"
#include "entitylist.h"
#include "nav_mesh.h"

#include "omnibot_interface.h"	// Omnibot::kFlag, kFlagCap, kBackPack_*, TF_TEAM_*

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// FF spawn-room flood-fill radius. ForAllAreasInRadius walks every nav area
// whose center is within this 2D radius of the spawn entity. 600u covers a
// typical FF spawn room without bleeding too far into corridors.
#define FFBOT_SPAWN_ROOM_RADIUS		600.0f


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
// Stamp every nav area within FFBOT_SPAWN_ROOM_RADIUS of pSpot with the
// per-team SPAWN_ROOM bit. Idempotent across multiple spawn entities of
// the same team; just AddToTail-skips duplicates.
//-----------------------------------------------------------------------------
namespace
{
	struct SpawnRoomTagger
	{
		int spawnAttr;
		int team;
		CFFNavMesh *mesh;
		int *count;

		bool operator()( CNavArea *a )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( a );
			if ( !area->HasAttributeFF( spawnAttr ) )
			{
				area->SetAttributeFF( spawnAttr );
				mesh->AddSpawnRoomArea( team, area );
				++(*count);
			}
			return true;
		}
	};
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

	// ---- info_ff_teamspawn → SPAWN_ROOM_<team> ---------------------------
	int spawnEntsConsidered = 0;
	int spawnEntsSkippedNoTeam = 0;
	int perTeamSpawnEnts[ FF_NAV_TEAM_COUNT ] = { 0, 0, 0, 0 };

	{
		CBaseEntity *pSpot = NULL;
		while ( ( pSpot = gEntList.FindEntityByClassT( pSpot, CLASS_TEAMSPAWN ) ) != NULL )
		{
			++spawnEntsConsidered;
			int team = pSpot->GetTeamNumber();
			int attr = CFFNavArea::SpawnRoomAttributeForTeam( team );

			// Name-based fallback: FF maps drive spawn validity through Lua's
			// validspawn predicate, so some maps leave the spawn entity's
			// team field at TEAM_UNASSIGNED at level-init time. Parse the
			// targetname so those still get tagged.
			if ( attr == 0 )
			{
				const char *name = STRING( pSpot->GetEntityName() );
				int nameTeam = TEAM_UNASSIGNED;
				if ( name && name[ 0 ] )
				{
					if ( !V_strnicmp( name, "blue_", 5 ) || !V_strnicmp( name, "blu_", 4 ) )
						nameTeam = TEAM_BLUE;
					else if ( !V_strnicmp( name, "red_", 4 ) )
						nameTeam = TEAM_RED;
					else if ( !V_strnicmp( name, "yellow_", 7 ) || !V_strnicmp( name, "yel_", 4 ) )
						nameTeam = TEAM_YELLOW;
					else if ( !V_strnicmp( name, "green_", 6 ) || !V_strnicmp( name, "grn_", 4 ) )
						nameTeam = TEAM_GREEN;
				}
				if ( nameTeam != TEAM_UNASSIGNED )
				{
					team = nameTeam;
					attr = CFFNavArea::SpawnRoomAttributeForTeam( team );
					Msg( "[CFFBotTagger] Spawn '%s' at (%.0f,%.0f,%.0f) GetTeamNumber()=0; "
						"name-based fallback assigned team=%d.\n",
						name, pSpot->GetAbsOrigin().x, pSpot->GetAbsOrigin().y,
						pSpot->GetAbsOrigin().z, team );
				}
			}

			if ( attr == 0 )
			{
				++spawnEntsSkippedNoTeam;
				Msg( "[CFFBotTagger] Spawn '%s' at (%.0f,%.0f,%.0f) has no team — skipping.\n",
					STRING( pSpot->GetEntityName() ),
					pSpot->GetAbsOrigin().x, pSpot->GetAbsOrigin().y, pSpot->GetAbsOrigin().z );
				continue;
			}

			++perTeamSpawnEnts[ team - TEAM_BLUE ];

			SpawnRoomTagger tagger = { attr, team, mesh, &spawnTagged };
			TheNavMesh->ForAllAreasInRadius( tagger, pSpot->GetAbsOrigin(), FFBOT_SPAWN_ROOM_RADIUS );
		}
	}

	Msg( "[CFFBotTagger] info_ff_teamspawn: %d total, %d skipped (no team), "
		"per-team B/R/Y/G = %d/%d/%d/%d\n",
		spawnEntsConsidered, spawnEntsSkippedNoTeam,
		perTeamSpawnEnts[ 0 ], perTeamSpawnEnts[ 1 ],
		perTeamSpawnEnts[ 2 ], perTeamSpawnEnts[ 3 ] );

	// ---- CFFInfoScript goal entities (flag, cap, backpack, hunted-escape)
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
					for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
					{
						area->SetAttributeFF( CFFNavArea::FlagAttributeForTeam( t ) );
						mesh->AddFlagArea( t, area );
					}
				}
				else
				{
					for ( int i = 0; i < teamCount; ++i )
					{
						area->SetAttributeFF( CFFNavArea::FlagAttributeForTeam( teams[ i ] ) );
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
						area->SetAttributeFF( CFFNavArea::CapAttributeForTeam( t ) );
						mesh->AddCapArea( t, area );
					}
				}
				else
				{
					for ( int i = 0; i < teamCount; ++i )
					{
						area->SetAttributeFF( CFFNavArea::CapAttributeForTeam( teams[ i ] ) );
						mesh->AddCapArea( teams[ i ], area );
					}
				}
				++capTagged;
				break;

			case Omnibot::kBackPack_Ammo:
				area->SetAttributeFF( FF_NAV_HAS_AMMO );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;
			case Omnibot::kBackPack_Armor:
				area->SetAttributeFF( FF_NAV_HAS_ARMOR );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;
			case Omnibot::kBackPack_Health:
				area->SetAttributeFF( FF_NAV_HAS_HEALTH );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;
			case Omnibot::kBackPack_Grenades:
				area->SetAttributeFF( FF_NAV_HAS_GRENADES );
				mesh->AddResupplyArea( area );
				++resupplyTagged;
				break;

			case Omnibot::kHuntedEscape:
				area->SetAttributeFF( FF_NAV_HUNTED_ESCAPE );
				break;

			default:
				break;
			}
		}
	}

	// ---- Mark threshold areas + per-team spawn-exit lists ---------------
	mesh->CollectAndMarkSpawnRoomExits();

	// ---- Per-team incursion distance flood-fill -------------------------
	mesh->ComputeIncursionDistances();

	// ---- Per-area invasion vectors (depend on incursion distances) ------
	mesh->ComputeInvasionAreas();

	// ---- Diagnostics ----------------------------------------------------
	int perTeamSpawnRooms[ FF_NAV_TEAM_COUNT ] = { 0, 0, 0, 0 };
	int perTeamSpawnExits[ FF_NAV_TEAM_COUNT ] = { 0, 0, 0, 0 };
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		const CUtlVector< CFFNavArea * > *rooms = mesh->GetSpawnRoomAreas( t );
		const CUtlVector< CFFNavArea * > *exits = mesh->GetSpawnRoomExitAreas( t );
		if ( rooms ) perTeamSpawnRooms[ t - TEAM_BLUE ] = rooms->Count();
		if ( exits ) perTeamSpawnExits[ t - TEAM_BLUE ] = exits->Count();
	}
	Msg( "[CFFBotTagger] Tagged %d spawn-room area(s), %d flag, %d cap, %d resupply.\n",
		spawnTagged, flagTagged, capTagged, resupplyTagged );
	Msg( "[CFFBotTagger] Per-team spawn-rooms B/R/Y/G = %d/%d/%d/%d, "
		"per-team spawn-exits B/R/Y/G = %d/%d/%d/%d\n",
		perTeamSpawnRooms[ 0 ], perTeamSpawnRooms[ 1 ],
		perTeamSpawnRooms[ 2 ], perTeamSpawnRooms[ 3 ],
		perTeamSpawnExits[ 0 ], perTeamSpawnExits[ 1 ],
		perTeamSpawnExits[ 2 ], perTeamSpawnExits[ 3 ] );
}
