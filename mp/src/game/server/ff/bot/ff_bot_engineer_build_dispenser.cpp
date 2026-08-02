//========= Fortress Forever Bot =============================================//
//
// CFFBotEngineerBuildDispenser — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_engineer_build_dispenser.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "ff_buildable_dispenser.h"
#include "ff_nav_area.h"
#include "ff_player.h"
#include "ammodef.h"
#include "nav_mesh.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Engineer wants the dispenser near the sentry so a single spanner-swing
// position can hit both. Target window: 80-150u from SG.
#define FFBOT_DISP_NEAR_SG_MIN	80.0f
#define FFBOT_DISP_NEAR_SG_MAX	150.0f

#define FFBOT_DISP_BUILD_TOLERANCE	60.0f

// Dispenser cost in cells (FF). 100 is the typical figure; a buffer ensures
// we don't try and instantly fail.
#define FFBOT_DISP_BUILD_MIN_CELLS	100


//-----------------------------------------------------------------------------
CFFBotEngineerBuildDispenser::CFFBotEngineerBuildDispenser( void )
{
	m_haveBuildLocation = false;
	m_buildIssued = false;
	m_triesLeft = 3;
	m_buildLocation.Init();
}


// A hand-authored dispenser spot within this distance of the sentry beats
// anything the ring sampler comes up with. Wider than the sampler's 150u
// window because an author placing the marker knows things the sampler can't —
// where the engineer can actually stand, which corner eats splash.
#define FFBOT_DISP_HINT_MAX_FROM_SG	400.0f

// With no sentry yet, a hint has to be somewhere we'd plausibly go.
#define FFBOT_DISP_HINT_MAX_FROM_ME	1200.0f


//-----------------------------------------------------------------------------
// FF_NAV2_DISPENSER_SPOT — placed by hand with ff_nav_place dispenser.
//
// A hint, not an order: if none is in range we fall straight through to the
// geometric search below, exactly as before.
//-----------------------------------------------------------------------------
static bool FindAuthoredDispenserSpot( const Vector &anchor, float maxRange, Vector *out )
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return false;

	const float maxRangeSq = maxRange * maxRange;
	CFFNavArea *best = NULL;
	float bestDistSq = FLT_MAX;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area || !area->HasAttributeFF2( FF_NAV2_DISPENSER_SPOT ) )
			continue;

		const float distSq = ( area->GetCenter() - anchor ).LengthSqr();
		if ( distSq > maxRangeSq || distSq >= bestDistSq )
			continue;

		bestDistSq = distSq;
		best = area;
	}

	if ( !best )
		return false;

	*out = best->GetCenter();
	return true;
}


//-----------------------------------------------------------------------------
bool CFFBotEngineerBuildDispenser::ChooseBuildLocation( CFFBot *me )
{
	CFFSentryGun *sg = me->GetSentryGun();
	if ( !sg )
	{
		// No SG yet. An authored spot near us still beats "wherever I happen
		// to be standing", which is what this used to do unconditionally.
		if ( FindAuthoredDispenserSpot( me->GetAbsOrigin(), FFBOT_DISP_HINT_MAX_FROM_ME, &m_buildLocation ) )
		{
			m_haveBuildLocation = true;
			return true;
		}

		m_buildLocation = me->GetAbsOrigin();
		m_haveBuildLocation = true;
		return true;
	}

	const Vector sgPos = sg->GetAbsOrigin();

	if ( FindAuthoredDispenserSpot( sgPos, FFBOT_DISP_HINT_MAX_FROM_SG, &m_buildLocation ) )
	{
		m_haveBuildLocation = true;
		return true;
	}

	// Sample several offsets around the SG; pick the first nav-area-traceable
	// spot in [80, 150]u with clear LOS to the SG.
	for ( int attempt = 0; attempt < 12; ++attempt )
	{
		const float angle = ( (float)attempt / 12.0f ) * 2.0f * M_PI_F;
		const float dist = RandomFloat( FFBOT_DISP_NEAR_SG_MIN, FFBOT_DISP_NEAR_SG_MAX );
		const Vector candidate(
			sgPos.x + cosf( angle ) * dist,
			sgPos.y + sinf( angle ) * dist,
			sgPos.z );

		// Must be a valid nav location.
		if ( !TheNavMesh )
			break;
		CNavArea *area = TheNavMesh->GetNearestNavArea( candidate, false, 64.0f );
		if ( !area )
			continue;

		// Reject water — dispenser underwater is useless (animations
		// break, can't approach from dry land easily). Surface water
		// rejected too; FF dispensers don't deploy properly in liquid.
		if ( static_cast< CFFNavArea * >( area )->HasAttributeFF( FF_NAV_WATER | FF_NAV_UNDERWATER ) )
			continue;

		// Must have LOS to the SG so the engineer can spanner-swing both.
		trace_t tr;
		UTIL_TraceLine( area->GetCenter() + Vector( 0, 0, 32.0f ),
			sgPos + Vector( 0, 0, 32.0f ),
			MASK_VISIBLE_AND_NPCS, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction < 0.95f )
			continue;

		m_buildLocation = area->GetCenter();
		m_haveBuildLocation = true;
		return true;
	}

	// No good slot — drop where we are if we're already near the SG.
	const float distSq = ( me->GetAbsOrigin() - sgPos ).LengthSqr();
	if ( distSq < FFBOT_DISP_NEAR_SG_MAX * FFBOT_DISP_NEAR_SG_MAX )
	{
		m_buildLocation = me->GetAbsOrigin();
		m_haveBuildLocation = true;
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEngineerBuildDispenser::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_buildIssued = false;
	m_triesLeft = 3;

	if ( me->GetDispenser() )
		return Done( "Dispenser already built" );

	if ( !ChooseBuildLocation( me ) )
		return Done( "No dispenser location near sentry" );

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEngineerBuildDispenser::Update( CFFBot *me, float interval )
{
	if ( me->GetDispenser() )
		return Done( "Dispenser built" );

	const int cellsType = GetAmmoDef()->Index( AMMO_CELLS );
	const int cells = ( cellsType >= 0 ) ? me->GetAmmoCount( cellsType ) : 0;
	if ( cells < FFBOT_DISP_BUILD_MIN_CELLS )
	{
		return Done( "Need cells before building dispenser" );
	}

	if ( !m_haveBuildLocation )
		return Done( "No build location" );

	const float distSq = ( me->GetAbsOrigin() - m_buildLocation ).LengthSqr();
	if ( distSq > FFBOT_DISP_BUILD_TOLERANCE * FFBOT_DISP_BUILD_TOLERANCE )
	{
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			if ( !m_path.Compute( me, m_buildLocation, cost ) )
				return Done( "No path" );
		}
		// FIX 1 — single movement authority. CanDrivePath publishes the
		// path goal for the aim driver and refuses while the movement
		// arbiter owns locomotion, so this can never issue a second,
		// contradictory Approach() in the same tick.
		if ( FFBotHelpers::CanDrivePath( me, m_path ) )
			m_path.Update( me );
		return Continue();
	}

	if ( !m_buildIssued )
	{
		me->Command_BuildDispenser();
		m_buildIssued = true;
		m_searchTimer.Start( 5.0f );
		return Continue();
	}

	if ( m_searchTimer.IsElapsed() )
	{
		--m_triesLeft;
		if ( m_triesLeft <= 0 )
			return Done( "Exhausted dispenser placement attempts" );
		m_buildIssued = false;
		m_haveBuildLocation = false;
		ChooseBuildLocation( me );
	}

	return Continue();
}
