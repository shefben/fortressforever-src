//========= Fortress Forever Bot =============================================//
//
// FFBotHelpers — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_helpers.h"
#include "ff_bot.h"
#include "ff_player.h"
#include "ff_info_script.h"
#include "ff_bot_lua_objectives.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "engine/IEngineTrace.h"

#include "Path/NextBotPathFollow.h"
#include "NextBotLocomotionInterface.h"
#include "NextBotBodyInterface.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// FIX 1 — single movement authority.
//-----------------------------------------------------------------------------
bool FFBotHelpers::CanDrivePath( CFFBot *me, PathFollower &path )
{
	if ( !me )
		return false;

	// Publish the path goal + whether a sharp turn / discontinuity is coming
	// up. Nothing else in the engine does this for player bots: PathFollower
	// ::Update calls ILocomotion::FaceTowards, which is an empty stub unless
	// the locomotor is a NextBotGroundLocomotion (ours is a PlayerLocomotion).
	const Path::Segment *goal = path.GetCurrentGoal();
	if ( goal )
	{
		me->NotePathGoal( goal->pos );

		// Turn-ahead detection for the bunny-hop gate (FIX 8). Either the next
		// segment is a climb / gap jump, or the route bends more than ~45deg.
		bool turnAhead = ( goal->type == Path::CLIMB_UP || goal->type == Path::JUMP_OVER_GAP );
		if ( !turnAhead )
		{
			const Path::Segment *next = path.NextSegment( goal );
			if ( next )
			{
				Vector a = goal->forward;
				Vector b = next->forward;
				a.z = 0.0f;
				b.z = 0.0f;
				if ( a.NormalizeInPlace() > 0.0f && b.NormalizeInPlace() > 0.0f )
					turnAhead = ( DotProduct( a, b ) < 0.707f );	// > 45 deg
			}
		}
		me->m_pathTurnAhead = turnAhead;
	}
	else
	{
		me->m_pathTurnAhead = false;
	}

	// While the arbiter owns movement, the path follower must stay quiet.
	if ( me->IsMoveOverrideActive() )
		return false;

	// Legacy inhibit flag — kept so existing callers that set it still work.
	if ( me->m_pathInhibitTimer.HasStarted() && !me->m_pathInhibitTimer.IsElapsed() )
		return false;

	// Claim this tick's single Approach() so the arbiter stands down.
	me->m_pathDrivenTick = gpGlobals->tickcount;
	return true;
}


//-----------------------------------------------------------------------------
// FIX 7 — repath hysteresis.
//-----------------------------------------------------------------------------
bool FFBotHelpers::ShouldRecomputePath( CFFBot *me, const PathFollower &path, const Vector &goalPos )
{
	if ( !me )
		return false;

	// No usable path — always recompute.
	if ( !path.IsValid() )
		return true;

	// Goal moved (chasing a carrier, a pickup respawned elsewhere, ...).
	// 100u is under one nav-area width, so a genuinely new destination always
	// trips it while sub-area jitter does not.
	if ( ( path.GetEndPosition() - goalPos ).IsLengthGreaterThan( 100.0f ) )
		return true;

	// Not making progress — the current route is not working, so re-plan even
	// though the goal is unchanged.
	Vector vel = me->GetAbsVelocity();
	vel.z = 0.0f;
	if ( vel.LengthSqr() < ( 100.0f * 100.0f ) )
		return true;

	// Path still valid, goal unchanged, bot travelling. Leave it alone — this
	// is the case that used to flip lanes once a second.
	return false;
}


//-----------------------------------------------------------------------------
// FIX 6 — doors.
//-----------------------------------------------------------------------------
bool FFBotHelpers::IsOpenableBlocker( CBaseEntity *ent )
{
	if ( !ent || ent->IsWorld() )
		return false;

	// Never treat a player / NPC as a door.
	if ( ent->IsPlayer() || ent->MyCombatCharacterPointer() )
		return false;

	static const char * const kOpenable[] = {
		"func_door",
		"func_door_rotating",
		"prop_door_rotating",
		"func_movelinear",
		"func_areaportalwindow",	// FF respawn "doors" are sometimes these
		"func_brush",				// lua-toggled respawn gates
		"func_wall_toggle",
	};

	for ( int i = 0; i < ARRAYSIZE( kOpenable ); ++i )
	{
		if ( FClassnameIs( ent, kOpenable[ i ] ) )
			return true;
	}
	return false;
}


