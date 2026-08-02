//========= Fortress Forever Bot =============================================//
//
// CFFBotTagger — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_tagger.h"
#include "ff_bot_autotag.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_nav_builder.h"
#include "ff_bot_lua_objectives.h"
#include "ff_info_script.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "nav_mesh.h"

#include "omnibot_interface.h"	// Omnibot::kFlag, kFlagCap, kBackPack_*, TF_TEAM_*

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// FF spawn-room flood-fill parameters.
//
// FFBOT_SPAWN_ROOM_RADIUS — 2D Euclidean radius from each spawn entity. Was
// 600u; lowered to 400u because well2 (and other large maps) have spawn
// entities scattered across the back of the room, and 600u from each one
// flooded out the front wall into the courtyard, tagging outdoor battlements
// as spawn-room. The user-visible symptom: snipers couldn't pick the front-
// wall perches because the autotagger excluded them as "in spawn".
//
// FFBOT_SPAWN_ROOM_Z_DELTA — vertical band around the spawn entity's Z. The
// flood is 2D-only by Source's API, so without this filter a battlement
// directly ABOVE the spawn floor (different floor entirely) gets tagged
// spawn-room. 96u ≈ player crouch height — anything more than that is a
// different floor and shouldn't be lumped with the spawn room.
#define FFBOT_SPAWN_ROOM_RADIUS		400.0f
#define FFBOT_SPAWN_ROOM_Z_DELTA	96.0f


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
// Find the nearest nav area to the given world position. Returns NULL if no
// mesh / no walkable area within FFBOT_ENTITY_NAV_RANGE (which most likely
// means the entity is floating or the .nav doesn't cover it).
//-----------------------------------------------------------------------------
#define FFBOT_ENTITY_NAV_RANGE		512.0f

