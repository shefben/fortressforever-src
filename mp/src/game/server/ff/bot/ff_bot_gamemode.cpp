//========= Fortress Forever Bot =============================================//
//
// FFBotGameMode — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_gamemode.h"
#include "ff_bot.h"
#include "ff_bot_helpers.h"
#include "ff_bot_lua_objectives.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_nav_builder.h"
#include "ff_player.h"
#include "ff_info_script.h"
#include "ff_team.h"
#include "entitylist.h"
#include "nav_mesh.h"

#include "omnibot_interface.h"	// Omnibot::kFlagCap

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_gamemode( "ff_bot_gamemode", "-1", FCVAR_NONE,
	"Override the detected game mode. -1 = auto-detect from the live goal "
	"registry, 0 = unknown, 1 = CTF, 2 = attack/defend, 3 = control point, "
	"4 = invade, 5 = hunted, 6 = fortball, 7 = deathmatch." );

ConVar ff_bot_defense_fraction( "ff_bot_defense_fraction", "-1", FCVAR_NONE,
	"Fraction of each bot team assigned to hold ground, as a percentage. "
	"-1 = pick from the detected game mode." );

ConVar ff_bot_objective_gating( "ff_bot_objective_gating", "1", FCVAR_NONE,
	"Blacklist objectives a bot cannot reach or make progress towards, so it "
	"falls through to the next one. 0 = off, 1 = on, 2 = on and log." );


// How long an objective stays blacklisted for a bot that failed on it. Long
// enough that the bot commits to the alternative and actually gets somewhere,
// short enough that a door opening is noticed within one engagement.
#define FFBOT_OBJECTIVE_BLACKLIST_TIME	45.0f

// How long a bot may pursue an objective without reducing its distance to it
// before we conclude something is in the way. Generous: a long route around a
// building legitimately increases straight-line distance for a while.
#define FFBOT_OBJECTIVE_STALL_TIME		25.0f

// Distance improvement that counts as progress. Below this it's noise.
#define FFBOT_OBJECTIVE_PROGRESS_EPSILON	64.0f

// Blacklist slots per bot. Four is enough to walk down the ladder past every
// item class on any shipped map without evicting the entry that matters.
#define FFBOT_OBJECTIVE_BLACKLIST_SLOTS	4

// Mode re-derivation throttle. Cheap (one pass over a registry of tens of
// entries) but there is no reason to do it per frame.
#define FFBOT_MODE_REDERIVE_INTERVAL	1.0f

// Role quota throttle.
#define FFBOT_ROLE_QUOTA_INTERVAL		2.0f

// A defender picked from FF_NAV2_DEFEND_* ground won't consider posts further
// than this from the thing being defended — an author marking defensive ground
// on the far side of the map meant it for a different objective.
#define FFBOT_DEFEND_POST_MAX_RANGE		2500.0f


//=============================================================================
// State.
//=============================================================================

static int   s_mode            = FFGAMEMODE_UNKNOWN;
static int   s_attackerMask    = 0;		// bit (1 << team)
static int   s_defenderMask    = 0;
static bool  s_modeDirty       = true;
static float s_nextModeTime    = 0.0f;
static float s_nextRoleTime    = 0.0f;

// Diagnostics only — what the last derivation actually saw.
static int   s_lastFlagOwners   = 0;
static int   s_lastCapOwners    = 0;
static int   s_lastNeutralFlags = 0;
static int   s_lastNeutralCaps  = 0;
static int   s_lastLiveGoals    = 0;


//-----------------------------------------------------------------------------
// Per-bot objective bookkeeping, indexed by entindex. Kept here rather than on
// CFFBot because it is entirely this module's business and nothing else reads
// it.
//-----------------------------------------------------------------------------
struct ObjectiveBlacklistEntry
{
	EHANDLE ent;
	float   expireTime;
};

struct BotObjectiveState
{
	ObjectiveBlacklistEntry blacklist[ FFBOT_OBJECTIVE_BLACKLIST_SLOTS ];

	// Progress watchdog for whatever the bot is currently pursuing.
	EHANDLE pursuing;
	float   bestDistance;
	float   bestDistanceTime;
};

static BotObjectiveState s_botState[ MAX_PLAYERS + 1 ];


static BotObjectiveState *StateFor( CFFBot *me )
{
	if ( !me )
		return NULL;
	const int idx = me->entindex();
	if ( idx < 0 || idx > MAX_PLAYERS )
		return NULL;
	return &s_botState[ idx ];
}


//=============================================================================
// Mode detection.
//
// Everything below reads the live goal registry. Not the map name, which is a
// naming convention rather than data, and not the included script name, which
// says what the author started from rather than what the round is doing now.
//=============================================================================

//-----------------------------------------------------------------------------
// Which teams are actually playing. A team counts as in play if it has a player
// on it or if any live goal is declared for it — the second half matters during
// warmup and on empty servers, when the first half says nothing.
//-----------------------------------------------------------------------------
static int ComputeActiveTeamMask( void )
{
	int mask = 0;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player )
			continue;
		const int t = player->GetTeamNumber();
		if ( t >= TEAM_BLUE && t <= TEAM_GREEN )
			mask |= ( 1 << t );
	}

	const int count = FFBotLuaObjectives::Count();
	for ( int i = 0; i < count; ++i )
	{
		const FFBotLuaGoal *goal = FFBotLuaObjectives::Get( i );
		if ( !goal || !goal->isLive || goal->teamFlags == 0 )
			continue;
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			if ( FFBotLuaObjectives::GoalIsForTeam( goal, t ) )
				mask |= ( 1 << t );
		}
	}

	// Absolute fallback so nothing downstream divides by an empty set.
	if ( mask == 0 )
		mask = ( 1 << TEAM_BLUE ) | ( 1 << TEAM_RED );

	return mask;
}


