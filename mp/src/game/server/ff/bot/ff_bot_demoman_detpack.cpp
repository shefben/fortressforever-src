//========= Fortress Forever Bot =============================================//
//
// CFFBotDemomanDetpack — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_demoman_detpack.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_bot_gamemode.h"	// FFROLE_DEFENSE — breach vs seal is a role question
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_player.h"
#include "ff_buildableobject.h"
#include "ff_buildable_detpack.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"
#include "NextBotKnownEntity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Distance the bot retreats before letting the detpack cook. Detpack blast
// radius is ~625u; we err well outside it.
#define FFBOT_DETPACK_SAFE_DIST		700.0f

// How close to the picked target we need to be before we consider ourselves
// "in position" and call Command_BuildDetpack.
#define FFBOT_DETPACK_PLACE_TOLERANCE	80.0f

// Whole-action timeout — if we can't get this done in 30s, abort and let
// other behaviors take over. Common cause: target became unreachable mid-run.
#define FFBOT_DETPACK_TIMEOUT			30.0f


//-----------------------------------------------------------------------------
bool CFFBotDemomanDetpack::IsPossible( CFFBot *me )
{
	if ( !me || me->GetClassSlot() != CLASS_DEMOMAN )
		return false;

	// Already have a detpack out? Don't double-place.
	if ( me->GetDetpack() )
		return false;

	// Active combat — placing now means we eat our own fuse. Combat first,
	// detpack later when the air clears.
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	if ( threat && threat->GetEntity() && threat->IsVisibleRecently() )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
// Pick a chokepoint in enemy territory. Strategy:
//   1. FF_NAV_SENTRY_SPOT areas not in our spawn (mapper-tagged chokes).
//   2. Highest-enemy-incursion area within ~2500u (deep in their half).
// Score is "deeper into enemy territory" so a forward push is rewarded.
//-----------------------------------------------------------------------------
Vector CFFBotDemomanDetpack::PickTargetChoke( CFFBot *me )
{
	if ( !me )
		return vec3_origin;

	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();
	const float searchSq = 2500.0f * 2500.0f;

	// Sentry spots in non-friendly territory get the strongest preference.
	CFFNavArea *bestSpot = NULL;
	float bestScore = -FLT_MAX;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
			continue;
		// Skip our own spawn-room (we'd just blow ourselves up).
		const unsigned int attrs = area->GetAttributesFF();
		const unsigned int ownSpawn = CFFNavArea::SpawnRoomAttributeForTeam( myTeam );
		if ( attrs & ownSpawn )
			continue;
		// Skip enemy spawn rooms — pathing there is denied anyway, and the
		// detpack would be wasted on a respawn-immune zone.
		if ( ( attrs & FF_NAV_SPAWN_ROOM_ANY ) && !( attrs & ownSpawn ) )
			continue;

		const Vector spotPos = area->GetCenter();
		const float distSq = ( spotPos - myPos ).LengthSqr();
		if ( distSq > searchSq )
			continue;

		// Score: prefer high enemy-team incursion (deeper into their half).
		// Negative incursion = unreachable for that team; skip.
		float score = 0.0f;
		int incursionCount = 0;
		float incursionSum = 0.0f;
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			if ( t == myTeam )
				continue;
			const float incur = area->GetIncursionDistance( t );
			if ( incur >= 0.0f )
			{
				incursionSum += incur;
				++incursionCount;
			}
		}
		if ( incursionCount == 0 )
			continue;	// not reachable from any enemy spawn — useless choke
		score += ( incursionSum / (float)incursionCount );	// avg enemy incursion

		// FF_NAV_SENTRY_SPOT (mapper-tagged) or FF_NAV_AUTO_SENTRY_SPOT
		// (auto-detected) in enemy territory is a known choke; big bonus.
		// FF_NAV_CHOKE alone (any narrow corridor) gets a smaller bonus.
		if ( attrs & ( FF_NAV_SENTRY_SPOT | FF_NAV_AUTO_SENTRY_SPOT ) )
			score += 4000.0f;
		else if ( attrs & FF_NAV_CHOKE )
			score += 1500.0f;

		// FF_NAV2_DETPACK_SPOT — hand-authored with ff_nav_place detpack.
		// "There is a breakable wall here" is not derivable from the nav mesh
		// at all: the shortcut it opens doesn't exist in the geometry until
		// after the charge goes off. Weighted to dominate the heuristics
		// rather than merely compete with them, since a human placing this
		// marker is asserting something the bot has no other way to know.
		if ( area->HasAttributeFF2( FF_NAV2_DETPACK_SPOT ) )
			score += 20000.0f;

		// FF_NAV2_DETPACK_SEAL is the opposite instruction: blow this SHUT to
		// deny a route, rather than open to make one. FoxBot has carried the
		// distinction since TFC (W_FL_TFC_DETPACK_CLEAR vs _SEAL) and it is not
		// cosmetic — the two produce opposite outcomes from the same position,
		// and conflating them means a demoman opening the route his own defence
		// just paid to close.
		//
		// Which one applies is not a property of the marker, it is a property
		// of what this bot has been told to do. An attacker wants routes open;
		// a defender wants them shut. So a seal spot is worth the same large
		// bonus as a breach spot to a defender, and is actively rejected for an
		// attacker rather than merely scoring low.
		if ( area->HasAttributeFF2( FF_NAV2_DETPACK_SEAL ) )
		{
			if ( me->m_botRole != FFROLE_DEFENSE )
				continue;
			score += 20000.0f;
		}

		// Per-bot deterministic noise so different demos pick different spots.
		score += me->TransientlyConsistentRandomValue( 30.0f, 11 ) * 500.0f;

		if ( score > bestScore )
		{
			bestScore = score;
			bestSpot = area;
		}
	}

	return bestSpot ? bestSpot->GetCenter() : vec3_origin;
}


