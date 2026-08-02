//========= Fortress Forever Bot =============================================//
//
// FFBotHazard / CFFBotEscapeHazard — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_hazard.h"
#include "ff_bot_helpers.h"
#include "ff_bot_lua_objectives.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_buildableobject.h"
#include "ff_player.h"
#include "entitylist.h"
#include "nav_mesh.h"
#include "takedamageinfo.h"
#include "NextBotLocomotionInterface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_hazard_response( "ff_bot_hazard_response", "1", FCVAR_NONE,
	"React to environmental damage by fetching protective equipment or leaving "
	"the volume. 0 = off (bots stand in the gas, the FoxBot behaviour), "
	"1 = on, 2 = on and log every classification." );


//-----------------------------------------------------------------------------
// Damage-type bits that can only come from the environment.
//
// DMG_DROWN is deliberately absent: drowning already has an owner in
// CFFBotMainAction::Update, which surfaces the bot, and routing a drowning bot
// to a gas suit would be actively worse than what it does now.
//-----------------------------------------------------------------------------
#define FFBOT_ENV_DAMAGE_BITS	( DMG_NERVEGAS | DMG_RADIATION | DMG_POISON |   \
                                  DMG_ACID | DMG_SLOWBURN | DMG_SHOCK |         \
                                  DMG_ENERGYBEAM | DMG_PARALYZE )

// How long after the last environmental hit the bot still counts as suffering.
// trigger_hurt ticks at 0.5s by default, so this has to comfortably span two
// ticks or the state flickers.
#define FFBOT_HAZARD_MEMORY			2.5f

// Below this much continuous exposure we assume the bot clipped the corner of
// something and will walk out of it on its own. Above it, the room is the
// problem.
#define FFBOT_HAZARD_COMMIT_TIME	1.0f

// Give up on the whole business after this long. A bot that cannot reach the
// suit is better off back on the objective than looping forever.
#define FFBOT_HAZARD_TIMEOUT		25.0f

// How far we'll travel for protective equipment.
#define FFBOT_HAZARD_GEAR_RANGE		3000.0f


//-----------------------------------------------------------------------------
// Per-bot exposure clock, indexed by entindex.
//-----------------------------------------------------------------------------
struct HazardState
{
	float lastHitTime;		// last environmental hit
	float exposureStart;	// when the current continuous exposure began
};

static HazardState s_hazard[ MAX_PLAYERS + 1 ];


static HazardState *StateFor( CFFBot *me )
{
	if ( !me )
		return NULL;
	const int idx = me->entindex();
	if ( idx < 0 || idx > MAX_PLAYERS )
		return NULL;
	return &s_hazard[ idx ];
}


//-----------------------------------------------------------------------------
static CFFNavArea *AreaUnder( CFFBot *me )
{
	CNavArea *here = me->GetLastKnownArea();
	if ( !here && TheNavMesh )
		here = TheNavMesh->GetNearestNavArea( me->GetAbsOrigin() );
	return static_cast< CFFNavArea * >( here );
}


//-----------------------------------------------------------------------------
// Is this hit the world rather than a person?
//
// The damage-type bits answer it outright when the map bothers to set them.
// When they don't — and plenty of Lua-driven trigger_hurts don't — the fallback
// is "nobody is credited with this and we are standing in a volume the
// auto-tagger already marked as dangerous", which is about as close to certain
// as this gets without reading the map's script.
//-----------------------------------------------------------------------------
static bool IsEnvironmentalDamage( CFFBot *me, const CTakeDamageInfo &info )
{
	const int bits = info.GetDamageType();

	if ( bits & FFBOT_ENV_DAMAGE_BITS )
		return true;

	// Owned by the swim code.
	if ( bits & DMG_DROWN )
		return false;

	CBaseEntity *attacker = info.GetAttacker();

	// Our own rockets, our own detpack, fall damage we caused. Not a hazard.
	if ( attacker == me )
		return false;

	if ( attacker && attacker->IsPlayer() )
		return false;

	// A sentry gun credits its owner, but be defensive about it.
	if ( dynamic_cast< CFFBuildableObject * >( attacker ) != NULL )
		return false;

	// Nobody is behind this. If we're standing in a volume the auto-tagger
	// already flagged, that is the volume doing it.
	CFFNavArea *here = AreaUnder( me );
	return ( here != NULL && here->HasAttributeFF2( FF_NAV2_HAZARD_ZONE ) );
}