static CFFNavArea *AreaForPosition( const Vector &pos )
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return NULL;

	CNavArea *area = TheNavMesh->GetNearestNavArea( pos, false,
		FFBOT_ENTITY_NAV_RANGE, false, true, TEAM_ANY );
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
		float spawnZ;	// spawn entity's Z; reject areas too far above/below

		bool operator()( CNavArea *a )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( a );
			// Z filter: 2D radius alone tags balconies above the spawn
			// floor as "spawn", and basements below same. Reject areas
			// whose center is more than FFBOT_SPAWN_ROOM_Z_DELTA from
			// the spawn entity's elevation — those are different floors.
			if ( fabsf( area->GetCenter().z - spawnZ ) > FFBOT_SPAWN_ROOM_Z_DELTA )
				return true;	// continue iteration but don't tag
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
			//
			// Matches both with-underscore ("blue_spawn") and without-
			// underscore ("bluespawn") naming. well2 and several other
			// maps use the no-underscore form.
			if ( attr == 0 )
			{
				const char *name = STRING( pSpot->GetEntityName() );
				int nameTeam = TEAM_UNASSIGNED;
				if ( name && name[ 0 ] )
				{
					// Order matters here — check longer prefixes first so
					// we don't match "yellow*" as "y*" or "green*" as "g*".
					if ( !V_strnicmp( name, "yellow", 6 ) ||
					     !V_strnicmp( name, "yel", 3 ) )
						nameTeam = TEAM_YELLOW;
					else if ( !V_strnicmp( name, "green", 5 ) ||
					          !V_strnicmp( name, "grn", 3 ) )
						nameTeam = TEAM_GREEN;
					else if ( !V_strnicmp( name, "blue", 4 ) ||
					          !V_strnicmp( name, "blu", 3 ) )
						nameTeam = TEAM_BLUE;
					else if ( !V_strnicmp( name, "red", 3 ) )
						nameTeam = TEAM_RED;
				}
				if ( nameTeam != TEAM_UNASSIGNED )
				{
					team = nameTeam;
					attr = CFFNavArea::SpawnRoomAttributeForTeam( team );
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

			SpawnRoomTagger tagger = { attr, team, mesh, &spawnTagged,
				pSpot->GetAbsOrigin().z };
			TheNavMesh->ForAllAreasInRadius( tagger, pSpot->GetAbsOrigin(), FFBOT_SPAWN_ROOM_RADIUS );
		}
	}

	Msg( "[CFFBotTagger] info_ff_teamspawn: %d total, %d skipped (no team), "
		"per-team B/R/Y/G = %d/%d/%d/%d\n",
		spawnEntsConsidered, spawnEntsSkippedNoTeam,
		perTeamSpawnEnts[ 0 ], perTeamSpawnEnts[ 1 ],
		perTeamSpawnEnts[ 2 ], perTeamSpawnEnts[ 3 ] );

	// ---- Lua-declared goal entities ------------------------------------
	//
	// Source of truth is FFBotLuaObjectives, which tracks every entity Lua
	// declared through SetBotGoalInfo and keeps its live state current. Two
	// things that changes versus walking gEntList here:
	//
	//   * trigger_ff_script goals are included. CFuncFFScript has the same
	//     GetBotGoalType / GetBotTeamFlags / IsActive interface as
	//     CFFInfoScript and includes/base.lua calls SetBotGoalInfo on it, but
	//     this pass only ever looked at CLASS_INFOSCRIPT, so trigger-based
	//     goals were invisible to the entire bot layer.
	//
	//   * Dead goals are skipped. base_ad.lua and friends call Restore() and
	//     Remove() on flags and caps as the round changes phase; tagging all
	//     of them from level init had bots walking to objectives that, to a
	//     player, do not exist yet.
	//
	// A goal that has been removed also has its position invalidated, so
	// tagging uses the home position — where Lua first put it — rather than
	// wherever a carrier has since dragged it.
	{
		int goalsSkippedDead = 0;
		int goalsSkippedNoNav = 0;
		int triggerGoals = 0;

		for ( int g = 0; g < FFBotLuaObjectives::Count(); ++g )
		{
			const FFBotLuaGoal *goal = FFBotLuaObjectives::Get( g );
			if ( !goal )
				continue;

			if ( !goal->isLive )
			{
				++goalsSkippedDead;
				continue;
			}

			CBaseEntity *pEnt = goal->entity.Get();
			if ( !pEnt )
				continue;

			int teams[ 4 ];
			int teamCount = BotTeamFlagsToTeams( goal->teamFlags, teams );

			// Tag where the objective LIVES, not where it currently is. A flag
			// being carried across the map shouldn't repaint the nav mesh
			// along the carrier's route.
			CFFNavArea *area = AreaForPosition( goal->homePos );
			if ( !area )
			{
				++goalsSkippedNoNav;
				continue;
			}

			if ( goal->isTrigger )
				++triggerGoals;

			switch ( goal->goalType )
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

		Msg( "[CFFBotTagger] Lua goals: %d tracked, %d live-tagged "
			"(%d from trigger_ff_script), %d skipped inactive/removed, "
			"%d with no nav coverage.\n",
			FFBotLuaObjectives::Count(),
			flagTagged + capTagged + resupplyTagged,
			triggerGoals, goalsSkippedDead, goalsSkippedNoNav );

		if ( FFBotLuaObjectives::Count() == 0 )
		{
			Msg( "[CFFBotTagger] This map's Lua declares no bot goals at all "
				"(no 'botgoaltype' on any entity). Roughly half the shipped map "
				"scripts don't. Use ff_manual_nav_builder to author objectives "
				"by hand.\n" );
		}
	}

	// ---- Hand-authored markers -----------------------------------------
	// After the entity pass so a manual marker can supplement or override what
	// the entities say, and before spawn-exit collection / incursion distances
	// so a hand-drawn spawn room participates in them exactly like a real one.
	FFNavBuilder::ApplyToMesh( mesh );

	// ---- Mark threshold areas + per-team spawn-exit lists ---------------
	mesh->CollectAndMarkSpawnRoomExits();

	// ---- Per-team incursion distance flood-fill -------------------------
	mesh->ComputeIncursionDistances();

	// ---- Per-area invasion vectors (depend on incursion distances) ------
	mesh->ComputeInvasionAreas();

	// ---- Heuristic auto-tagging (water, sniper, sentry, choke, etc.) ----
	// Runs AFTER all entity-derived tags + incursion calculations are done
	// so the heuristics can use spawn-room / flag / cap tags as inputs.
	CFFBotAutoTagger::TagAllAreas( mesh );

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