static int CountBits( int mask )
{
	int n = 0;
	while ( mask )
	{
		n += ( mask & 1 );
		mask >>= 1;
	}
	return n;
}


//-----------------------------------------------------------------------------
static void DeriveMode( void )
{
	const int forced = ff_bot_gamemode.GetInt();
	if ( forced >= 0 && forced < FFGAMEMODE_COUNT )
	{
		s_mode = forced;
		// A forced mode still needs sides, and the only honest guess is that
		// the lower-numbered team attacks.
		if ( s_mode == FFGAMEMODE_ATTACK_DEFEND && s_attackerMask == 0 )
		{
			s_attackerMask = ( 1 << TEAM_BLUE );
			s_defenderMask = ( 1 << TEAM_RED );
		}
		return;
	}

	const int activeMask = ComputeActiveTeamMask();

	int  flagOwnerMask   = 0;
	int  capOwnerMask    = 0;
	int  flagTouchMask   = 0;	// teams allowed to take an owned flag
	int  neutralFlags    = 0;
	int  neutralCaps     = 0;
	int  liveBalls       = 0;
	int  liveEscapes     = 0;
	int  liveGoals       = 0;

	const int count = FFBotLuaObjectives::Count();
	for ( int i = 0; i < count; ++i )
	{
		const FFBotLuaGoal *goal = FFBotLuaObjectives::Get( i );
		if ( !goal || !goal->isLive )
			continue;

		++liveGoals;

		switch ( goal->goalClass )
		{
		case FFGOALCLASS_BALL:
			++liveBalls;
			break;

		case FFGOALCLASS_HUNTED_ESCAPE:
			++liveEscapes;
			break;

		case FFGOALCLASS_FLAG:
		case FFGOALCLASS_KEYCARD:
			{
				// A keycard is flag-shaped but it is a gate, not a win
				// condition, so it must not make a map look like CTF. Only a
				// real flag contributes to ownership.
				if ( goal->goalClass != FFGOALCLASS_FLAG )
					break;

				if ( goal->teamFlags == 0 )
				{
					++neutralFlags;
					break;
				}

				// The team that owns a flag is the one that cannot pick it up.
				for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
				{
					if ( !( activeMask & ( 1 << t ) ) )
						continue;
					if ( FFBotLuaObjectives::GoalIsForTeam( goal, t ) )
						flagTouchMask |= ( 1 << t );
					else
						flagOwnerMask |= ( 1 << t );
				}
			}
			break;

		case FFGOALCLASS_CAP:
			{
				if ( goal->teamFlags == 0 )
				{
					++neutralCaps;
					break;
				}
				for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
				{
					if ( !( activeMask & ( 1 << t ) ) )
						continue;
					if ( FFBotLuaObjectives::GoalIsForTeam( goal, t ) )
						capOwnerMask |= ( 1 << t );
				}
			}
			break;

		default:
			break;
		}
	}

	// Hand-authored neutral caps count too. On a map whose control points are
	// brushwork rather than script entities, ff_nav_place capneutral is the
	// only thing that knows they exist.
	if ( neutralCaps == 0 )
		neutralCaps = FFNavBuilder::CountPoints( FFNAVPT_CAP_NEUTRAL );

	const int flagOwners = CountBits( flagOwnerMask );
	const int capOwners  = CountBits( capOwnerMask );

	s_lastFlagOwners   = flagOwners;
	s_lastCapOwners    = capOwners;
	s_lastNeutralFlags = neutralFlags;
	s_lastNeutralCaps  = neutralCaps;
	s_lastLiveGoals    = liveGoals;

	s_attackerMask = 0;
	s_defenderMask = 0;

	if ( liveBalls > 0 )
	{
		s_mode = FFGAMEMODE_FORTBALL;
	}
	else if ( liveEscapes > 0 )
	{
		s_mode = FFGAMEMODE_HUNTED;
	}
	else if ( neutralFlags > 0 && capOwners >= 2 )
	{
		// A flag everyone may take, delivered to team-owned points.
		s_mode = FFGAMEMODE_INVADE;
	}
	else if ( flagOwners >= 2 && capOwners >= 2 )
	{
		s_mode = FFGAMEMODE_CTF;
	}
	else if ( flagOwners == 1 || capOwners == 1 )
	{
		// Asymmetric: one side has something the other side wants. Whoever owns
		// the capture points is doing the capturing.
		s_mode = FFGAMEMODE_ATTACK_DEFEND;
		s_attackerMask = capOwnerMask ? capOwnerMask : flagTouchMask;
		s_defenderMask = activeMask & ~s_attackerMask;
		if ( s_attackerMask == 0 || s_defenderMask == 0 )
		{
			// Degenerate declaration. Fall back to "the team that can take the
			// flag attacks", and if that says nothing either, drop the sides
			// rather than invent them.
			s_attackerMask = flagTouchMask;
			s_defenderMask = activeMask & ~flagTouchMask;
		}
	}
	else if ( neutralCaps > 0 )
	{
		s_mode = FFGAMEMODE_CONTROL_POINT;
	}
	else if ( liveGoals > 0 )
	{
		// Live objectives exist but none of the above shapes fit. Treating it as
		// attack/defend with no known sides gets the bots to the objectives,
		// which is the part that matters.
		s_mode = FFGAMEMODE_ATTACK_DEFEND;
	}
	else
	{
		s_mode = FFGAMEMODE_DEATHMATCH;
	}
}


//-----------------------------------------------------------------------------
int FFBotGameMode::Get( void )
{
	return s_mode;
}