//-----------------------------------------------------------------------------
void FFBotHazard::OnInjured( CFFBot *me, const CTakeDamageInfo &info )
{
	if ( ff_bot_hazard_response.GetInt() <= 0 )
		return;

	HazardState *state = StateFor( me );
	if ( !state )
		return;

	if ( !IsEnvironmentalDamage( me, info ) )
		return;

	// A gap longer than our memory window means this is a fresh exposure, not a
	// continuation of the last one.
	if ( ( gpGlobals->curtime - state->lastHitTime ) > FFBOT_HAZARD_MEMORY )
		state->exposureStart = gpGlobals->curtime;

	state->lastHitTime = gpGlobals->curtime;

	if ( ff_bot_hazard_response.GetInt() >= 2 )
	{
		CBaseEntity *attacker = info.GetAttacker();
		Msg( "[FFBotHazard] %s took %.0f environmental damage (bits 0x%x) from '%s'; "
		     "exposed for %.1fs\n",
			me->GetPlayerName(), info.GetDamage(), info.GetDamageType(),
			attacker ? attacker->GetClassname() : "(nobody)",
			gpGlobals->curtime - state->exposureStart );
	}
}


bool FFBotHazard::IsSuffering( CFFBot *me )
{
	if ( ff_bot_hazard_response.GetInt() <= 0 )
		return false;

	const HazardState *state = StateFor( me );
	if ( !state || state->lastHitTime <= 0.0f )
		return false;

	return ( gpGlobals->curtime - state->lastHitTime ) < FFBOT_HAZARD_MEMORY;
}


float FFBotHazard::GetSufferingDuration( CFFBot *me )
{
	const HazardState *state = StateFor( me );
	if ( !state || !IsSuffering( me ) )
		return 0.0f;
	return gpGlobals->curtime - state->exposureStart;
}


void FFBotHazard::Reset( CFFBot *me )
{
	HazardState *state = StateFor( me );
	if ( !state )
		return;
	state->lastHitTime = 0.0f;
	state->exposureStart = 0.0f;
}


//-----------------------------------------------------------------------------
bool FFBotHazard::FindHazardGear( CFFBot *me, Vector *outPos, CBaseEntity **outEnt )
{
	if ( outEnt )
		*outEnt = NULL;

	if ( !me || !outPos )
		return false;

	const Vector myPos = me->GetAbsOrigin();

	// The real thing: a live, touchable pickup the classifier identified.
	CBaseEntity *gear = FFBotLuaObjectives::FindNearestOfClass(
		FFGOALCLASS_HAZARD_GEAR, -1, myPos, me );

	if ( gear )
	{
		const float distSq = ( gear->GetAbsOrigin() - myPos ).LengthSqr();
		if ( distSq <= ( FFBOT_HAZARD_GEAR_RANGE * FFBOT_HAZARD_GEAR_RANGE ) )
		{
			*outPos = gear->GetAbsOrigin();
			if ( outEnt )
				*outEnt = gear;
			return true;
		}
	}

	// No entity we recognise. FF_NAV2_HAZARD_GEAR is stamped both by the
	// auto-tagger (from the classified entity) and by hand from
	// ff_nav_place gassuit, so on a map where the gear is a brush, a func_button
	// or something else we cannot classify, the marker is the only record that
	// it exists at all.
	CFFNavArea *best = NULL;
	float bestDistSq = FFBOT_HAZARD_GEAR_RANGE * FFBOT_HAZARD_GEAR_RANGE;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area->HasAttributeFF2( FF_NAV2_HAZARD_GEAR ) )
			continue;
		const float dSq = ( area->GetCenter() - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}

	if ( !best )
		return false;

	*outPos = best->GetCenter();
	return true;	// position only — there is no entity to hold a handle on
}


//-----------------------------------------------------------------------------
bool FFBotHazard::FindWayOut( CFFBot *me, Vector *outPos )
{
	if ( !me || !outPos || !TheNavMesh || !TheNavMesh->IsLoaded() )
		return false;

	const Vector myPos = me->GetAbsOrigin();

	CFFNavArea *best = NULL;
	float bestDistSq = FLT_MAX;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );

		if ( area->HasAttributeFF2( FF_NAV2_HAZARD_ZONE | FF_NAV2_DANGER ) )
			continue;
		if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
			continue;

		// Don't flee into an enemy spawn room; we'd be turned round at the door
		// and the path cost model refuses it anyway.
		if ( area->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY ) )
		{
			const int myOwnSpawn = CFFNavArea::SpawnRoomAttributeForTeam( me->GetTeamNumber() );
			if ( ( area->GetAttributesFF() & FF_NAV_SPAWN_ROOM_ANY & ~myOwnSpawn ) != 0 )
				continue;
		}

		const float dSq = ( area->GetCenter() - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}

	if ( !best )
		return false;

	*outPos = best->GetCenter();
	return true;
}


