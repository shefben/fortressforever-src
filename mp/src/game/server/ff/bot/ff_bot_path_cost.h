//========= Fortress Forever Bot =============================================//
//
// CFFBotPathCost — IPathCost functor used when computing PathFollower paths
// for an FF bot. Phase 3: minimal cost model — straight distance, with jump
// and drop checks against locomotion limits, and an enemy-spawn-room avoidance
// rule. Will grow as later phases add danger / sentry-line / class restriction
// considerations.
//
//===========================================================================//

#ifndef FF_BOT_PATH_COST_H
#define FF_BOT_PATH_COST_H
#ifdef _WIN32
#pragma once
#endif

#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"
#include "ff_nav_area.h"
#include "ff_door_link.h"
#include "ff_bot_mapintel.h"
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "ammodef.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "tier1/utlvector.h"

enum FFBotRouteType
{
	FFBOT_DEFAULT_ROUTE,
	FFBOT_FASTEST_ROUTE,
};


class CFFBotPathCost : public IPathCost
{
public:
	CFFBotPathCost( CFFBot *me, FFBotRouteType routeType = FFBOT_DEFAULT_ROUTE )
	{
		m_me = me;
		m_routeType = routeType;
		ILocomotion *loco = me->GetLocomotionInterface();
		m_stepHeight    = loco ? loco->GetStepHeight()      : 18.0f;
		m_maxJumpHeight = loco ? loco->GetMaxJumpHeight()   : 64.0f;
		m_maxDropHeight = loco ? loco->GetDeathDropHeight() : 200.0f;

		// Cache caltrop positions once at construction so per-edge cost
		// queries don't iterate gEntList. Caltrops persist for several
		// seconds; cache is fresh enough for one path computation.
		m_caltrops.RemoveAll();
		CBaseEntity *e = NULL;
		while ( ( e = gEntList.FindEntityByClassname( e, "caltrop" ) ) != NULL )
		{
			m_caltrops.AddToTail( e->GetAbsOrigin() );
		}

		// Cache class identity + ammo state so per-edge query stays fast.
		m_classSlot = me->GetClassSlot();
		m_isLowAmmo  = false;
		m_isLowHealth = false;
		const int hp = me->GetHealth();
		const int hpMax = me->GetMaxHealth();
		if ( hpMax > 0 && hp < ( hpMax / 2 ) )
			m_isLowHealth = true;
		CFFWeaponBase *active = me->GetActiveFFWeapon();
		if ( active )
		{
			const int ammoType = active->GetPrimaryAmmoType();
			if ( ammoType >= 0 )
			{
				const int reserve = me->GetAmmoCount( ammoType );
				const int clip = active->Clip1();
				// Heuristic: < 20% of typical loadout or zero in clip
				// with low reserve.
				if ( reserve < 20 && clip <= 0 )
					m_isLowAmmo = true;
			}
		}
		// Engineers also rate as "low ammo" when cells are short — that's
		// the resource that gates SG upgrades and repairs, so the resupply
		// discount should pull them through ammo pickups even when their
		// active weapon's clip is fine.
		if ( m_classSlot == CLASS_ENGINEER )
		{
			const int cellsAmmoType = GetAmmoDef()->Index( AMMO_CELLS );
			if ( cellsAmmoType >= 0 && me->GetAmmoCount( cellsAmmoType ) < 50 )
				m_isLowAmmo = true;
		}
	}

	virtual float operator()( CNavArea *baseArea, CNavArea *fromArea, const CNavLadder *ladder, const CFuncElevator *elevator, float length ) const OVERRIDE
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( baseArea );

		// First area in path — free.
		if ( fromArea == NULL )
			return 0.0f;

		if ( !m_me->GetLocomotionInterface()->IsAreaTraversable( area ) )
			return -1.0f;

		// Class restriction mask — if the area is restricted to specific
		// classes and ours isn't listed, refuse traversal.
		if ( !area->IsClassAllowed( m_classSlot ) )
			return -1.0f;