//-----------------------------------------------------------------------------
CFFBotDemomanDetpack::CFFBotDemomanDetpack( const Vector &targetPos )
{
	m_state = STATE_APPROACH;
	m_targetPos = targetPos;
	m_detpackPos.Init();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDemomanDetpack::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_giveupTimer.Start( FFBOT_DETPACK_TIMEOUT );
	m_placeAttemptTimer.Invalidate();

	if ( m_targetPos == vec3_origin )
		return Done( "No target choke" );

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDemomanDetpack::Update( CFFBot *me, float interval )
{
	if ( m_giveupTimer.IsElapsed() )
		return Done( "Detpack action timed out" );

	// Bail if combat erupts mid-run; let the threat-suspend chain handle it
	// rather than place during a firefight.
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat() : NULL;
	if ( threat && threat->GetEntity() && threat->IsVisibleRecently() &&
		 me->IsRangeLessThan( threat->GetLastKnownPosition(), 800.0f ) )
	{
		// Don't blow ourselves up while engaging — abort and let MainAction
		// suspend to attack/retreat as appropriate.
		return Done( "Threat appeared mid-detpack — abort" );
	}

	switch ( m_state )
	{
	case STATE_APPROACH:
	{
		const float distSqToTarget = ( me->GetAbsOrigin() - m_targetPos ).LengthSqr();
		if ( distSqToTarget < ( FFBOT_DETPACK_PLACE_TOLERANCE * FFBOT_DETPACK_PLACE_TOLERANCE ) )
		{
			m_state = STATE_PLACE;
			m_path.Invalidate();
			break;
		}

		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 0.75f, 1.25f ) );
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			if ( !m_path.Compute( me, m_targetPos, cost ) )
				return Done( "Path to choke failed" );
		}
		// FIX 1 — single movement authority. CanDrivePath publishes the
		// path goal for the aim driver and refuses while the movement
		// arbiter owns locomotion, so this can never issue a second,
		// contradictory Approach() in the same tick.
		if ( FFBotHelpers::CanDrivePath( me, m_path ) )
			m_path.Update( me );
		break;
	}

	case STATE_PLACE:
	{
		// Issue the build command. The player's PreBuildGenericThink handles
		// switching to ff_weapon_deploydetpack and the build animation
		// internally — we just need to stand still and wait for the detpack
		// entity to spawn.
		if ( !m_placeAttemptTimer.HasStarted() )
		{
			me->Command_BuildDetpack();
			m_placeAttemptTimer.Start( 4.0f );	// build animation window
		}

		CFFDetpack *dp = me->GetDetpack();
		if ( dp )
		{
			m_detpackPos = dp->GetAbsOrigin();
			m_state = STATE_RETREAT;
			m_path.Invalidate();
			break;
		}

		if ( m_placeAttemptTimer.IsElapsed() )
		{
			// Build never finished — out of cells, or interrupted by movement.
			// Bail out cleanly.
			return Done( "Detpack build did not complete" );
		}
		break;
	}

	case STATE_RETREAT:
	{
		CFFDetpack *dp = me->GetDetpack();
		if ( !dp )
		{
			// Already detonated (player camped too long?) or got destroyed.
			return Done( "Detpack already gone" );
		}
		const float distSq = ( me->GetAbsOrigin() - m_detpackPos ).LengthSqr();
		if ( distSq >= ( FFBOT_DETPACK_SAFE_DIST * FFBOT_DETPACK_SAFE_DIST ) )
		{
			m_state = STATE_COOK;
			m_path.Invalidate();
			break;
		}

		// Retreat to a safe spot — pick a nav area we already came from.
		// Quick heuristic: move toward our team's spawn, which is by
		// construction "back" from the choke we picked. Filter to a
		// non-water exit area; bots stuck in water during the cook
		// timer would just oscillate while taking detpack damage on
		// detonation.
		CFFNavMesh *mesh = TheFFNavMesh();
		Vector awayDir = me->GetAbsOrigin() - m_detpackPos;
		awayDir.NormalizeInPlace();
		Vector retreatTo = m_detpackPos + awayDir * ( FFBOT_DETPACK_SAFE_DIST * 1.2f );
		if ( mesh )
		{
			const CUtlVector< CFFNavArea * > *exits =
				mesh->GetSpawnRoomExitAreas( me->GetTeamNumber() );
			if ( exits && exits->Count() > 0 )
			{
				// Prefer the first dry exit; only fall back to a wet
				// one if every exit is wet (rare).
				CFFNavArea *dryExit = NULL;
				for ( int i = 0; i < exits->Count(); ++i )
				{
					if ( !( *exits )[ i ]->HasAttributeFF( FF_NAV_WATER | FF_NAV_UNDERWATER ) )
					{
						dryExit = ( *exits )[ i ];
						break;
					}
				}
				retreatTo = dryExit ? dryExit->GetCenter() : ( *exits )[ 0 ]->GetCenter();
			}
		}

		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			m_path.Compute( me, retreatTo, cost );
		}
		// FIX 1 — single movement authority. CanDrivePath publishes the
		// path goal for the aim driver and refuses while the movement
		// arbiter owns locomotion, so this can never issue a second,
		// contradictory Approach() in the same tick.
		if ( FFBotHelpers::CanDrivePath( me, m_path ) )
			m_path.Update( me );
		break;
	}

	case STATE_COOK:
	{
		CFFDetpack *dp = me->GetDetpack();
		if ( !dp )
			return Done( "Detpack detonated" );

		// Stand still and wait. If we're somehow still inside the blast
		// radius, push back further — m_giveupTimer will eventually catch a
		// stuck case.
		const float distSq = ( me->GetAbsOrigin() - m_detpackPos ).LengthSqr();
		if ( distSq < ( FFBOT_DETPACK_SAFE_DIST * FFBOT_DETPACK_SAFE_DIST ) )
		{
			m_state = STATE_RETREAT;
			break;
		}
		break;
	}
	}

	return Continue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotDemomanDetpack::ShouldRetreat( const INextBot *me ) const
{
	// We're already managing our own retreat (post-placement). Don't let the
	// generic low-HP retreat hijack our path — that would route home and
	// abandon the placement mid-build.
	return ANSWER_NO;
}