const char *FFBotGameMode::Name( int mode )
{
	switch ( mode )
	{
	case FFGAMEMODE_CTF:			return "ctf";
	case FFGAMEMODE_ATTACK_DEFEND:	return "attack-defend";
	case FFGAMEMODE_CONTROL_POINT:	return "control-point";
	case FFGAMEMODE_INVADE:			return "invade";
	case FFGAMEMODE_HUNTED:			return "hunted";
	case FFGAMEMODE_FORTBALL:		return "fortball";
	case FFGAMEMODE_DEATHMATCH:		return "deathmatch";
	}
	return "unknown";
}


bool FFBotGameMode::IsTeamAttacker( int team )
{
	if ( s_mode != FFGAMEMODE_ATTACK_DEFEND )
		return false;
	if ( team < TEAM_BLUE || team > TEAM_GREEN )
		return false;
	return ( s_attackerMask & ( 1 << team ) ) != 0;
}


bool FFBotGameMode::IsTeamDefender( int team )
{
	if ( s_mode != FFGAMEMODE_ATTACK_DEFEND )
		return false;
	if ( team < TEAM_BLUE || team > TEAM_GREEN )
		return false;
	return ( s_defenderMask & ( 1 << team ) ) != 0;
}


void FFBotGameMode::InvalidateMode( void )
{
	s_modeDirty = true;
}


//=============================================================================
// Objective gating.
//=============================================================================

bool FFBotGameMode::IsObjectiveBlacklisted( CFFBot *me, CBaseEntity *ent )
{
	if ( !ent || ff_bot_objective_gating.GetInt() <= 0 )
		return false;

	BotObjectiveState *state = StateFor( me );
	if ( !state )
		return false;

	for ( int i = 0; i < FFBOT_OBJECTIVE_BLACKLIST_SLOTS; ++i )
	{
		if ( state->blacklist[ i ].ent.Get() != ent )
			continue;
		if ( state->blacklist[ i ].expireTime <= gpGlobals->curtime )
			return false;
		return true;
	}
	return false;
}


void FFBotGameMode::NoteObjectiveFailure( CFFBot *me, CBaseEntity *ent, const char *why )
{
	if ( !ent || ff_bot_objective_gating.GetInt() <= 0 )
		return;

	BotObjectiveState *state = StateFor( me );
	if ( !state )
		return;

	// Reuse the slot holding this entity, then the first expired slot, then the
	// one that expires soonest.
	int slot = -1;
	for ( int i = 0; i < FFBOT_OBJECTIVE_BLACKLIST_SLOTS; ++i )
	{
		if ( state->blacklist[ i ].ent.Get() == ent )
		{
			slot = i;
			break;
		}
	}
	if ( slot < 0 )
	{
		float soonest = FLT_MAX;
		for ( int i = 0; i < FFBOT_OBJECTIVE_BLACKLIST_SLOTS; ++i )
		{
			if ( state->blacklist[ i ].expireTime <= gpGlobals->curtime )
			{
				slot = i;
				break;
			}
			if ( state->blacklist[ i ].expireTime < soonest )
			{
				soonest = state->blacklist[ i ].expireTime;
				slot = i;
			}
		}
	}
	if ( slot < 0 )
		return;

	state->blacklist[ slot ].ent = ent;
	state->blacklist[ slot ].expireTime = gpGlobals->curtime + FFBOT_OBJECTIVE_BLACKLIST_TIME;

	// Whatever we were tracking progress on is no longer it.
	if ( state->pursuing.Get() == ent )
		state->pursuing.Term();

	if ( ff_bot_objective_gating.GetInt() >= 2 )
	{
		Msg( "[FFBotGameMode] %s drops objective '%s': %s\n",
			me ? me->GetPlayerName() : "?",
			ent->GetEntityName() != NULL_STRING ? STRING( ent->GetEntityName() ) : ent->GetClassname(),
			why ? why : "no reason given" );
	}
}


void FFBotGameMode::NoteObjectiveProgress( CFFBot *me, CBaseEntity *ent, float distanceRemaining )
{
	if ( ff_bot_objective_gating.GetInt() <= 0 )
		return;

	BotObjectiveState *state = StateFor( me );
	if ( !state )
		return;

	if ( !ent )
	{
		state->pursuing.Term();
		return;
	}

	if ( state->pursuing.Get() != ent )
	{
		state->pursuing = ent;
		state->bestDistance = distanceRemaining;
		state->bestDistanceTime = gpGlobals->curtime;
		return;
	}

	if ( distanceRemaining < ( state->bestDistance - FFBOT_OBJECTIVE_PROGRESS_EPSILON ) )
	{
		state->bestDistance = distanceRemaining;
		state->bestDistanceTime = gpGlobals->curtime;
		return;
	}

	if ( ( gpGlobals->curtime - state->bestDistanceTime ) >= FFBOT_OBJECTIVE_STALL_TIME )
		NoteObjectiveFailure( me, ent, "no progress towards it" );
}


void FFBotGameMode::ClearObjectiveBlacklist( CFFBot *me )
{
	BotObjectiveState *state = StateFor( me );
	if ( !state )
		return;

	for ( int i = 0; i < FFBOT_OBJECTIVE_BLACKLIST_SLOTS; ++i )
	{
		state->blacklist[ i ].ent.Term();
		state->blacklist[ i ].expireTime = 0.0f;
	}
	state->pursuing.Term();
}


//=============================================================================
// Objective resolution.
//=============================================================================

//-----------------------------------------------------------------------------
// Ladder rank for a carriable. Lower is more urgent.
//
// The keycard rule is the load-bearing one. A keycard is not a scoring
// objective; it exists to open something. So on any map that has one, taking it
// is what to do first, and that is true without knowing what it opens or where
// the thing it opens is. rock2's entire sequence is this rule.
//-----------------------------------------------------------------------------
static int TakeableRank( int goalClass )
{
	switch ( goalClass )
	{
	case FFGOALCLASS_KEYCARD:	return 0;
	case FFGOALCLASS_BALL:		return 1;
	case FFGOALCLASS_FLAG:		return 2;
	}
	return -1;	// not a carriable objective
}


