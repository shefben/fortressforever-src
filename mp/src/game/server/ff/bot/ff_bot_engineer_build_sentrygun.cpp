//========= Fortress Forever Bot =============================================//
//
// CFFBotEngineerBuildSentryGun — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_engineer_build_sentrygun.h"
#include "ff_bot_path_cost.h"
#include "ff_bot_get_ammo.h"
#include "ff_bot_helpers.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "ff_info_script.h"
#include "ff_player.h"
#include "ammodef.h"
#include "entitylist.h"
#include "nav_mesh.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Don't drop a fresh SG within this radius of an existing friendly one.
#define FFBOT_FRIENDLY_SG_MIN_SEPARATION	300.0f

// How close we need to be to the chosen spot before firing the build command.
#define FFBOT_SG_BUILD_TOLERANCE			60.0f

// Cells (FF AMMO_CELLS) needed to start a sentry build. Real cost is ~130;
// we want a comfortable buffer.
#define FFBOT_SG_BUILD_MIN_CELLS			130


//-----------------------------------------------------------------------------
CFFBotEngineerBuildSentryGun::CFFBotEngineerBuildSentryGun( void )
{
	m_haveBuildLocation = false;
	m_buildIssued = false;
	m_triesLeft = 5;
	m_buildLocation.Init();
}