CBaseEntity *FFBotHelpers::FindBlockingDoor( CFFBot *me, const Vector &towards )
{
	if ( !me )
		return NULL;

	ILocomotion *loco = me->GetLocomotionInterface();
	IBody *body = me->GetBodyInterface();
	if ( !loco || !body )
		return NULL;

	Vector dir = towards - me->GetAbsOrigin();
	dir.z = 0.0f;
	if ( dir.NormalizeInPlace() < 0.5f )
		return NULL;

	// Probe only as far as we could walk in a fraction of a second — we want
	// "the thing I am pressed against", not "the next door down the corridor".
	const float kProbeRange = 48.0f;
	const Vector start = me->GetAbsOrigin() + Vector( 0, 0, loco->GetStepHeight() + 1.0f );

	trace_t tr;
	const float halfWidth = 0.5f * body->GetHullWidth();
	UTIL_TraceHull( start, start + dir * kProbeRange,
		Vector( -halfWidth, -halfWidth, 0.0f ),
		Vector( halfWidth, halfWidth, body->GetCrouchHullHeight() ),
		MASK_PLAYERSOLID, me, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );

	if ( tr.fraction >= 1.0f && !tr.startsolid )
		return NULL;

	if ( !tr.DidHitNonWorldEntity() || tr.m_pEnt == NULL )
		return NULL;

	return IsOpenableBlocker( tr.m_pEnt ) ? tr.m_pEnt : NULL;
}


bool FFBotHelpers::IsBotCarryingFlag( CFFBot *me, CFFInfoScript **outFlag )
{
	if ( !me )
		return false;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( s->IsCarried() && s->GetCarrier() == me )
		{
			if ( outFlag )
				*outFlag = s;
			return true;
		}
	}
	return false;
}


// Map FF team (TEAM_BLUE..TEAM_GREEN = 2..5) to the bit position used by
// CFFInfoScript::m_BotTeamFlags (Omnibot's TF_TEAM_BLUE..TF_TEAM_GREEN = 1..4).
static int BotTeamFlagBitForFFTeam( int ffTeam )
{
	if ( ffTeam < TEAM_BLUE || ffTeam > TEAM_GREEN )
		return 0;
	return 1 << ( ffTeam - TEAM_BLUE + Omnibot::TF_TEAM_BLUE );
}


bool FFBotHelpers::IsBotsOwnFlag( int myTeam, CFFInfoScript *flag )
{
	if ( !flag )
		return false;
	const int myBit = BotTeamFlagBitForFFTeam( myTeam );
	if ( myBit == 0 )
		return false;
	const int teamFlags = flag->GetBotTeamFlags();
	if ( teamFlags == 0 )
	{
		// Flag has no team-flag bits set at all (rare, e.g. unconfigured
		// neutral). Treat as not-mine — bots will go grab it as offense.
		return false;
	}
	// Own flag = touchflags do NOT include our team (we can't grab/touch it
	// for pickup, only enemies can).
	return ( teamFlags & myBit ) == 0;
}


bool FFBotHelpers::CanBotGrabFlag( int myTeam, CFFInfoScript *flag )
{
	if ( !flag )
		return false;
	const int myBit = BotTeamFlagBitForFFTeam( myTeam );
	if ( myBit == 0 )
		return false;
	const int teamFlags = flag->GetBotTeamFlags();
	if ( teamFlags == 0 )
	{
		// Neutral / unconfigured — anyone can grab.
		return true;
	}
	return ( teamFlags & myBit ) != 0;
}


CFFInfoScript *FFBotHelpers::FindOwnFlag( int myTeam )
{
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( s->IsRemoved() )
			continue;
		if ( IsBotsOwnFlag( myTeam, s ) )
			return s;
	}
	return NULL;
}


bool FFBotHelpers::IsOwnFlagThreatened( int myTeam, float radius )
{
	CFFInfoScript *ownFlag = FindOwnFlag( myTeam );
	if ( !ownFlag )
		return false;

	const Vector flagPos = ownFlag->GetAbsOrigin();
	const float radiusSq = radius * radius;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() == myTeam )
			continue;
		// Spy disguised as our team — assume hostile (real spies are still
		// a threat near our flag).
		const float distSq = ( pp->GetAbsOrigin() - flagPos ).LengthSqr();
		if ( distSq < radiusSq )
			return true;
	}
	return false;
}