//-----------------------------------------------------------------------------
static CBaseEntity *FindBestTakeable( CFFBot *me, int *outClass )
{
	const Vector myPos = me->GetAbsOrigin();

	CBaseEntity *best = NULL;
	float bestScore = FLT_MAX;
	int   bestClass = FFGOALCLASS_UNKNOWN;

	const int count = FFBotLuaObjectives::Count();
	for ( int i = 0; i < count; ++i )
	{
		const FFBotLuaGoal *goal = FFBotLuaObjectives::Get( i );
		if ( !goal || !goal->isLive || goal->isCarried )
			continue;

		const int rank = TakeableRank( goal->goalClass );
		if ( rank < 0 )
			continue;

		CBaseEntity *ent = goal->entity.Get();
		if ( !ent )
			continue;

		// The touch-permission test is what tells our own flag apart from the
		// enemy's, and what stops a red bot walking across rock2 to a key it is
		// physically incapable of picking up.
		if ( !FFBotLuaObjectives::CanBotTouch( ent, me ) )
			continue;

		if ( FFBotGameMode::IsObjectiveBlacklisted( me, ent ) )
			continue;

		// Rank dominates distance: a keycard on the far side of the map still
		// beats a flag at our feet, because the flag is not takeable until the
		// keycard has been.
		const float dist = ( goal->worldPos - myPos ).Length();
		const float score = (float)rank * 100000.0f + dist;
		if ( score < bestScore )
		{
			bestScore = score;
			best = ent;
			bestClass = goal->goalClass;
		}
	}

	if ( outClass )
		*outClass = bestClass;
	return best;
}


//-----------------------------------------------------------------------------
// Nearest live capture point. `requireMine` restricts to points declared for
// our team; otherwise any live point, including neutral ones, is fair game.
//-----------------------------------------------------------------------------
static CBaseEntity *FindBestCap( CFFBot *me, bool requireMine )
{
	const Vector myPos = me->GetAbsOrigin();
	const int myTeam = me->GetTeamNumber();

	CBaseEntity *best = NULL;
	float bestDistSq = FLT_MAX;

	const int count = FFBotLuaObjectives::Count();
	for ( int i = 0; i < count; ++i )
	{
		const FFBotLuaGoal *goal = FFBotLuaObjectives::Get( i );
		if ( !goal || !goal->isLive || goal->goalClass != FFGOALCLASS_CAP )
			continue;

		if ( requireMine && goal->teamFlags != 0 &&
		     !FFBotLuaObjectives::GoalIsForTeam( goal, myTeam ) )
		{
			continue;
		}

		CBaseEntity *ent = goal->entity.Get();
		if ( !ent )
			continue;
		if ( FFBotGameMode::IsObjectiveBlacklisted( me, ent ) )
			continue;

		const float dSq = ( goal->worldPos - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = ent;
		}
	}

	return best;
}


//-----------------------------------------------------------------------------
// Hand-authored neutral capture ground, for maps whose control points are
// brushwork the script never declares.
//-----------------------------------------------------------------------------
static bool FindAuthoredNeutralCap( const Vector &from, Vector *out )
{
	CFFNavArea *best = NULL;
	float bestDistSq = FLT_MAX;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area->HasAttributeFF2( FF_NAV2_CAP_NEUTRAL ) )
			continue;
		const float dSq = ( area->GetCenter() - from ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}

	if ( !best )
		return false;

	*out = best->GetCenter();
	return true;
}


//-----------------------------------------------------------------------------
// What is this team actually defending? Used as the anchor for defensive post
// selection, and as the "too far away to be relevant" test for authored posts.
//-----------------------------------------------------------------------------
static bool FindDefendedThing( CFFBot *me, Vector *out )
{
	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();

	// Our own flag, if we have one. This is the CTF answer and the attack/
	// defend answer both.
	CFFInfoScript *ownFlag = FFBotHelpers::FindOwnFlag( myTeam );
	if ( ownFlag )
	{
		*out = ownFlag->GetAbsOrigin();
		return true;
	}

	// A capture point declared for the enemy is a point we are defending; a
	// point declared for us on a mode where we're the defenders is one we hold.
	// Either way the geometry is the same and the nearest one is the one we can
	// affect.
	CBaseEntity *cap = FindBestCap( me, false );
	if ( cap )
	{
		*out = cap->GetAbsOrigin();
		return true;
	}

	if ( FindAuthoredNeutralCap( myPos, out ) )
		return true;

	return false;
}