		// Don't path through enemy spawn rooms.
		const int myTeam = m_me->GetTeamNumber();
		if ( area->HasFFTag( FF_NAV_SPAWN_ANY ) )
		{
			int myOwnSpawn = CFFNavArea::SpawnTagForTeam( myTeam );
			if ( ( area->GetFFTags() & FF_NAV_SPAWN_ANY & ~myOwnSpawn ) != 0 )
				return -1.0f;
		}

		// One-way door check: refuse traversal through doors we can't open
		// from this side (FF spawn room exit/entry pattern).
		if ( CFFDoorLinkRegistry::Get().IsBlockedConnection( fromArea, area ) )
			return -1.0f;

		// Recent stuck position: when this bot got severely stuck within
		// the last 30s, penalize areas within 256u so the next path
		// computation routes around the same wedge instead of barreling
		// back into it.
		if ( m_me->m_recentStuckExpireTime > gpGlobals->curtime )
		{
			const float dSq = ( m_me->m_recentStuckPos - area->GetCenter() ).LengthSqr();
			const float radSq = 256.0f * 256.0f;
			if ( dSq < radSq )
			{
				const float closeness = 1.0f - sqrtf( dSq ) / 256.0f;
				return ( fromArea ? ( fromArea->GetCenter() - area->GetCenter() ).Length() : 0.0f ) * 4.0f
					+ 200.0f * closeness;
			}
		}

		// Caltrop avoidance: any nav-area within ~80u of a deployed caltrop
		// gets a steep penalty so the path routes around.
		const Vector areaCenter = area->GetCenter();
		for ( int i = 0; i < m_caltrops.Count(); ++i )
		{
			const float dSq = ( m_caltrops[ i ] - areaCenter ).LengthSqr();
			if ( dSq < ( 80.0f * 80.0f ) )
			{
				return ( fromArea ? ( fromArea->GetCenter() - areaCenter ).Length() : 0.0f ) * 6.0f + 100.0f;
			}
		}

		// ---- Tag-based modifiers ------------------------------------------
		const int tags = area->GetFFTags();
		float costMul = 1.0f;
		float costFlat = 0.0f;

		// Water — drowning risk for non-spies. Spies treat water as a
		// stealth corridor (no penalty).
		if ( ( tags & FF_NAV_WATER ) && m_classSlot != CLASS_SPY )
			costMul *= 1.5f;

		// Backdoor — bonus for spies, neutral for others.
		if ( ( tags & FF_NAV_BACKDOOR ) && m_classSlot == CLASS_SPY )
			costMul *= 0.7f;

		// Mancannon — slight bonus for fast classes who can chain a jump
		// and make distance.
		if ( ( tags & FF_NAV_MANCANNON ) &&
			 ( m_classSlot == CLASS_SCOUT || m_classSlot == CLASS_SPY ||
			   m_classSlot == CLASS_MEDIC ) )
			costMul *= 0.85f;

		// Resupply discount when low on ammo or health — bot routes
		// through pickups instead of straight at objective.
		if ( ( tags & FF_NAV_RESUPPLY ) && ( m_isLowAmmo || m_isLowHealth ) )
			costMul *= 0.6f;

		// Hot-zone heatmap: areas where lots of dying happens get a small
		// penalty so paths drift away from established kill zones unless
		// they're directly on the way.
		const float danger = area->GetDangerScore();
		if ( danger > 1.0f )
			costFlat += danger * 8.0f;

		// Demoman bonus on choke points — best places to drop pipes/stickies.
		if ( ( tags & FF_NAV_CHOKE ) && m_classSlot == CLASS_DEMOMAN )
			costMul *= 0.85f;