bool FFBotHelpers::IsEnemyApproachingOwnFlag( int myTeam, float radius,
                                              CFFPlayer **out )
{
	CFFInfoScript *ownFlag = FindOwnFlag( myTeam );
	if ( !ownFlag )
	{
		if ( out ) *out = NULL;
		return false;
	}

	const Vector flagPos = ownFlag->GetAbsOrigin();
	const float radiusSq = radius * radius;

	CFFPlayer *closest = NULL;
	float bestDistSq = radiusSq;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() == myTeam )
			continue;

		const Vector enemyPos = pp->GetAbsOrigin();
		const float distSq = ( enemyPos - flagPos ).LengthSqr();
		if ( distSq >= radiusSq )
			continue;

		// Direction filter: either the enemy has LOS to the flag (eyes on
		// it, even if not actively moving), OR their velocity points
		// roughly at the flag. Velocity check uses a >0 dot — even
		// stationary enemies with LOS qualify since they're sitting in
		// the flag room.
		bool qualifies = false;

		// Velocity-toward check.
		Vector vel = pp->GetAbsVelocity();
		vel.z = 0.0f;
		if ( vel.LengthSqr() > ( 50.0f * 50.0f ) )
		{
			Vector toFlag = flagPos - enemyPos;
			toFlag.z = 0.0f;
			if ( toFlag.NormalizeInPlace() > 0.1f )
			{
				vel.NormalizeInPlace();
				if ( vel.Dot( toFlag ) > 0.5f )		// pointing at flag, not crossing
					qualifies = true;
			}
		}

		// LOS check. Skip if velocity already qualifies (we'd already
		// know it's a threat).
		if ( !qualifies )
		{
			trace_t tr;
			UTIL_TraceLine( pp->EyePosition(), flagPos, MASK_VISIBLE_AND_NPCS,
				NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 0.99f )
				qualifies = true;
		}

		if ( qualifies && distSq < bestDistSq )
		{
			bestDistSq = distSq;
			closest = pp;
		}
	}

	if ( out ) *out = closest;
	return closest != NULL;
}


CFFPlayer *FFBotHelpers::FindEnemyCarryingOurFlag( int myTeam )
{
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( !s->IsCarried() )
			continue;
		if ( !IsBotsOwnFlag( myTeam, s ) )
			continue;	// not our flag
		CBaseEntity *carrier = s->GetCarrier();
		if ( !carrier )
			continue;
		CFFPlayer *cp = ToFFPlayer( carrier );
		if ( !cp || !cp->IsAlive() )
			continue;
		if ( cp->GetTeamNumber() == myTeam )
			continue;	// teammate carrying our flag — Lua oddity, skip
		return cp;
	}
	return NULL;
}


int FFBotHelpers::CountAliveOnTeam( int myTeam, int classSlot )
{
	int n = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		if ( pp->GetClassSlot() != classSlot )
			continue;
		++n;
	}
	return n;
}


CFFPlayer *FFBotHelpers::FindClosestAliveEngineer( int myTeam, const Vector &pos )
{
	CFFPlayer *best = NULL;
	float bestDistSq = FLT_MAX;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		if ( pp->GetClassSlot() != CLASS_ENGINEER )
			continue;
		const float dSq = ( pp->GetAbsOrigin() - pos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = pp;
		}
	}
	return best;
}


bool FFBotHelpers::IsFriendlySentryNear( int myTeam, const Vector &pos, float radius )
{
	const float radiusSq = radius * radius;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "FF_SentryGun" ) ) != NULL )
	{
		if ( e->GetTeamNumber() != myTeam )
			continue;
		if ( ( e->GetAbsOrigin() - pos ).LengthSqr() < radiusSq )
			return true;
	}
	return false;
}


