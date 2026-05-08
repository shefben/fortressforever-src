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
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "ammodef.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "engine/IEngineTrace.h"
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

		// Cache persistent grenade-aftermath danger zones so the path
		// follower routes around them. Without this, bots run straight
		// through pyro fire / scout gas / hwguy slowfield / enemy sticky
		// traps and take damage they could have avoided. Each entity
		// becomes a hazard point; per-edge cost adds a steep penalty
		// for areas within the hazard radius.
		//
		// Only enemy ff_projectile_pl (sticky bombs) count — our team's
		// stickies don't hurt us. Other danger grenades affect everyone
		// in radius regardless of owner so no team filter applies.
		const int myTeam = me->GetTeamNumber();
		m_dangers.RemoveAll();
		static const char * const kDangerClasses[] = {
			"ff_grenade_napalmlet",   // pyro fire patch (after detonation)
			"ff_grenade_gas",         // scout gas cloud
			"ff_grenade_slowfield",   // hwguy slow zone
		};
		for ( int c = 0; c < ARRAYSIZE( kDangerClasses ); ++c )
		{
			CBaseEntity *de = NULL;
			while ( ( de = gEntList.FindEntityByClassname( de, kDangerClasses[ c ] ) ) != NULL )
			{
				m_dangers.AddToTail( de->GetAbsOrigin() );
			}
		}
		// Enemy sticky bombs.
		CBaseEntity *sb = NULL;
		while ( ( sb = gEntList.FindEntityByClassname( sb, "ff_projectile_pl" ) ) != NULL )
		{
			if ( sb->GetTeamNumber() == myTeam )
				continue;	// our own stickies — no friendly damage in normal play
			m_dangers.AddToTail( sb->GetAbsOrigin() );
		}

		// Cache class identity + ammo state so per-edge query stays fast.
		m_classSlot = me->GetClassSlot();
		m_routeFlavor = me->m_routeFlavor;
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
		if ( area->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY ) )
		{
			const int myOwnSpawn = CFFNavArea::SpawnRoomAttributeForTeam( myTeam );
			if ( ( area->GetAttributesFF() & FF_NAV_SPAWN_ROOM_ANY & ~myOwnSpawn ) != 0 )
				return -1.0f;
		}

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

		// Grenade-aftermath avoidance: any nav-area within ~150u of a
		// fire patch / gas cloud / slowfield / enemy sticky gets the
		// same kind of steep multiplier so the path routes around. The
		// 150u radius covers the napalmlet's flame radius (~80u) with
		// margin, the gas cloud, and stickies' splash range.
		for ( int i = 0; i < m_dangers.Count(); ++i )
		{
			const float dSq = ( m_dangers[ i ] - areaCenter ).LengthSqr();
			if ( dSq < ( 150.0f * 150.0f ) )
			{
				return ( fromArea ? ( fromArea->GetCenter() - areaCenter ).Length() : 0.0f ) * 6.0f + 200.0f;
			}
		}

		// ---- Attribute-based modifiers -----------------------------------
		const unsigned int attrs = area->GetAttributesFF();
		float costMul = 1.0f;
		float costFlat = 0.0f;

		// Resupply discount when low on ammo or health.
		const unsigned int kAnyResupply = FF_NAV_HAS_AMMO | FF_NAV_HAS_HEALTH |
		                                  FF_NAV_HAS_ARMOR | FF_NAV_HAS_GRENADES;
		if ( ( attrs & kAnyResupply ) && ( m_isLowAmmo || m_isLowHealth ) )
			costMul *= 0.6f;

		// Combat-intensity penalty: areas with active combat get a flat
		// cost so paths drift around hot zones unless directly on the way.
		// Replaces our previous custom danger-score system.
		const float combat = area->GetCombatIntensity();
		if ( combat > 0.1f )
			costFlat += combat * 200.0f;

		// Per-class signature: scouts ignore danger (fast), soldiers reduce
		// it (rockets clear chokes), medics avoid sentry-spotted lanes.
		switch ( m_classSlot )
		{
		case CLASS_SCOUT:
			if ( costFlat > 0.0f ) costFlat *= 0.5f;
			break;
		case CLASS_SOLDIER:
			if ( costFlat > 0.0f ) costFlat *= 0.7f;
			break;
		default:
			break;
		}

		// Sentry-spot avoidance for non-engineers / non-spies. The mapper
		// hand-tagged areas with FF_NAV_SENTRY_SPOT — those are likely SG
		// positions. Engineers want to BUILD there; spies want to SAP.
		// Everyone else should route around.
		if ( m_classSlot != CLASS_ENGINEER && m_classSlot != CLASS_SPY &&
			 ( attrs & FF_NAV_SENTRY_SPOT ) )
		{
			costMul *= 1.5f;
		}

		// Per-bot route flavor — drives some bots through the underwater
		// /alternate route while others take the main path. Without this,
		// every bot picks the shortest route (the main bridge on 2fort)
		// because the per-area noise below isn't large enough to flip
		// long-vs-short. Sampling water content per area is a physics
		// query but cheap (no contact, just point in space). Caltrop
		// caching above shows the same gEntList-once-per-path pattern.
		const int areaContents = enginetrace->GetPointContents( areaCenter );
		const bool isWaterArea = ( areaContents & ( CONTENTS_WATER | CONTENTS_SLIME ) ) != 0;
		if ( isWaterArea )
		{
			switch ( m_routeFlavor )
			{
			case CFFBot::ROUTE_FLAVOR_DRY:
				costMul *= 1.6f;	// avoid water — take the dry route
				break;
			case CFFBot::ROUTE_FLAVOR_WATER:
				costMul *= 0.7f;	// prefer water — sewer / underwater approach
				break;
			default:
				break;
			}
		}

		// Route entropy: per-bot deterministic noise so different bots
		// pick different shortest paths through the same map. ±20% — large
		// enough that medium-length detours can win against the shortest
		// route, so multi-bot teams actually spread across the map's
		// alternate corridors instead of all funneling through the same
		// choke. Smaller values (we used to run ±5%) only scrambled the
		// order of equally-short paths and produced no visible spread.
		const unsigned int h0 = m_me->m_routeSeed * 2654435769U;
		const unsigned int h1 = ( h0 + area->GetID() * 22695477U ) ^ ( h0 >> 13 );
		const float noise = ( ( (int)( h1 % 1001 ) ) - 500 ) * 0.0004f;	// ±20%
		costMul *= ( 1.0f + noise );

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
	unsigned char m_routeFlavor;
	bool m_isLowAmmo;
	bool m_isLowHealth;

	// Caltrop positions cached at construction for per-edge avoidance.
	CUtlVector< Vector > m_caltrops;

	// Persistent grenade-aftermath danger zones (fire patches, gas, slow
	// fields, enemy stickies). Same per-path snapshot as caltrops.
	CUtlVector< Vector > m_dangers;
};


#endif // FF_BOT_PATH_COST_H
