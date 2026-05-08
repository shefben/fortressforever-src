//========= Fortress Forever Bot =============================================//
//
// CFFBot — Phase 3. Bot has a minimal IIntention with a Wander root action so
// nav-walking can be exercised end-to-end. Real behavior tree comes in Phase 5.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_main_action.h"
#include "ff_bot_vision.h"
#include "ff_bot_body.h"
#include "ff_bot_helpers.h"
#include "ff_bot_intel.h"
#include "ff_bot_mapintel.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_player.h"
#include "ff_team.h"
#include "ff_utils.h"
#include "ff_gamerules.h"
#include "nav_mesh.h"
#include "nav_area.h"

#include <algorithm>

#include "NextBotManager.h"
#include "NextBotBehavior.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( ff_bot, CFFBot );


//-----------------------------------------------------------------------------
// CFFBotIntention — owns the root Behavior. In Phase 3, the root action is
// a simple Wander; later phases will replace it with a class/mode-aware
// behavior selector.
//-----------------------------------------------------------------------------
class CFFBotIntention : public IIntention
{
public:
	CFFBotIntention( CFFBot *me ) : IIntention( me )
	{
		m_behavior = new Behavior< CFFBot >( new CFFBotMainAction, "FFBotIntention" );
	}

	virtual ~CFFBotIntention()
	{
		delete m_behavior;
	}

	virtual void Reset( void ) OVERRIDE
	{
		IIntention::Reset();
		delete m_behavior;
		m_behavior = new Behavior< CFFBot >( new CFFBotMainAction, "FFBotIntention" );
	}

	virtual void Update( void ) OVERRIDE
	{
		m_behavior->Update( static_cast< CFFBot * >( GetBot() ), GetUpdateInterval() );
	}

	virtual INextBotEventResponder *FirstContainedResponder( void ) const OVERRIDE { return m_behavior; }
	virtual INextBotEventResponder *NextContainedResponder( INextBotEventResponder *current ) const OVERRIDE { return NULL; }

private:
	Behavior< CFFBot > *m_behavior;
};


//-----------------------------------------------------------------------------
CFFBot::CFFBot()
{
	// Base NextBot components. We'll subclass these in later phases for class-aware
	// locomotion (scout speed, hwguy spinup), spy-disguise filtering, etc.
	m_locomotor = new PlayerLocomotion( this );
	m_body      = new CFFBotBody( this );
	m_vision    = new CFFBotVision( this );
	m_intention = new CFFBotIntention( this );

	m_bClassDidSpawnInit = false;
	m_sniperFireState = SNIPER_FIRE_IDLE;
	m_sniperFireStartTime = 0.0f;
	m_routeSeed = (unsigned int)RandomInt( 1, 65535 );
	m_lastRouteChokeID = 0;
	m_spawnExitDir.Init();
	m_spawnExitForceTimer.Invalidate();
}

//-----------------------------------------------------------------------------
CFFBot::~CFFBot()
{
	if ( m_intention ) { delete m_intention; m_intention = NULL; }
	if ( m_locomotor ) { delete m_locomotor; m_locomotor = NULL; }
	if ( m_body )      { delete m_body;      m_body      = NULL; }
	if ( m_vision )    { delete m_vision;    m_vision    = NULL; }
}