		// Per-class signature routing biases:
		switch ( m_classSlot )
		{
		case CLASS_SCOUT:
			// Fast class — outruns danger, doesn't need to detour. Halve
			// the danger penalty so they take the shortest dangerous lane.
			if ( costFlat > 0.0f ) costFlat *= 0.5f;
			// Slight preference for choke (in-and-out fast).
			if ( tags & FF_NAV_CHOKE ) costMul *= 0.95f;
			break;

		case CLASS_HWGUY:
			// Slow class — every step matters. Stronger choke preference
			// (good area-denial spot) and stronger jump penalty (slow them
			// down further).
			if ( tags & FF_NAV_CHOKE ) costMul *= 0.85f;
			break;

		case CLASS_SOLDIER:
			// Tank class — push through most direct route. Reduced danger
			// penalty (rockets clear chokes).
			if ( costFlat > 0.0f ) costFlat *= 0.7f;
			break;

		case CLASS_PYRO:
			// Short-range — wants to be IN chokes, not avoiding them.
			if ( tags & FF_NAV_CHOKE ) costMul *= 0.9f;
			break;

		case CLASS_MEDIC:
			// Avoid chokes (low HP, low DPS) — prefer flanks.
			if ( tags & FF_NAV_CHOKE ) costMul *= 1.15f;
			break;

		case CLASS_SPY:
			// Already has backdoor + water + sentry-pass-through bonuses.
			// Add: avoid CHOKE (predictable lanes get pre-aimed).
			if ( tags & FF_NAV_CHOKE ) costMul *= 1.20f;
			break;

		default:
			break;
		}

		// Intercept-lane bonus when our flag is being carried away — runtime
		// tag; defenders converge on the carrier route.
		if ( ( tags & FF_NAV_INTERCEPT_LANE ) && m_classSlot != CLASS_CIVILIAN )
			costMul *= 0.7f;

		// Route entropy + last-choke memory: bot-specific noise on every
		// area's cost so different bots see different shortest paths. Plus
		// a flat penalty for the choke we took *last* time so we vary
		// routes round-to-round.
		// Hash: small deterministic mix of bot seed and area ID.
		const unsigned int h0 = m_me->m_routeSeed * 2654435769U;
		const unsigned int h1 = ( h0 + area->GetID() * 22695477U ) ^ ( h0 >> 13 );
		// Map to ±5% on the multiplier.
		const float noise = ( ( (int)( h1 % 1001 ) ) - 500 ) * 0.0001f;
		costMul *= ( 1.0f + noise );

		if ( m_me->m_lastRouteChokeID != 0 &&
			 ( tags & FF_NAV_CHOKE ) &&
			 area->GetID() == m_me->m_lastRouteChokeID )
		{
			costMul *= 1.25f;	// took this last time — try a different lane
		}

		// Enemy sentry exposure: any area with LOS to a live enemy SG
		// (within 1200u) gets a steep penalty. Without this, bots feed
		// sentries by walking the most direct path through their arc.
		// Skip the check entirely when class loadout makes us SG-resistant
		// or specifically tasked with killing it (engineer EMP / spy sap).
		if ( m_classSlot != CLASS_ENGINEER && m_classSlot != CLASS_SPY &&
			 FFBotMapIntel::IsExposedToEnemySentry( m_me->GetTeamNumber(), areaCenter, 1200.0f ) )
		{
			costMul *= 3.0f;
			costFlat += 250.0f;
		}

		// Distance traveled.
		float dist;
		if ( ladder )
		{
			dist = ladder->m_length;
		}
		else if ( length > 0.0f )
		{
			dist = length;
		}
		else
		{
			dist = ( area->GetCenter() - fromArea->GetCenter() ).Length();
		}

		// Height-change check.
		float deltaZ = fromArea->ComputeAdjacentConnectionHeightChange( area );
		if ( deltaZ >= m_stepHeight )
		{
			if ( deltaZ >= m_maxJumpHeight )
				return -1.0f;	// can't reach
			dist *= 2.0f;		// jumping is slower
		}
		else if ( deltaZ < -m_maxDropHeight )
		{
			return -1.0f;		// drop too far
		}

		return dist * costMul + costFlat;
	}

private:
	CFFBot *m_me;
	FFBotRouteType m_routeType;
	float m_stepHeight;
	float m_maxJumpHeight;
	float m_maxDropHeight;

	// Bot state cached for per-edge cost lookup.
	int  m_classSlot;
	bool m_isLowAmmo;
	bool m_isLowHealth;

	// Caltrop positions cached at construction for per-edge avoidance.
	CUtlVector< Vector > m_caltrops;
};


#endif // FF_BOT_PATH_COST_H