//-----------------------------------------------------------------------------
bool FFBotGameMode::ResolveDefendPosition( CFFBot *me, Vector *out )
{
	if ( !me || !out )
		return false;

	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();

	Vector anchor;
	const bool haveAnchor = FindDefendedThing( me, &anchor );

	// 1) Hand-authored defensive ground for our team, from
	//    ff_nav_place defend. An author who marked a post knows something about
	//    where the attacks come from that no heuristic recovers.
	//
	//    Posts are scored with a per-bot deterministic multiplier so several
	//    defenders spread across the available posts instead of stacking on the
	//    nearest one.
	const int myDefendBit = CFFNavArea::DefendAttributeForTeam( myTeam );
	const int anyDefendBit = FF_NAV2_DEFEND_ANY;

	CFFNavArea *bestPost = NULL;
	float bestPostScore = FLT_MAX;

	for ( int pass = 0; pass < 2 && !bestPost; ++pass )
	{
		// Pass 0 wants posts marked for us specifically. Pass 1 accepts any
		// team's post, which is right for a map authored before teams were
		// worth distinguishing and harmless otherwise.
		const int wantBits = ( pass == 0 ) ? myDefendBit : anyDefendBit;
		if ( wantBits == 0 )
			continue;

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( !area->HasAttributeFF2( wantBits ) )
				continue;

			const Vector p = area->GetCenter();

			if ( haveAnchor &&
			     ( p - anchor ).LengthSqr() > ( FFBOT_DEFEND_POST_MAX_RANGE * FFBOT_DEFEND_POST_MAX_RANGE ) )
			{
				continue;
			}

			const unsigned int h = ( me->m_routeSeed ^ ( area->GetID() * 2654435761U ) );
			const float spread = 0.75f + ( (float)( h % 1000 ) * 0.0005f );	// 0.75 .. 1.25
			const float score = ( p - myPos ).Length() * spread;

			if ( score < bestPostScore )
			{
				bestPostScore = score;
				bestPost = area;
			}
		}
	}

	if ( bestPost )
	{
		*out = bestPost->GetCenter();
		return true;
	}

	if ( !haveAnchor )
		return false;

	// 2) No authored post. Hold a choke between the thing and the nearest place
	//    the enemy comes from, which is closer to how a human defends than
	//    standing on the objective itself.
	CFFNavMesh *mesh = TheFFNavMesh();
	Vector threshold;
	bool gotThreshold = false;
	if ( mesh )
	{
		float bestDistSq = FLT_MAX;
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			if ( t == myTeam )
				continue;
			CUtlVector< CFFNavArea * > thresholds;
			mesh->CollectSpawnRoomThresholdAreas( t, &thresholds );
			for ( int i = 0; i < thresholds.Count(); ++i )
			{
				const Vector p = thresholds[ i ]->GetCenter();
				const float dSq = ( p - anchor ).LengthSqr();
				if ( dSq < bestDistSq )
				{
					bestDistSq = dSq;
					threshold = p;
					gotThreshold = true;
				}
			}
		}
	}

	if ( gotThreshold )
	{
		CFFNavArea *bestChoke = NULL;
		float bestScore = FLT_MAX;
		Vector toThreshold = threshold - anchor;
		const float thresholdLen = toThreshold.Length();
		if ( thresholdLen > 1.0f )
			toThreshold *= ( 1.0f / thresholdLen );

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *cand = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			const unsigned int attrs = cand->GetAttributesFF();
			if ( !( attrs & FF_NAV_CHOKE ) )
				continue;
			if ( attrs & ( FF_NAV_SPAWN_ROOM_ANY | FF_NAV_UNDERWATER ) )
				continue;

			const Vector p = cand->GetCenter();
			Vector toCand = p - anchor;
			const float distFromAnchor = toCand.Length();
			if ( distFromAnchor > 1500.0f )
				continue;

			// Close to the thing wins, with a nudge towards the enemy side so
			// the defender holds the approach rather than the pedestal.
			const float forwardness = ( thresholdLen > 1.0f ) ? toCand.Dot( toThreshold ) : 0.0f;
			const float score = distFromAnchor - forwardness * 0.3f;
			if ( score < bestScore )
			{
				bestScore = score;
				bestChoke = cand;
			}
		}

		if ( bestChoke )
		{
			*out = bestChoke->GetCenter();
			return true;
		}

		// 3) No choke found: two thirds of the way from the thing to the enemy
		//    approach. Crude, but it puts the bot in the lane.
		*out = anchor + ( threshold - anchor ) * 0.66f;
		return true;
	}

	// 4) Last resort: the thing itself.
	*out = anchor;
	return true;
}