CBaseEntity *FFBotHelpers::FindOwnCapPoint( int myTeam, const Vector &myPos )
{
	// Caps inherit FF's "Lua-table-team-but-not-C++-team" issue, so
	// GetTeamNumber() on a cap is unreliable. In CTF, own cap is always
	// paired with own flag at the same base — use proximity to own flag as
	// the anchor. If no own flag (modes without flags), fall back to
	// closest cap to ourself.
	CFFInfoScript *ownFlag = FindOwnFlag( myTeam );
	const Vector anchor = ownFlag ? ownFlag->GetAbsOrigin() : myPos;

	// team -1: cap ownership is not reliably expressed in the goal's team
	// flags, which is exactly why this function anchors on proximity to our
	// own flag instead. Take every live cap and pick the nearest to home.
	return FFBotLuaObjectives::FindNearestGoal( Omnibot::kFlagCap, -1, anchor, NULL );
}


CBaseEntity *FFBotHelpers::FindAnyCapPoint( const Vector &myPos )
{
	return FFBotLuaObjectives::FindNearestGoal( Omnibot::kFlagCap, -1, myPos, NULL );
}


CFFPlayer *FFBotHelpers::FindWoundedTeammate( CFFPlayer *me, float searchRange )
{
	if ( !me )
		return NULL;

	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();
	const float searchSq = searchRange * searchRange;

	CFFPlayer *best = NULL;
	int bestDeficit = 10;	// must be at least 10hp short of max to qualify

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || pp == me )
			continue;
		if ( !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		const float distSq = ( pp->GetAbsOrigin() - myPos ).LengthSqr();
		if ( distSq > searchSq )
			continue;

		const int deficit = pp->GetMaxHealth() - pp->GetHealth();
		if ( deficit > bestDeficit )
		{
			bestDeficit = deficit;
			best = pp;
		}
	}
	return best;
}


CFFPlayer *FFBotHelpers::FindFriendlyCivilian( int myTeam )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp )
			continue;
		if ( !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		if ( pp->GetClassSlot() == CLASS_CIVILIAN )
			return pp;
	}
	return NULL;
}


CFFPlayer *FFBotHelpers::FindNearestFriendlyMedic( int myTeam, const Vector &fromPos, float maxRange )
{
	CFFPlayer *best = NULL;
	float bestDistSq = maxRange * maxRange;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		if ( pp->GetClassSlot() != CLASS_MEDIC )
			continue;
		const float dSq = ( pp->GetAbsOrigin() - fromPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = pp;
		}
	}
	return best;
}


CFFPlayer *FFBotHelpers::FindTeammateForFlagToss( CFFPlayer *carrier, float radius )
{
	if ( !carrier )
		return NULL;
	const int myTeam = carrier->GetTeamNumber();
	const Vector myPos = carrier->GetAbsOrigin();
	const float radiusSq = radius * radius;

	CFFPlayer *best = NULL;
	float bestDistSq = radiusSq;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || pp == carrier || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		// Don't toss to a teammate who can't really run with it (low HP).
		if ( pp->GetMaxHealth() > 0 && pp->GetHealth() < pp->GetMaxHealth() / 3 )
			continue;
		// Don't toss to civilian (too slow / fragile in Hunted-style maps).
		if ( pp->GetClassSlot() == CLASS_CIVILIAN )
			continue;
		const float dSq = ( pp->GetAbsOrigin() - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = pp;
		}
	}
	return best;
}


bool FFBotHelpers::IsAnyEnemyNear( int myTeam, const Vector &pos, float radius )
{
	const float radiusSq = radius * radius;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() == myTeam )
			continue;
		const float dSq = ( pp->GetAbsOrigin() - pos ).LengthSqr();
		if ( dSq < radiusSq )
			return true;
	}
	return false;
}


CFFPlayer *FFBotHelpers::FindTeammateCarryingEnemyFlag( CFFPlayer *me )
{
	if ( !me )
		return NULL;
	const int myTeam = me->GetTeamNumber();

	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( !s->IsCarried() )
			continue;
		CBaseEntity *carrier = s->GetCarrier();
		CFFPlayer *cp = carrier ? ToFFPlayer( carrier ) : NULL;
		if ( !cp || cp == me )
			continue;
		if ( cp->GetTeamNumber() != myTeam )
			continue;
		// This carrier is on our team — make sure the flag they hold is an
		// enemy flag (i.e. one our team can grab — touchflags include us).
		if ( !CanBotGrabFlag( myTeam, s ) )
			continue;
		return cp;
	}
	return NULL;
}