//-----------------------------------------------------------------------------
bool CFFBotEngineerBuildSentryGun::IsLocationFreeOfFriendlySentries( const Vector &where, int myTeam ) const
{
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "FF_SentryGun" ) ) != NULL )
	{
		if ( e->GetTeamNumber() != myTeam )
			continue;
		const float dSq = ( e->GetAbsOrigin() - where ).LengthSqr();
		if ( dSq < FFBOT_FRIENDLY_SG_MIN_SEPARATION * FFBOT_FRIENDLY_SG_MIN_SEPARATION )
			return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Pick the highest-scoring FF_NAV_SENTRY_SPOT area, falling back to a nav
// area near the team's flag/cap if no mapper hints exist.
//-----------------------------------------------------------------------------
bool CFFBotEngineerBuildSentryGun::ChooseBuildLocation( CFFBot *me )
{
	const int myTeam = me->GetTeamNumber();

	// 1. Scan FF_NAV_SENTRY_SPOT mapper hints.
	float bestScore = -FLT_MAX;
	Vector bestPos = vec3_origin;
	bool gotHint = false;

	const NavAreaVector &areas = TheNavAreas;
	for ( int i = 0; i < areas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( areas[ i ] );
		if ( !( area->GetAttributesFF() & ( FF_NAV_SENTRY_SPOT | FF_NAV_AUTO_SENTRY_SPOT ) ) )
			continue;

		const Vector pos = area->GetCenter();
		if ( !IsLocationFreeOfFriendlySentries( pos, myTeam ) )
			continue;

		// Score: prefer hints that are deeper in our own territory (we want
		// our sentry guarding our base, not pushed up). Reward: low enemy
		// incursion distance == close to enemy team's spawn (forward); high
		// enemy incursion distance == close to our spawn (defensive).
		// FF defenders typically want forward-defensive: ~mid-incursion.
		float score = 0.0f;
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			if ( t == myTeam )
				continue;
			const float incursion = area->GetIncursionDistance( t );
			score += incursion;
		}

		if ( score > bestScore )
		{
			bestScore = score;
			bestPos = pos;
			gotHint = true;
		}
	}

	if ( gotHint )
	{
		m_buildLocation = bestPos;
		m_haveBuildLocation = true;
		return true;
	}

	// 2. Fall back: build near our flag/cap.
	CBaseEntity *anchor = FFBotHelpers::FindOwnFlag( myTeam );
	if ( !anchor )
		anchor = FFBotHelpers::FindOwnCapPoint( myTeam, me->GetAbsOrigin() );
	if ( anchor )
	{
		const Vector pos = anchor->GetAbsOrigin();
		if ( IsLocationFreeOfFriendlySentries( pos, myTeam ) )
		{
			m_buildLocation = pos;
			m_haveBuildLocation = true;
			return true;
		}
	}

	// 3. Last resort: build wherever we are — but if we're standing in
	// water, the SG would sink and be useless. Walk back to the closest
	// dry nav area instead.
	const Vector myPos = me->GetAbsOrigin();
	CNavArea *here = me->GetLastKnownArea();
	if ( here && static_cast< CFFNavArea * >( here )->HasAttributeFF( FF_NAV_WATER | FF_NAV_UNDERWATER ) )
	{
		float bestDistSq = FLT_MAX;
		Vector bestDryPos = myPos;
		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *a = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( a->HasAttributeFF( FF_NAV_WATER | FF_NAV_UNDERWATER ) )
				continue;
			const float dSq = ( a->GetCenter() - myPos ).LengthSqr();
			if ( dSq < bestDistSq )
			{
				bestDistSq = dSq;
				bestDryPos = a->GetCenter();
			}
		}
		m_buildLocation = bestDryPos;
	}
	else
	{
		m_buildLocation = myPos;
	}
	m_haveBuildLocation = true;
	return true;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEngineerBuildSentryGun::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_buildIssued = false;
	m_triesLeft = 5;

	// Already have a sentry — done.
	if ( me->GetSentryGun() )
		return Done( "Sentry already built" );

	if ( !ChooseBuildLocation( me ) )
		return Done( "No build location" );

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEngineerBuildSentryGun::Update( CFFBot *me, float interval )
{
	// Sentry exists — we're done.
	if ( me->GetSentryGun() )
		return Done( "Sentry built" );

	// Out of cells? Detour to ammo.
	const int cellsType = GetAmmoDef()->Index( AMMO_CELLS );
	const int cells = ( cellsType >= 0 ) ? me->GetAmmoCount( cellsType ) : 0;
	if ( cells < FFBOT_SG_BUILD_MIN_CELLS )
	{
		if ( CFFBotGetAmmo::IsPossible( me ) )
		{
			return SuspendFor( new CFFBotGetAmmo, "Need cells to build sentry" );
		}
		// No ammo source reachable — keep moving toward build spot, hope
		// teammate dispenser drops cells, or pick this up again later.
	}

	if ( !m_haveBuildLocation )
		return Done( "No build location" );

	const float distToBuildSq =
		( me->GetAbsOrigin() - m_buildLocation ).LengthSqr();

	if ( distToBuildSq > FFBOT_SG_BUILD_TOLERANCE * FFBOT_SG_BUILD_TOLERANCE )
	{
		// Still moving to build location.
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			if ( !m_path.Compute( me, m_buildLocation, cost ) )
				return Done( "No path to build location" );
		}
		// FIX 1 — single movement authority. CanDrivePath publishes the
		// path goal for the aim driver and refuses while the movement
		// arbiter owns locomotion, so this can never issue a second,
		// contradictory Approach() in the same tick.
		if ( FFBotHelpers::CanDrivePath( me, m_path ) )
			m_path.Update( me );
		return Continue();
	}

	// At build location. Aim toward enemy invasion vector before triggering
	// the build — FF sentries auto-rotate but first-shot direction matters.
	if ( !m_buildIssued )
	{
		CFFNavArea *myArea = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
		if ( myArea )
		{
			const CUtlVector< CFFNavArea * > &invasion =
				myArea->GetEnemyInvasionAreaVector( me->GetTeamNumber() );
			if ( invasion.Count() > 0 )
			{
				CFFNavArea *target = invasion[ 0 ];
				IBody *body = me->GetBodyInterface();
				if ( body )
				{
					body->AimHeadTowards( target->GetCenter(), IBody::CRITICAL,
						0.5f, NULL, "Sentry build aim" );
				}
			}
		}

		// Trigger the build (ff_player's PostBuildGenericThink will cancel
		// if we drift > 128u during the 3s build, so stop moving).
		me->Command_BuildSentryGun();
		m_buildIssued = true;
		m_searchTimer.Start( 5.0f );	// expect build to finish within 5s
		return Continue();
	}

	// Build issued — wait for SG to appear in GetSentryGun(). If it doesn't
	// after a few seconds, retry with a fresh location.
	if ( m_searchTimer.IsElapsed() )
	{
		--m_triesLeft;
		if ( m_triesLeft <= 0 )
			return Done( "Build attempts exhausted" );

		m_buildIssued = false;
		m_haveBuildLocation = false;
		ChooseBuildLocation( me );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEngineerBuildSentryGun::OnInjured( CFFBot *me, const CTakeDamageInfo &info )
{
	// Took damage during build — usually means we're under attack and need
	// to defer. Bail out so the parent action can decide (retreat, attack).
	return TryDone( RESULT_IMPORTANT, "Under attack during build" );
}