//-----------------------------------------------------------------------------
bool FFBotGameMode::ResolveObjective( CFFBot *me, FFBotObjective *out )
{
	if ( !me || !out )
		return false;

	out->entity = NULL;
	out->pos.Init();
	out->kind = FFOBJ_NONE;
	out->goalClass = FFGOALCLASS_UNKNOWN;
	out->why = "";

	const Vector myPos = me->GetAbsOrigin();

	// 1) Carrying something? Everything else waits. Which cap accepts it is the
	//    map's business, so take the nearest one we're allowed to use and let
	//    the gating watchdog move us on if it turns out to be the wrong one.
	CFFInfoScript *carried = NULL;
	if ( FFBotHelpers::IsBotCarryingFlag( me, &carried ) )
	{
		CBaseEntity *cap = FindBestCap( me, true );
		if ( !cap )
			cap = FindBestCap( me, false );
		if ( cap )
		{
			out->entity = cap;
			out->pos = cap->GetAbsOrigin();
			out->kind = FFOBJ_DELIVER_ITEM;
			out->goalClass = FFGOALCLASS_CAP;
			out->why = "carrying an objective item";
			return true;
		}

		// No cap entity anywhere. On a push map the capture ground may only
		// exist as authored markers.
		Vector authored;
		if ( FindAuthoredNeutralCap( myPos, &authored ) )
		{
			out->pos = authored;
			out->kind = FFOBJ_DELIVER_ITEM;
			out->why = "carrying an objective item; authored capture ground";
			return true;
		}
	}

	// 2) Defenders hold ground. Asked before the take-the-thing ladder because
	//    a defender who runs at the enemy flag is not defending.
	if ( me->m_botRole == FFROLE_DEFENSE && s_mode != FFGAMEMODE_DEATHMATCH )
	{
		Vector post;
		if ( ResolveDefendPosition( me, &post ) )
		{
			out->pos = post;
			out->kind = FFOBJ_DEFEND_POINT;
			out->why = "assigned to defense";
			return true;
		}
	}

	// 3) Take the highest-ranked carriable we're permitted to touch. Keycards
	//    before balls before flags — see TakeableRank.
	{
		int takeableClass = FFGOALCLASS_UNKNOWN;
		CBaseEntity *item = FindBestTakeable( me, &takeableClass );
		if ( item )
		{
			out->entity = item;
			out->pos = item->GetAbsOrigin();
			out->kind = FFOBJ_TAKE_ITEM;
			out->goalClass = takeableClass;
			out->why = FFBotLuaObjectives::GoalClassName( takeableClass );
			return true;
		}
	}

	// 4) Capture points. On CTF this is only reached when no flag is takeable
	//    (all carried, all removed), and going to the cap is where the fight is
	//    anyway.
	{
		CBaseEntity *cap = FindBestCap( me, true );
		if ( !cap )
			cap = FindBestCap( me, false );
		if ( cap )
		{
			out->entity = cap;
			out->pos = cap->GetAbsOrigin();
			out->kind = FFOBJ_CAPTURE_POINT;
			out->goalClass = FFGOALCLASS_CAP;
			out->why = "capture point";
			return true;
		}
	}

	// 5) Ask the map. Lua's UpdateObjectiveIcon is exactly what the HUD arrow
	//    shows a human, so when the map has an opinion and we have run out of
	//    derived ones, following it is strictly better than wandering.
	{
		CBaseEntity *scripted = FFBotLuaObjectives::GetScriptedObjective( me );
		if ( scripted && !IsObjectiveBlacklisted( me, scripted ) )
		{
			out->entity = scripted;
			out->pos = scripted->GetAbsOrigin();
			out->kind = FFOBJ_CAPTURE_POINT;
			out->why = "the map's own objective arrow";
			return true;
		}
	}

	// 6) Authored capture ground, for maps whose points are brushwork.
	{
		Vector authored;
		if ( FindAuthoredNeutralCap( myPos, &authored ) )
		{
			out->pos = authored;
			out->kind = FFOBJ_CAPTURE_POINT;
			out->why = "authored neutral capture ground";
			return true;
		}
	}

	// 7) Nothing to capture and nothing to take, but we might still have ground
	//    worth holding — a control-point map between phases, an attack/defend
	//    map whose next point hasn't opened yet.
	if ( s_mode != FFGAMEMODE_DEATHMATCH )
	{
		Vector post;
		if ( ResolveDefendPosition( me, &post ) )
		{
			out->pos = post;
			out->kind = FFOBJ_DEFEND_POINT;
			out->why = "no live objective; holding ground";
			return true;
		}
	}

	return false;
}


//=============================================================================
// Roles.
//=============================================================================

const char *FFBotGameMode::RoleName( int role )
{
	switch ( role )
	{
	case FFROLE_OFFENSE:	return "offense";
	case FFROLE_DEFENSE:	return "defense";
	case FFROLE_SUPPORT:	return "support";
	}
	return "?";
}


//-----------------------------------------------------------------------------
int FFBotGameMode::GetDesiredDefenderCount( int team, int teamSize )
{
	if ( teamSize <= 0 )
		return 0;

	int percent = ff_bot_defense_fraction.GetInt();

	if ( percent < 0 )
	{
		switch ( s_mode )
		{
		case FFGAMEMODE_DEATHMATCH:
			percent = 0;
			break;

		case FFGAMEMODE_ATTACK_DEFEND:
			// The whole point of the mode. An attacking team that leaves bots
			// at home is throwing the round; a defending team that doesn't is
			// throwing it faster.
			if ( IsTeamDefender( team ) )
				percent = 75;
			else if ( IsTeamAttacker( team ) )
				percent = 10;
			else
				percent = 40;
			break;

		case FFGAMEMODE_CONTROL_POINT:
		case FFGAMEMODE_INVADE:
			percent = 35;
			break;

		case FFGAMEMODE_HUNTED:
			// Escorts do the work; a couple of bots watch the escape route.
			percent = 25;
			break;

		case FFGAMEMODE_FORTBALL:
			percent = 30;
			break;

		case FFGAMEMODE_CTF:
		default:
			percent = 40;
			break;
		}
	}

	if ( percent <= 0 )
		return 0;

	int want = ( teamSize * percent + 50 ) / 100;

	// One defender is the floor for any mode that has something to defend, as
	// soon as there are two bots to split.
	if ( want < 1 && teamSize >= 2 && percent >= 20 )
		want = 1;

	if ( want > teamSize )
		want = teamSize;

	return want;
}


//-----------------------------------------------------------------------------
// How well a class holds ground. Not a value judgement about the class — a
// statement about whether its kit rewards standing still.
//-----------------------------------------------------------------------------
static float DefensiveAffinity( int classSlot )
{
	switch ( classSlot )
	{
	case CLASS_ENGINEER:	return 3.0f;	// buildings don't move
	case CLASS_HWGUY:		return 2.5f;	// spin-up is a commitment
	case CLASS_SNIPER:		return 2.0f;	// sightlines are positional
	case CLASS_DEMOMAN:		return 1.5f;	// traps are positional
	case CLASS_PYRO:		return 1.0f;
	case CLASS_SOLDIER:		return 0.8f;
	case CLASS_MEDIC:		return 0.3f;
	case CLASS_SCOUT:		return 0.0f;	// speed is the whole kit
	case CLASS_SPY:			return 0.0f;	// behind their lines, not ours
	case CLASS_CIVILIAN:	return -10.0f;	// the VIP defends nothing
	}
	return 0.5f;
}