//-----------------------------------------------------------------------------
void CFFBot::Spawn( void )
{
	// Adaptive role reassignment: before the player Spawn() picks our
	// loadout, swap our class if the team has lost a key role (no engy
	// alive, no medic alive). PickRespawnClass() returns currentClass
	// when no swap is warranted.
	const int currentClass = GetClassSlot();
	if ( currentClass >= CLASS_SCOUT && currentClass <= CLASS_CIVILIAN )
	{
		const int suggested = FFBotIntel::PickRespawnClass( this, currentClass );
		if ( suggested != currentClass && suggested > 0 )
		{
			const char *className = Class_IntToString( suggested );
			if ( className && className[ 0 ] )
			{
				ChangeClass( className );
				Msg( "[CFFBot] Adaptive respawn: '%s' switching from class %d to %d (team need).\n",
					GetPlayerName() ? GetPlayerName() : "?", currentClass, suggested );
			}
		}
	}

	BaseClass::Spawn();

	// Class-specialty per-life state — wiped here so spy re-disguises, engineer
	// re-tries to build, etc. on every respawn.
	m_bClassDidSpawnInit = false;
	m_classBuildTimer.Invalidate();
	m_classDisguiseTimer.Invalidate();
	m_classCloakTimer.Invalidate();
	m_calloutTimer.Invalidate();
	m_weaponSwitchTimer.Invalidate();
	m_retreatTimer.Invalidate();
	m_grenadePrimeStart = 0.0f;
	m_grenadeCooldownTimer.Invalidate();
	m_sniperFireState = SNIPER_FIRE_IDLE;
	m_sniperFireStartTime = 0.0f;
	m_lastThreatTime = 0.0f;
	m_lastThreatPos.Init();
	m_bunnyHopTimer.Invalidate();
	m_lastUnstuckTime = gpGlobals->curtime;
	m_pathInhibitTimer.Invalidate();
	m_recentStuckPos.Init();
	m_recentStuckExpireTime = 0.0f;
	m_lookAroundUntil = 0.0f;

	// Spawn-exit aim override.
	//
	// Root cause we're fighting: CGameRules::GetPlayerSpawnSpot
	// (gamerules.cpp:218) calls SnapEyeAngles( pSpawnSpot->GetLocalAngles() )
	// during BaseClass::Spawn(). That sets the bot's view to whatever angle
	// the mapper gave the info_ff_teamspawn entity, which is often an
	// arbitrary direction (sometimes facing the back wall). Human players
	// turn around as soon as they spawn; bots can't, so PlayerLocomotion
	// ::Approach reads view-vs-goal and presses IN_BACK, walking the bot
	// into whatever is behind their facing.
	//
	// Fix sequence (each tier falls through if the previous returns no
	// usable direction): pick a target point, snap eye angles to face it,
	// seed the body so Upkeep doesn't slew back to a stale m_lookAtPos.
	//
	//   1. Adjacent non-spawn nav area (we're inside spawn, doorway is
	//      one nav-edge away).
	//   2. Two-hop search (multi-area spawn rooms).
	//   3. Nearest SPAWN_EXIT-tagged area for our team (precise threshold
	//      list built by the tagger).
	//   4. Nearest non-spawn nav area within 1500u (covers the case where
	//      the tagger didn't tag our spawn area at all — we still face
	//      *some* non-spawn region).
	//   5. Direction toward our enemy team's nearest cap/flag area
	//      (objective direction; usually points outward from spawn).
	//
	// If even tier 5 fails (no nav, no objectives), the bot keeps the
	// spawn entity's angles. That's the same outcome a human player gets;
	// we just can't do better without a map cue.
	m_spawnExitDir.Init();
	m_spawnExitForceTimer.Invalidate();

	const Vector myPos = GetAbsOrigin();
	Vector targetPos = vec3_origin;
	const char *tier = "none";
	int navTier = 0;	// 0 = no nav, otherwise tier number (1..5) used

	if ( TheNavMesh && TheNavMesh->IsLoaded() )
	{
		CNavArea *startArea = TheNavMesh->GetNearestNavArea(
			myPos, false, 1024.0f, false, true, TEAM_ANY );
		CFFNavArea *here = static_cast< CFFNavArea * >( startArea );
		const bool hereIsSpawn = ( here && here->HasFFTag( FF_NAV_SPAWN_ANY ) );

		// Tier 1+2: adjacent / two-hop non-spawn area.
		if ( hereIsSpawn )
		{
			CFFNavArea *exit = NULL;
			for ( int d = 0; d < NUM_DIRECTIONS && !exit; ++d )
			{
				const NavConnectVector *adj = here->GetAdjacentAreas( (NavDirType)d );
				if ( !adj )
					continue;
				for ( int i = 0; i < adj->Count(); ++i )
				{
					CFFNavArea *cand = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
					if ( cand && !cand->HasFFTag( FF_NAV_SPAWN_ANY ) )
					{
						exit = cand;
						break;
					}
				}
			}
			if ( exit )
			{
				targetPos = exit->GetCenter();
				tier = "T1 adjacent non-spawn";
				navTier = 1;
			}
			else
			{
				for ( int d = 0; d < NUM_DIRECTIONS && !exit; ++d )
				{
					const NavConnectVector *adj = here->GetAdjacentAreas( (NavDirType)d );
					if ( !adj )
						continue;
					for ( int i = 0; i < adj->Count() && !exit; ++i )
					{
						CFFNavArea *step1 = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
						if ( !step1 )
							continue;
						for ( int d2 = 0; d2 < NUM_DIRECTIONS && !exit; ++d2 )
						{
							const NavConnectVector *adj2 = step1->GetAdjacentAreas( (NavDirType)d2 );
							if ( !adj2 )
								continue;
							for ( int j = 0; j < adj2->Count(); ++j )
							{
								CFFNavArea *cand = static_cast< CFFNavArea * >( ( *adj2 )[ j ].area );
								if ( cand && !cand->HasFFTag( FF_NAV_SPAWN_ANY ) )
								{
									exit = cand;
									break;
								}
							}
						}
					}
				}
				if ( exit )
				{
					targetPos = exit->GetCenter();
					tier = "T2 two-hop non-spawn";
					navTier = 2;
				}
			}
		}

		// Tier 3: nearest SPAWN_EXIT-tagged area for our team (the explicit
		// threshold list the tagger built). Useful when our nav area got
		// tagged spawn but neighbor walking didn't find an exit (e.g.,
		// disconnected sub-areas of the same spawn room).
		if ( navTier == 0 )
		{
			CFFNavMesh *mesh = TheFFNavMesh();
			const CUtlVector< CFFNavArea * > *exits =
				mesh ? mesh->GetSpawnExitAreas( GetTeamNumber() ) : NULL;
			if ( exits && exits->Count() > 0 )
			{
				CFFNavArea *best = NULL;
				float bestDistSq = FLT_MAX;
				for ( int i = 0; i < exits->Count(); ++i )
				{
					const float dSq = ( ( *exits )[ i ]->GetCenter() - myPos ).LengthSqr();
					if ( dSq < bestDistSq )
					{
						bestDistSq = dSq;
						best = ( *exits )[ i ];
					}
				}
				if ( best )
				{
					targetPos = best->GetCenter();
					tier = "T3 nearest SPAWN_EXIT";
					navTier = 3;
				}
			}
		}

		// Tier 4: BFS through the nav graph from our start area. The first
		// non-spawn-tagged area we reach via nav edges is the actual
		// REACHABLE exit, regardless of whether the tagger worked.
		//
		// Why this beats Euclidean "nearest non-spawn":
		//   - On 2fort's lower spawn, the side-wall doorway connects (via
		//     nav edges) to a corridor heading UP — but the Euclidean
		//     nearest non-spawn area might be a courtyard chunk on the
		//     other side of a wall. Aiming Euclidean walks the bot into
		//     the wall.
		//   - BFS through the nav graph follows the ACTUAL connectivity:
		//     the first non-spawn area found is the area on the other
		//     side of a real doorway / corridor / ramp.
		//
		// We track the area we reached the non-spawn area FROM ("parent"
		// in BFS terms). Aiming at the parent's center pulls the bot
		// through the doorway threshold rather than toward the far side
		// of the next room.
		if ( navTier == 0 && startArea )
		{
			CUtlVector< CFFNavArea * > openList;
			CUtlVector< CFFNavArea * > parentOf;	// 1:1 with openList
			CUtlVector< CFFNavArea * > visited;

			openList.AddToTail( static_cast< CFFNavArea * >( startArea ) );
			parentOf.AddToTail( NULL );
			visited.AddToTail( static_cast< CFFNavArea * >( startArea ) );

			CFFNavArea *firstNonSpawn = NULL;
			CFFNavArea *thresholdParent = NULL;

			// Cap BFS depth so we don't explore the whole map for no
			// gain. 32 areas is plenty to reach the first doorway from
			// any spawn room.
			int explored = 0;
			while ( explored < openList.Count() && explored < 32 && !firstNonSpawn )
			{
				CFFNavArea *cur = openList[ explored ];
				CFFNavArea *parent = parentOf[ explored ];
				++explored;

				if ( !cur->HasFFTag( FF_NAV_SPAWN_ANY ) && parent != NULL )
				{
					firstNonSpawn = cur;
					thresholdParent = parent;
					break;
				}

				for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
				{
					const NavConnectVector *adj = cur->GetAdjacentAreas( (NavDirType)dir );
					if ( !adj )
						continue;
					for ( int i = 0; i < adj->Count(); ++i )
					{
						CFFNavArea *neighbor = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
						if ( !neighbor )
							continue;
						if ( visited.Find( neighbor ) != visited.InvalidIndex() )
							continue;
						visited.AddToTail( neighbor );
						openList.AddToTail( neighbor );
						parentOf.AddToTail( cur );
					}
				}
			}

			if ( firstNonSpawn )
			{
				// Aim through the threshold (the spawn-side area we
				// reached from). If we reached the non-spawn area
				// directly from start (parent == start), aim straight
				// at the non-spawn area's center.
				if ( thresholdParent && thresholdParent != startArea )
					targetPos = thresholdParent->GetCenter();
				else
					targetPos = firstNonSpawn->GetCenter();
				tier = "T4 BFS first non-spawn";
				navTier = 4;
			}
		}
	}

	// Tier 5: enemy objective direction. Points roughly toward enemy
	// territory regardless of whether nav tagging worked.
	if ( navTier == 0 )
	{
		CFFNavMesh *mesh = TheFFNavMesh();
		if ( mesh )
		{
			Vector best = vec3_origin;
			float bestDistSq = FLT_MAX;
			for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
			{
				if ( t == GetTeamNumber() )
					continue;
				const CUtlVector< CFFNavArea * > *enemyFlags = mesh->GetFlagAreas( t );
				if ( enemyFlags )
				{
					for ( int i = 0; i < enemyFlags->Count(); ++i )
					{
						const float dSq = ( ( *enemyFlags )[ i ]->GetCenter() - myPos ).LengthSqr();
						if ( dSq < bestDistSq )
						{
							bestDistSq = dSq;
							best = ( *enemyFlags )[ i ]->GetCenter();
						}
					}
				}
				const CUtlVector< CFFNavArea * > *enemyCaps = mesh->GetCapAreas( t );
				if ( enemyCaps )
				{
					for ( int i = 0; i < enemyCaps->Count(); ++i )
					{
						const float dSq = ( ( *enemyCaps )[ i ]->GetCenter() - myPos ).LengthSqr();
						if ( dSq < bestDistSq )
						{
							bestDistSq = dSq;
							best = ( *enemyCaps )[ i ]->GetCenter();
						}
					}
				}
			}
			if ( bestDistSq < FLT_MAX )
			{
				targetPos = best;
				tier = "T5 enemy objective";
				navTier = 5;
			}
		}
	}

	// Targeted diagnostics so we can correlate "bot stuck" reports with
	// what nav tagging / search picked. Logged once per spawn (initial
	// and respawn). Includes:
	//   - bot identity (name, team, class)
	//   - bot spawn position
	//   - nearest nav area ID + tag bits
	//   - per-team SPAWN_EXIT count (T3 input — empty = over-tagging
	//     consumed all the threshold areas)
	//   - tier selected, target world pos
	const int diagTeam = GetTeamNumber();
	const int diagClass = GetClassSlot();
	const char *diagName = GetPlayerName() ? GetPlayerName() : "?";
	int diagAreaId = -1;
	int diagAreaTags = 0;
	{
		CNavArea *cur = TheNavMesh ? TheNavMesh->GetNearestNavArea(
			myPos, false, 1024.0f, false, true, TEAM_ANY ) : NULL;
		if ( cur )
		{
			diagAreaId = cur->GetID();
			diagAreaTags = static_cast< CFFNavArea * >( cur )->GetFFTags();
		}
	}
	int diagExitCount = 0;
	if ( CFFNavMesh *mesh = TheFFNavMesh() )
	{
		const CUtlVector< CFFNavArea * > *exits = mesh->GetSpawnExitAreas( diagTeam );
		if ( exits )
			diagExitCount = exits->Count();
	}

	if ( navTier > 0 )
	{
		Vector dir = targetPos - myPos;
		dir.z = 0.0f;
		if ( dir.NormalizeInPlace() > 0.1f )
		{
			// No yaw jitter — the user identified that ±15° rotation can
			// aim a bot into a wall when the doorway is narrow. Spread
			// bots out via path-cost penalties, player avoidance, and
			// HandleWallAvoidance whisker tracing instead.
			m_spawnExitDir = dir;
			m_spawnExitForceTimer.Start( 1.5f );

			// Snap eye angles so PlayerLocomotion::Approach reads the
			// correct view this tick and presses IN_FORWARD.
			QAngle desiredAngles;
			VectorAngles( dir, desiredAngles );
			SnapEyeAngles( desiredAngles );

			IBody *body = GetBodyInterface();
			if ( body )
			{
				Vector lookAt = EyePosition() + dir * 200.0f;
				body->AimHeadTowards( lookAt, IBody::IMPORTANT, 1.0f, NULL, "Spawn exit" );
			}

			Msg( "[CFFBot] spawn-aim '%s' team=%d class=%d pos=(%.0f,%.0f,%.0f) "
				"navArea=%d tags=0x%08x exits[team]=%d tier=%s dir=(%.2f,%.2f) yaw=%.0f\n",
				diagName, diagTeam, diagClass,
				myPos.x, myPos.y, myPos.z,
				diagAreaId, diagAreaTags, diagExitCount,
				tier, dir.x, dir.y, desiredAngles.y );
		}
		else
		{
			Msg( "[CFFBot] spawn-aim '%s' team=%d class=%d navArea=%d tags=0x%08x "
				"exits[team]=%d tier=%s — direction collapsed (target == bot pos).\n",
				diagName, diagTeam, diagClass, diagAreaId, diagAreaTags,
				diagExitCount, tier );
		}
	}
	else
	{
		// No tier matched — emit a clear warning. With BFS T4 added,
		// this should be rare (only happens with no nav loaded at all).
		Msg( "[CFFBot] spawn-aim '%s' team=%d class=%d pos=(%.0f,%.0f,%.0f) "
			"navArea=%d tags=0x%08x exits[team]=%d — NO TIER MATCHED, "
			"bot keeps spawn entity's angles.\n",
			diagName, diagTeam, diagClass,
			myPos.x, myPos.y, myPos.z,
			diagAreaId, diagAreaTags, diagExitCount );
	}
}

