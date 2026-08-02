//========= Fortress Forever Bot =============================================//
//
// CFFBotDestroyEnemySentry — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_destroy_enemy_sentry.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "entitylist.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// FF sentry's targeting range. Defined locally in ff_buildable_sentrygun.cpp
// (#define SG_RANGE 1050.0f) and not exported; mirror it here.
#define FFBOT_SG_RANGE			1050.0f
#define FFBOT_SG_SAFE_DISTANCE	( 1.2f * FFBOT_SG_RANGE )


//-----------------------------------------------------------------------------
CFFBotDestroyEnemySentry::CFFBotDestroyEnemySentry( void )
{
	m_targetSentry = NULL;
	m_hasSafeAttackSpot = false;
	m_isAttackingSentry = false;
}


CFFBotDestroyEnemySentry::CFFBotDestroyEnemySentry( CFFSentryGun *targetSentry )
{
	m_targetSentry = targetSentry;
	m_hasSafeAttackSpot = false;
	m_isAttackingSentry = false;
}


//-----------------------------------------------------------------------------
bool CFFBotDestroyEnemySentry::IsPossible( CFFBot *me )
{
	if ( !me )
		return false;

	const int slot = me->GetClassSlot();
	switch ( slot )
	{
	case CLASS_SOLDIER:
	case CLASS_DEMOMAN:
	case CLASS_HWGUY:
	case CLASS_SCOUT:
		break;	// these classes can credibly engage a sentry
	default:
		return false;	// engy/spy build/sap; medic/civ no AOE; sniper needs lurk-LOS
	}

	// Need ammo to fire on the sentry — don't approach if we can't shoot.
	CFFWeaponBase *active = me->GetActiveFFWeapon();
	if ( active )
	{
		const int ammoType = active->GetPrimaryAmmoType();
		if ( ammoType >= 0 && me->GetAmmoCount( ammoType ) <= 0 && active->Clip1() <= 0 )
			return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
CFFSentryGun *CFFBotDestroyEnemySentry::FindClosestEnemySentry( CFFBot *me ) const
{
	const Vector myPos = me->GetAbsOrigin();
	const int myTeam = me->GetTeamNumber();
	CFFSentryGun *best = NULL;
	float bestDistSq = FLT_MAX;

	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "FF_SentryGun" ) ) != NULL )
	{
		CFFSentryGun *sg = static_cast< CFFSentryGun * >( e );
		if ( !sg->IsBuilt() )
			continue;
		if ( sg->GetTeamNumber() == myTeam )
			continue;
		const float dSq = ( sg->GetAbsOrigin() - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = sg;
		}
	}
	return best;
}