//-----------------------------------------------------------------------------
void FFBotGameMode::AssignRoles( void )
{
	if ( gpGlobals->curtime < s_nextRoleTime )
		return;
	s_nextRoleTime = gpGlobals->curtime + FFBOT_ROLE_QUOTA_INTERVAL;

	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		CUtlVector< CFFBot * > bots;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex( i );
			if ( !player || player->GetTeamNumber() != team )
				continue;
			CFFBot *bot = dynamic_cast< CFFBot * >( player );
			if ( !bot )
				continue;
			bots.AddToTail( bot );
		}

		if ( bots.Count() == 0 )
			continue;

		const int wantDefenders = GetDesiredDefenderCount( team, bots.Count() );

		// Rank by how well the bot suits holding ground, with proximity to what
		// we're defending as the tie-break — the bot already standing in the
		// flag room is the one to leave there.
		Vector anchor;
		bool haveAnchor = FindDefendedThing( bots[ 0 ], &anchor );

		struct Ranked
		{
			CFFBot *bot;
			float   score;
		};
		CUtlVector< Ranked > ranked;
		for ( int i = 0; i < bots.Count(); ++i )
		{
			Ranked r;
			r.bot = bots[ i ];
			r.score = DefensiveAffinity( bots[ i ]->GetClassSlot() );
			if ( haveAnchor )
			{
				const float d = ( bots[ i ]->GetAbsOrigin() - anchor ).Length();
				r.score -= ( d / 2000.0f );	// 2000u of distance ≈ one affinity step
			}
			ranked.AddToTail( r );
		}

		// Descending by score. Insertion sort: teams are at most 32 and this
		// runs twice a second.
		for ( int i = 1; i < ranked.Count(); ++i )
		{
			const Ranked key = ranked[ i ];
			int j = i - 1;
			while ( j >= 0 && ranked[ j ].score < key.score )
			{
				ranked[ j + 1 ] = ranked[ j ];
				--j;
			}
			ranked[ j + 1 ] = key;
		}

		for ( int i = 0; i < ranked.Count(); ++i )
		{
			CFFBot *bot = ranked[ i ].bot;

			int desired;
			if ( i < wantDefenders )
			{
				desired = FFROLE_DEFENSE;
			}
			else if ( bot->GetClassSlot() == CLASS_MEDIC || bot->GetClassSlot() == CLASS_SPY )
			{
				// Both have their own top-level Action (MedicFollow,
				// SpyInfiltrate) that is neither pushing nor holding.
				desired = FFROLE_SUPPORT;
			}
			else
			{
				desired = FFROLE_OFFENSE;
			}

			if ( bot->m_botRole == desired )
				continue;

			// Hysteresis. A bot that just took a role keeps it, so a death on
			// the other side of the map doesn't reshuffle everyone.
			if ( bot->m_roleAssignTime > 0.0f &&
			     ( gpGlobals->curtime - bot->m_roleAssignTime ) < FFBOT_ROLE_MIN_HOLD )
			{
				continue;
			}

			bot->m_botRole = (unsigned char)desired;
			bot->m_roleAssignTime = gpGlobals->curtime;
		}
	}
}


//-----------------------------------------------------------------------------
static int CountClassOnTeamLocal( int team, int classSlot )
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *player = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || player->GetTeamNumber() != team )
			continue;
		if ( player->GetClassSlot() == classSlot )
			++count;
	}
	return count;
}


//-----------------------------------------------------------------------------
// Class weights per role. Used as a weighted random draw over whatever the
// team's class limits still permit, so a team gets a plausible composition
// rather than nine scouts or nine engineers.
//-----------------------------------------------------------------------------
static int ClassWeightForRole( int classSlot, int role )
{
	if ( role == FFROLE_DEFENSE )
	{
		switch ( classSlot )
		{
		case CLASS_ENGINEER:	return 30;
		case CLASS_HWGUY:		return 20;
		case CLASS_SNIPER:		return 15;
		case CLASS_DEMOMAN:		return 15;
		case CLASS_PYRO:		return 10;
		case CLASS_SOLDIER:		return 10;
		case CLASS_MEDIC:		return 4;
		case CLASS_SCOUT:		return 2;
		case CLASS_SPY:			return 0;
		case CLASS_CIVILIAN:	return 0;
		}
		return 1;
	}

	switch ( classSlot )
	{
	case CLASS_SCOUT:		return 20;
	case CLASS_SOLDIER:		return 20;
	case CLASS_MEDIC:		return 15;
	case CLASS_SPY:			return 12;
	case CLASS_DEMOMAN:		return 10;
	case CLASS_PYRO:		return 10;
	case CLASS_HWGUY:		return 5;
	case CLASS_SNIPER:		return 5;
	case CLASS_ENGINEER:	return 3;
	case CLASS_CIVILIAN:	return 0;
	}
	return 1;
}


//-----------------------------------------------------------------------------
int FFBotGameMode::PickClassForTeamNeed( int team )
{
	CFFTeam *pTeam = GetGlobalFFTeam( team );
	if ( !pTeam )
		return 0;

	// Does the team still want defenders? Count what it has, against what the
	// mode wants for a team one bot larger — because the bot asking is about to
	// become that extra member.
	int teamSize = 0;
	int currentDefenders = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player || player->GetTeamNumber() != team )
			continue;
		++teamSize;
		CFFBot *bot = dynamic_cast< CFFBot * >( player );
		if ( bot && bot->m_botRole == FFROLE_DEFENSE )
			++currentDefenders;
	}

	const int wantDefenders = GetDesiredDefenderCount( team, teamSize + 1 );
	const int role = ( currentDefenders < wantDefenders ) ? FFROLE_DEFENSE : FFROLE_OFFENSE;

	// Weighted draw over the classes this team still permits.
	int weights[ CLASS_CIVILIAN + 1 ];
	int total = 0;
	for ( int c = CLASS_SCOUT; c <= CLASS_CIVILIAN; ++c )
	{
		weights[ c ] = 0;

		const int limit = pTeam->GetClassLimit( c );
		if ( limit == -1 )
			continue;	// disabled on this team
		if ( limit > 0 && CountClassOnTeamLocal( team, c ) >= limit )
			continue;	// full

		weights[ c ] = ClassWeightForRole( c, role );
		total += weights[ c ];
	}

	if ( total <= 0 )
		return 0;	// caller falls back to its own logic

	int roll = RandomInt( 0, total - 1 );
	for ( int c = CLASS_SCOUT; c <= CLASS_CIVILIAN; ++c )
	{
		roll -= weights[ c ];
		if ( roll < 0 )
			return c;
	}

	return 0;
}