//-----------------------------------------------------------------------------
// Static factory used by ClientPutInServerOverride during fake-client creation.
// CFFBot inherits from CBasePlayer (via NextBotPlayer<CFFPlayer>) and so has
// access to the protected static s_PlayerEdict.
//-----------------------------------------------------------------------------
CBasePlayer *CFFBot::AllocatePlayerEntity( edict_t *pEdict, const char *playerName )
{
	CBasePlayer::s_PlayerEdict = pEdict;
	CFFBot *pBot = static_cast< CFFBot * >( CreateEntityByName( "ff_bot" ) );
	if ( pBot )
	{
		pBot->SetPlayerName( playerName );
	}
	return pBot;
}

namespace
{
	int g_FFBotCurrentNumber = 1;
}

//-----------------------------------------------------------------------------
// Build a bitmask of teams that have at least one team-specific
// info_ff_teamspawn on the current map. A spawn with TEAM_UNASSIGNED is
// "generic" (DM / conc jump) and contributes nothing to the mask.
// Returns 0 when the map has *only* generic spawns — caller should treat that
// as "any team is allowed".
//-----------------------------------------------------------------------------
static int FFBot_GetMapSpawnTeamMask( void )
{
	int mask = 0;
	CBaseEntity *pSpawn = NULL;
	while ( ( pSpawn = gEntList.FindEntityByClassname( pSpawn, "info_ff_teamspawn" ) ) != NULL )
	{
		int spawnTeam = pSpawn->GetTeamNumber();
		if ( spawnTeam >= TEAM_BLUE && spawnTeam <= TEAM_GREEN )
			mask |= ( 1 << spawnTeam );
	}
	return mask;
}