//=============================================================================
// CFFBotEscapeHazard.
//=============================================================================

CFFBotEscapeHazard::CFFBotEscapeHazard( void )
{
	m_goalPos.Init();
	m_isFetchingGear = false;
}


//-----------------------------------------------------------------------------
bool CFFBotEscapeHazard::IsPossible( CFFBot *me )
{
	if ( !me || !me->IsAlive() )
		return false;

	if ( !FFBotHazard::IsSuffering( me ) )
		return false;

	// Brief contact with the edge of something is not worth abandoning the
	// objective for; the bot is already walking out of it.
	if ( FFBotHazard::GetSufferingDuration( me ) < FFBOT_HAZARD_COMMIT_TIME )
		return false;

	Vector pos;
	pos.Init();
	if ( FFBotHazard::FindHazardGear( me, &pos, NULL ) )
		return true;

	return FFBotHazard::FindWayOut( me, &pos );
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEscapeHazard::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_repathTimer.Invalidate();
	m_giveUpTimer.Start( FFBOT_HAZARD_TIMEOUT );

	Vector gearPos;
	gearPos.Init();
	CBaseEntity *gear = NULL;

	if ( FFBotHazard::FindHazardGear( me, &gearPos, &gear ) )
	{
		m_gear = gear;
		m_goalPos = gearPos;
		m_isFetchingGear = true;
	}
	else if ( FFBotHazard::FindWayOut( me, &m_goalPos ) )
	{
		m_gear = NULL;
		m_isFetchingGear = false;
	}
	else
	{
		return Done( "Nowhere to go about it" );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEscapeHazard::Update( CFFBot *me, float interval )
{
	if ( !me->IsAlive() )
		return Done( "Dead" );

	if ( m_giveUpTimer.IsElapsed() )
		return Done( "Gave up on the hazard" );

	// Out of it. Note that we deliberately do NOT require the gear to have been
	// picked up: if the damage stopped, whatever we did about it worked, and if
	// it starts again IsPossible will bring us straight back.
	if ( !FFBotHazard::IsSuffering( me ) )
		return Done( "No longer taking environmental damage" );

	// The gear we were going for was taken by somebody else, or the phase that
	// spawned it ended. Re-decide rather than walking to an empty pedestal.
	if ( m_isFetchingGear && m_gear.Get() != NULL &&
	     !FFBotLuaObjectives::IsGoalLive( m_gear.Get() ) )
	{
		Vector gearPos;
		gearPos.Init();
		CBaseEntity *gear = NULL;
		if ( FFBotHazard::FindHazardGear( me, &gearPos, &gear ) )
		{
			m_gear = gear;
			m_goalPos = gearPos;
		}
		else if ( FFBotHazard::FindWayOut( me, &m_goalPos ) )
		{
			m_gear = NULL;
			m_isFetchingGear = false;
		}
		else
		{
			return Done( "The gear is gone and there is nowhere to run" );
		}
		m_repathTimer.Invalidate();
	}

	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( 1.0f );

		CBaseEntity *gear = m_gear.Get();
		if ( gear )
			m_goalPos = gear->GetAbsOrigin();

		if ( FFBotHelpers::ShouldRecomputePath( me, m_path, m_goalPos ) )
		{
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			if ( !m_path.Compute( me, m_goalPos, cost ) )
			{
				// Can't reach it. If we were going for gear, try simply leaving
				// instead — that only needs a neighbouring area.
				if ( m_isFetchingGear )
				{
					m_isFetchingGear = false;
					m_gear = NULL;
					if ( !FFBotHazard::FindWayOut( me, &m_goalPos ) )
						return Done( "No path to the gear and no way out" );
					m_repathTimer.Invalidate();
				}
				else
				{
					return Done( "No path out of the hazard" );
				}
			}
		}
	}

	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me );

	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEscapeHazard::OnStuck( CFFBot *me )
{
	m_path.Invalidate();
	m_repathTimer.Invalidate();
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEscapeHazard::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	m_path.Invalidate();
	m_repathTimer.Invalidate();
	return TryContinue();
}