//=============================================================================
// Lifecycle.
//=============================================================================

void FFBotGameMode::OnMapLoad( void )
{
	s_mode = FFGAMEMODE_UNKNOWN;
	s_attackerMask = 0;
	s_defenderMask = 0;
	s_modeDirty = true;
	s_nextModeTime = 0.0f;
	s_nextRoleTime = 0.0f;

	for ( int i = 0; i <= MAX_PLAYERS; ++i )
	{
		for ( int s = 0; s < FFBOT_OBJECTIVE_BLACKLIST_SLOTS; ++s )
		{
			s_botState[ i ].blacklist[ s ].ent.Term();
			s_botState[ i ].blacklist[ s ].expireTime = 0.0f;
		}
		s_botState[ i ].pursuing.Term();
		s_botState[ i ].bestDistance = 0.0f;
		s_botState[ i ].bestDistanceTime = 0.0f;
	}

	DeriveMode();
	s_modeDirty = false;

	Msg( "[FFBotGameMode] %s (%d live goals: %d flag owners, %d cap owners, "
	     "%d neutral flags, %d neutral caps).\n",
		Name( s_mode ), s_lastLiveGoals, s_lastFlagOwners, s_lastCapOwners,
		s_lastNeutralFlags, s_lastNeutralCaps );

	if ( s_mode == FFGAMEMODE_ATTACK_DEFEND )
	{
		Msg( "[FFBotGameMode] attackers=0x%02x defenders=0x%02x\n",
			s_attackerMask, s_defenderMask );
	}
}


void FFBotGameMode::Tick( void )
{
	if ( s_modeDirty && gpGlobals->curtime >= s_nextModeTime )
	{
		s_modeDirty = false;
		s_nextModeTime = gpGlobals->curtime + FFBOT_MODE_REDERIVE_INTERVAL;

		const int was = s_mode;
		DeriveMode();

		if ( was != s_mode )
		{
			Msg( "[FFBotGameMode] mode is now %s.\n", Name( s_mode ) );
			// A mode change means the quota changed. Let it re-run now rather
			// than at the next throttle window.
			s_nextRoleTime = 0.0f;
		}
	}

	AssignRoles();
}


//=============================================================================
// Diagnostics.
//=============================================================================

void FFBotGameMode::PrintReport( void )
{
	Msg( "==== FF bot game mode ====\n" );
	Msg( "  mode=%s  (cvar override=%d)\n", Name( s_mode ), ff_bot_gamemode.GetInt() );
	Msg( "  derived from: %d live goals, %d flag owners, %d cap owners, "
	     "%d neutral flags, %d neutral caps\n",
		s_lastLiveGoals, s_lastFlagOwners, s_lastCapOwners,
		s_lastNeutralFlags, s_lastNeutralCaps );

	if ( s_mode == FFGAMEMODE_ATTACK_DEFEND )
		Msg( "  attackers=0x%02x  defenders=0x%02x\n", s_attackerMask, s_defenderMask );

	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		int counts[ FFROLE_COUNT ] = { 0, 0, 0 };
		int size = 0;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex( i );
			if ( !player || player->GetTeamNumber() != team )
				continue;
			++size;
			CFFBot *bot = dynamic_cast< CFFBot * >( player );
			if ( bot && bot->m_botRole < FFROLE_COUNT )
				++counts[ bot->m_botRole ];
		}
		if ( size == 0 )
			continue;

		Msg( "  team %d: %d players, want %d defenders, have %d defense / "
		     "%d offense / %d support\n",
			team, size, GetDesiredDefenderCount( team, size ),
			counts[ FFROLE_DEFENSE ], counts[ FFROLE_OFFENSE ], counts[ FFROLE_SUPPORT ] );
	}

	// Per-bot objective state. This is the view that explains a bot behaving
	// strangely: a blacklisted objective is the bot telling you it could not
	// get there.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFBot *bot = dynamic_cast< CFFBot * >( UTIL_PlayerByIndex( i ) );
		if ( !bot )
			continue;

		const BotObjectiveState *state = &s_botState[ i ];
		int blocked = 0;
		for ( int s = 0; s < FFBOT_OBJECTIVE_BLACKLIST_SLOTS; ++s )
		{
			if ( state->blacklist[ s ].ent.Get() &&
			     state->blacklist[ s ].expireTime > gpGlobals->curtime )
			{
				++blocked;
			}
		}

		Msg( "    %-20s role=%-8s blacklisted=%d\n",
			bot->GetPlayerName(), RoleName( bot->m_botRole ), blocked );
	}

	Msg( "==========================\n" );
}


CON_COMMAND_F( ff_bot_gamemode_report,
	"Show the detected game mode, the per-team defense quota, and each bot's "
	"role and blacklisted objectives.",
	FCVAR_CHEAT )
{
	FFBotGameMode::PrintReport();
}


CON_COMMAND_F( ff_bot_gamemode_rederive,
	"Re-derive the game mode from the live goal registry right now.",
	FCVAR_CHEAT )
{
	FFBotGameMode::InvalidateMode();
	s_nextModeTime = 0.0f;
	FFBotGameMode::Tick();
	FFBotGameMode::PrintReport();
}