//-----------------------------------------------------------------------------
// True if 'team' is allowed on the current map. Two independent checks:
//   1) FF Lua sets m_iMaxPlayers = -1 to *disable* a team (base_ctf.lua does
//      this for kYellow/kGreen on 2-team CTF maps). Reject those outright.
//   2) The team must have a team-specific info_ff_teamspawn — unless the map
//      has only generic spawns (DM / conc-jump), in which case all enabled
//      teams are allowed.
//-----------------------------------------------------------------------------
static bool FFBot_IsTeamAvailableOnMap( int team )
{
	if ( team < TEAM_BLUE || team > TEAM_GREEN )
		return false;

	CFFTeam *pTeam = GetGlobalFFTeam( team );
	if ( !pTeam )
		return false;

	// FF convention: SetPlayerLimit(team, -1) disables a team for the map.
	if ( pTeam->GetTeamLimits() == -1 )
		return false;

	int mask = FFBot_GetMapSpawnTeamMask();
	if ( mask == 0 )
		return true;	// generic-spawn-only map — all enabled teams allowed
	return ( mask & ( 1 << team ) ) != 0;
}

//-----------------------------------------------------------------------------
// Pick the smallest enabled game team that the current map actually supports.
// "Enabled" = at least one class is allowed (GetClassLimit != -1) AND the map
// has a team-specific spawn for that team (or is generic-spawn-only).
// On a tie, prefers the lower team number. Falls back to TEAM_BLUE.
//-----------------------------------------------------------------------------
static int FFBot_PickAutoTeam( void )
{
	int spawnMask = FFBot_GetMapSpawnTeamMask();
	bool restrictBySpawnMask = ( spawnMask != 0 );

	int bestTeam = -1;
	int bestCount = INT_MAX;

	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( restrictBySpawnMask && !( spawnMask & ( 1 << t ) ) )
			continue;	// no team-specific spawn for this team on this map

		CFFTeam *pTeam = GetGlobalFFTeam( t );
		if ( !pTeam )
			continue;

		// FF convention: SetPlayerLimit(team, -1) disables a team on this map.
		if ( pTeam->GetTeamLimits() == -1 )
			continue;

		bool anyClassEnabled = false;
		for ( int c = CLASS_SCOUT; c <= CLASS_CIVILIAN; ++c )
		{
			if ( pTeam->GetClassLimit( c ) != -1 )
			{
				anyClassEnabled = true;
				break;
			}
		}
		if ( !anyClassEnabled )
			continue;

		int count = pTeam->GetNumPlayers();
		if ( count < bestCount )
		{
			bestCount = count;
			bestTeam = t;
		}
	}

	return ( bestTeam == -1 ) ? TEAM_BLUE : bestTeam;
}