//-----------------------------------------------------------------------------
// Find a nav-area-center position with traceline LOS to the sentry, located
// at roughly 1.2x sentry range. Sample areas around the sentry within
// 1.0..1.5x range; pick closest with LOS.
//-----------------------------------------------------------------------------
void CFFBotDestroyEnemySentry::ComputeSafeAttackSpot( CFFBot *me )
{
	m_hasSafeAttackSpot = false;
	if ( !m_targetSentry || !TheNavMesh )
		return;

	const Vector sentryPos = m_targetSentry->GetAbsOrigin();
	const Vector myPos = me->GetAbsOrigin();

	// Walk all areas within the sentry's general radius, score by
	// (distance from me) for areas with LOS to sentry that are at safe range.
	CNavArea *bestArea = NULL;
	float bestDistSq = FLT_MAX;

	const NavAreaVector &areas = TheNavAreas;
	for ( int i = 0; i < areas.Count(); ++i )
	{
		CNavArea *area = areas[ i ];
		const Vector areaCenter = area->GetCenter();

		const float fromSentrySq = ( areaCenter - sentryPos ).LengthSqr();
		if ( fromSentrySq < FFBOT_SG_RANGE * FFBOT_SG_RANGE )
			continue;	// too close — in kill zone
		if ( fromSentrySq > ( 1.5f * FFBOT_SG_RANGE ) * ( 1.5f * FFBOT_SG_RANGE ) )
			continue;	// too far — not effective range

		// LOS check from area-center eye height to sentry.
		trace_t tr;
		const Vector eyeFrom = areaCenter + Vector( 0, 0, 64.0f );
		const Vector eyeTo = sentryPos + Vector( 0, 0, 32.0f );
		UTIL_TraceLine( eyeFrom, eyeTo, MASK_VISIBLE_AND_NPCS, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction < 0.99f )
			continue;

		const float fromMeSq = ( areaCenter - myPos ).LengthSqr();
		if ( fromMeSq < bestDistSq )
		{
			bestDistSq = fromMeSq;
			bestArea = area;
		}
	}

	if ( bestArea )
	{
		m_safeAttackSpot = bestArea->GetCenter();
		m_hasSafeAttackSpot = true;
	}
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDestroyEnemySentry::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_path.Invalidate();
	m_repathTimer.Invalidate();
	m_isAttackingSentry = false;

	if ( m_targetSentry == NULL )
	{
		m_targetSentry = FindClosestEnemySentry( me );
		if ( m_targetSentry == NULL )
			return Done( "No enemy sentry found" );
	}

	ComputeSafeAttackSpot( me );
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDestroyEnemySentry::Update( CFFBot *me, float interval )
{
	if ( m_targetSentry == NULL )
		return Done( "Target sentry is gone" );

	const Vector sentryPos = m_targetSentry->GetAbsOrigin();
	const float distToSentry = ( me->GetAbsOrigin() - sentryPos ).Length();

	const bool inAttackPosition = m_hasSafeAttackSpot &&
		me->IsRangeLessThan( m_safeAttackSpot, 60.0f );

	const bool clearLOS = me->IsLineOfFireClear( m_targetSentry );

	if ( inAttackPosition || clearLOS )
	{
		// Aim at sentry; per-tick FireWeaponAtEnemy in MainAction will fire.
		IBody *body = me->GetBodyInterface();
		if ( body )
		{
			body->AimHeadTowards( m_targetSentry, IBody::CRITICAL, 1.0f, NULL,
				"Aiming at enemy sentry" );
		}
		me->PressFireButton();
		m_isAttackingSentry = true;

		if ( distToSentry > FFBOT_SG_SAFE_DISTANCE )
		{
			// Out of return-fire range — hold and shoot.
			return Continue();
		}

		// In range — if sentry is firing on us, the bot's stuck-state /
		// retreat handling will deal with it. Don't pull a hard retreat
		// here; that's the caller's call.
		if ( inAttackPosition )
			return Continue();
	}
	else
	{
		m_isAttackingSentry = false;
	}

	// Move into position — toward safe-attack-spot when known, else
	// toward the sentry itself (path cost will steer around the kill zone
	// via combat-intensity penalty on hot areas).
	if ( !m_path.IsValid() || m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( 1.0f );
		const Vector goal = m_hasSafeAttackSpot ? m_safeAttackSpot : sentryPos;
		CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
		if ( !m_path.Compute( me, goal, cost ) )
			return Done( "No path to sentry" );
	}

	// FIX 1 — single movement authority. CanDrivePath publishes the
	// path goal for the aim driver and refuses while the movement
	// arbiter owns locomotion, so this can never issue a second,
	// contradictory Approach() in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me );
	return Continue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotDestroyEnemySentry::ShouldHurry( const INextBot *me ) const
{
	return m_isAttackingSentry ? ANSWER_YES : ANSWER_UNDEFINED;
}


QueryResultType CFFBotDestroyEnemySentry::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}


QueryResultType CFFBotDestroyEnemySentry::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	// While we're firing on the sentry, MainAction's per-tick fire is doing
	// the work — let the parent decide whether to engage other threats.
	return m_isAttackingSentry ? ANSWER_NO : ANSWER_UNDEFINED;
}