//-----------------------------------------------------------------------------
// Count how many players on `team` are currently a given class. Counts both
// human and bot CFFPlayer instances.
//-----------------------------------------------------------------------------
static int FFBot_CountClassOnTeam( int team, int classSlot )
{
	int n = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( pp && pp->GetTeamNumber() == team && pp->GetClassSlot() == classSlot )
			++n;
	}
	return n;
}

//-----------------------------------------------------------------------------
// Pick a class that's allowed on the given team and not at its cap. Biases
// toward role coverage:
//   - if the team has no engineer, strongly prefer engineer
//   - if the team has no medic, strongly prefer medic
//   - otherwise pick uniformly from remaining offensive classes
//
// This prevents 8-bot teams from ending up with 8 soldiers.
//-----------------------------------------------------------------------------
static int FFBot_PickAutoClass( int team )
{
	CFFTeam *pTeam = GetGlobalFFTeam( team );
	if ( !pTeam )
		return CLASS_SCOUT;

	int validClasses[ 10 ];
	int numValid = 0;

	for ( int c = CLASS_SCOUT; c <= CLASS_CIVILIAN; ++c )
	{
		int limit = pTeam->GetClassLimit( c );
		if ( limit == -1 )
			continue;	// disabled on this team

		if ( limit > 0 )
		{
			// Cap is set — count how many on the team are already this class.
			const int currCount = FFBot_CountClassOnTeam( team, c );
			if ( currCount >= limit )
				continue;	// full
		}

		validClasses[ numValid++ ] = c;
	}

	if ( numValid == 0 )
		return CLASS_SCOUT;

	// Role-coverage bias. If the team is missing an engineer or medic and
	// that class is in the valid list, pick it deterministically.
	const bool engineerValid = std::find( validClasses, validClasses + numValid, (int)CLASS_ENGINEER ) != validClasses + numValid;
	const bool medicValid    = std::find( validClasses, validClasses + numValid, (int)CLASS_MEDIC )    != validClasses + numValid;

	if ( engineerValid && FFBot_CountClassOnTeam( team, CLASS_ENGINEER ) == 0 )
		return CLASS_ENGINEER;
	if ( medicValid && FFBot_CountClassOnTeam( team, CLASS_MEDIC ) == 0 )
		return CLASS_MEDIC;

	// Soft-bias: if there's only 1 of either, prefer adding another so we
	// have redundancy when one dies. Otherwise random offense.
	if ( engineerValid && FFBot_CountClassOnTeam( team, CLASS_ENGINEER ) == 1 && RandomInt( 0, 3 ) == 0 )
		return CLASS_ENGINEER;
	if ( medicValid && FFBot_CountClassOnTeam( team, CLASS_MEDIC ) == 1 && RandomInt( 0, 3 ) == 0 )
		return CLASS_MEDIC;

	return validClasses[ RandomInt( 0, numValid - 1 ) ];
}

//-----------------------------------------------------------------------------
CBasePlayer *CreateFFBot( bool bFrozen, int iTeam, int iClass, const char *pszCustomName )
{
	char botname[ 64 ];
	if ( pszCustomName && pszCustomName[0] )
	{
		V_strcpy_safe( botname, pszCustomName );
	}
	else
	{
		Q_snprintf( botname, sizeof( botname ), "Bot%02i", g_FFBotCurrentNumber );
	}

	ClientPutInServerOverride( &CFFBot::AllocatePlayerEntity );
	edict_t *pEdict = engine->CreateFakeClient( botname );
	ClientPutInServerOverride( NULL );

	if ( !pEdict )
	{
		Msg( "Failed to create FF bot.\n" );
		return NULL;
	}

	CFFBot *pBot = static_cast< CFFBot * >( CBaseEntity::Instance( pEdict ) );

	pBot->ClearFlags();
	pBot->AddFlag( FL_CLIENT | FL_FAKECLIENT );

	if ( bFrozen )
		pBot->AddEFlags( EFL_BOT_FROZEN );

	// If caller didn't pick a real game team, auto-balance to the smallest
	// enabled team — otherwise the bot ends up on TEAM_UNASSIGNED and
	// CFFPlayer::ChangeClass returns early because GetGlobalFFTeam() is NULL.
	if ( iTeam < FIRST_GAME_TEAM || iTeam > TEAM_GREEN )
	{
		iTeam = FFBot_PickAutoTeam();
	}
	else if ( !FFBot_IsTeamAvailableOnMap( iTeam ) )
	{
		// User asked for a team the map doesn't support (no team-specific
		// spawn for it). Fall back to auto-pick rather than dead-spawn.
		Msg( "[CFFBot] Requested team %d has no spawn on the current map; auto-picking instead.\n", iTeam );
		iTeam = FFBot_PickAutoTeam();
	}

	pBot->ChangeTeam( iTeam );

	// Validate / replace class. If the requested class is invalid, disabled,
	// or already capped on this team, pick a random allowed one.
	if ( iClass < CLASS_SCOUT || iClass > CLASS_CIVILIAN )
	{
		iClass = FFBot_PickAutoClass( iTeam );
	}
	else
	{
		CFFTeam *pTeam = GetGlobalFFTeam( iTeam );
		if ( pTeam && pTeam->GetClassLimit( iClass ) == -1 )
		{
			Msg( "[CFFBot] Requested class %s is disabled on team %d; picking another.\n",
				Class_IntToString( iClass ), iTeam );
			iClass = FFBot_PickAutoClass( iTeam );
		}
	}

	const char *className = Class_IntToString( iClass );
	if ( className && className[0] )
		pBot->ChangeClass( className );

	pBot->RemoveAllItems( true );
	pBot->Spawn();

	Msg( "[CFFBot] Spawned bot '%s' on team %d as class %s.\n",
		botname, iTeam, className ? className : "?" );

	g_FFBotCurrentNumber++;
	return pBot;
}


//-----------------------------------------------------------------------------
// Periodic autobalance — moves a bot from the largest team to the smallest
// when the gap is >= 2 players. Runs every FFBOT_AUTOBALANCE_INTERVAL seconds
// from Bot_RunAll. Only ever moves bots; never humans.
//-----------------------------------------------------------------------------
#define FFBOT_AUTOBALANCE_INTERVAL	30.0f
#define FFBOT_AUTOBALANCE_THRESHOLD	2

void FFBotManager_Tick( void )
{
	// Intel layer runs every frame: alert decay, flag-stolen detection.
	FFBotIntel::Tick();
	// Map-intel layer: refresh enemy-sentry registry, mancannon nav tags.
	FFBotMapIntel::Tick();

	static CountdownTimer s_balanceTimer;
	if ( !s_balanceTimer.HasStarted() )
	{
		s_balanceTimer.Start( FFBOT_AUTOBALANCE_INTERVAL );
		return;
	}
	if ( !s_balanceTimer.IsElapsed() )
		return;
	s_balanceTimer.Start( FFBOT_AUTOBALANCE_INTERVAL );

	// Tally per-team population. Index by FF team number (TEAM_BLUE..TEAM_GREEN
	// = 2..5); use a small fixed-size array indexed directly.
	const int kFirstTeam = TEAM_BLUE;
	const int kLastTeam  = TEAM_GREEN;

	int teamSize[ 4 ] = { 0, 0, 0, 0 };
	CUtlVector< CFFBot * > teamBots[ 4 ];

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp )
			continue;
		const int t = pp->GetTeamNumber();
		if ( t < kFirstTeam || t > kLastTeam )
			continue;
		const int slot = t - kFirstTeam;
		++teamSize[ slot ];
		if ( pp->IsBot() )
			teamBots[ slot ].AddToTail( static_cast< CFFBot * >( pp ) );
	}

	// Find biggest and smallest map-allowed teams.
	int largestTeam  = -1;
	int smallestTeam = -1;
	int largestSize  = -1;
	int smallestSize = INT_MAX;
	for ( int t = kFirstTeam; t <= kLastTeam; ++t )
	{
		if ( !FFBot_IsTeamAvailableOnMap( t ) )
			continue;
		const int slot = t - kFirstTeam;
		if ( teamSize[ slot ] > largestSize )
		{
			largestSize  = teamSize[ slot ];
			largestTeam  = t;
		}
		if ( teamSize[ slot ] < smallestSize )
		{
			smallestSize = teamSize[ slot ];
			smallestTeam = t;
		}
	}

	if ( largestTeam < 0 || smallestTeam < 0 || largestTeam == smallestTeam )
		return;
	if ( largestSize - smallestSize < FFBOT_AUTOBALANCE_THRESHOLD )
		return;

	// Pick a bot to move. Prefer one that isn't carrying a flag (we don't
	// want to drop the flag mid-game by switching teams). Skip dead bots —
	// they'll just respawn on the new team naturally.
	CFFBot *moveCandidate = NULL;
	const CUtlVector< CFFBot * > &candidates = teamBots[ largestTeam - kFirstTeam ];
	for ( int i = 0; i < candidates.Count(); ++i )
	{
		CFFBot *bot = candidates[ i ];
		if ( !bot )
			continue;
		if ( FFBotHelpers::IsBotCarryingFlag( bot ) )
			continue;
		moveCandidate = bot;
		break;
	}

	if ( !moveCandidate )
		return;

	const char *botName = moveCandidate->GetPlayerName();
	moveCandidate->ChangeTeam( smallestTeam );

	// Re-pick a class that's allowed on the new team — old class may be
	// disabled there or already capped.
	const int newClassSlot = FFBot_PickAutoClass( smallestTeam );
	const char *newClassName = Class_IntToString( newClassSlot );
	if ( newClassName && newClassName[ 0 ] )
		moveCandidate->ChangeClass( newClassName );

	Msg( "[CFFBot] Autobalance: moved '%s' from team %d to team %d (%d vs %d).\n",
		botName ? botName : "?", largestTeam, smallestTeam, largestSize, smallestSize );
}
