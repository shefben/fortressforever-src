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
#include "ff_bot_hazard.h"
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
		// Is a hazard volume currently hurting us? FF_NAV2_HAZARD_ZONE has
		// deliberately never carried a path cost of its own: the tag says a
		// trigger_hurt is there, not that it is switched on, and plenty of maps
		// have one covering a pit the nav mesh never touches or one that stays
		// inert for the whole round. Making it permanently expensive would
		// distort routing on every map for the sake of the few where it
		// matters, at the times it doesn't.
		//
		// So the cost is conditional on the only evidence that the thing is
		// live: this bot is being hurt by it right now.
		m_avoidHazardZones = FFBotHazard::IsSuffering( me );

		// Wading or swimming. Water exits are only interesting to somebody in
		// the water — from dry land they are just a piece of shoreline.
		m_inWater = ( me->GetWaterLevel() >= WL_Waist );

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

		// FIX 10 — a shut door must not delete the route.
		//
		// ILocomotion::IsAreaTraversable is just !area->IsBlocked( team ), and
		// CNavMesh::UpdateBlockedAreas flips IsBlocked true for any area a
		// closed door brush occupies. Returning -1 there removed the only way
		// out of a spawn room every time its gate shut: no path existed at
		// all, so the bot had nothing to follow and just milled around inside.
		//
		// Doorway areas (tagged by CFFNavMesh::MarkDoorwayAreas) are therefore
		// merely EXPENSIVE while blocked. A route still exists, so the bot
		// walks up to the door and CFFBotMainAction::HandleDoors opens it. The
		// penalty is large enough that a genuinely open alternate route always
		// wins, so bots don't queue at a door when a side corridor is free.
		//
		// This mirrors the exemption CFFNavMesh::ComputeIncursionDistances
		// already makes for FF_NAV_SPAWN_ROOM_EXIT areas.
		bool blockedDoorway = false;
		if ( !m_me->GetLocomotionInterface()->IsAreaTraversable( area ) )
		{
			if ( !area->HasAttributeFF( FF_NAV_DOORWAY | FF_NAV_SPAWN_ROOM_EXIT ) )
				return -1.0f;
			blockedDoorway = true;
		}

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

		// A door that will not open for us is a wall, not an expensive door.
		//
		// This is the one place where the doorway exemption above is wrong.
		// FF_NAV_DOORWAY exists so a SHUT door does not delete the route —
		// the bot walks up to it and opens it. A team-restricted door cannot
		// be opened by walking up to it, so keeping the route alive sends the
		// bot to stand in front of it indefinitely, and it will look for all
		// the world like a pathing bug rather than a permissions one.
		//
		// Applied to the enemy's respawn gates above all. The enemy spawn room
		// itself is already refused, but its gate areas straddle the threshold
		// and are frequently NOT tagged as spawn room, so without this the
		// cheapest route to a flag runs straight through the door the enemy
		// team spawns behind.
		if ( !area->IsDoorPassableByTeam( myTeam ) )
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

		// FIX 10 — currently-shut door. Passable, but only if there is no
		// better option. Flat rather than multiplicative so the penalty
		// doesn't scale away on short segments.
		if ( blockedDoorway )
			costFlat += 1500.0f;

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

		// FF_NAV2_DANGER — hand-authored with ff_nav_place danger. Pits,
		// crushers, the grinder everyone on the server knows to walk around.
		// Expensive rather than impassable, deliberately: an author marking a
		// hazard is describing a cost, not a wall, and turning it into a wall
		// is how you end up with no path at all through a corridor whose only
		// route happens to run past the thing.
		if ( area->HasAttributeFF2( FF_NAV2_DANGER ) )
			costMul *= 4.0f;

		// FF_NAV2_HAZARD_ZONE — a trigger_hurt volume, auto-detected. Charged
		// only while this bot is actually taking environmental damage; see the
		// note in the constructor for why it is not a permanent cost.
		//
		// Steeper than DANGER because it is not an opinion: something in this
		// volume is measurably taking our health away, and the route out should
		// leave it at the first opportunity rather than cut a corner through
		// the far side of the room.
		if ( m_avoidHazardZones && area->HasAttributeFF2( FF_NAV2_HAZARD_ZONE ) )
		{
			costMul *= 8.0f;
			costFlat += 500.0f;
		}

		// FF_NAV2_WATER_EXIT — the marked way out of a body of water. The mesh
		// knows the water connects to the shore; it does not know which stretch
		// of shore you can actually climb out at, which on FF's sewers and
		// canals is frequently one specific ladder foot out of forty metres of
		// wall. To a bot already in the water that is worth a real discount.
		if ( m_inWater && area->HasAttributeFF2( FF_NAV2_WATER_EXIT ) )
			costMul *= 0.4f;

		// Sentry-spot avoidance for non-engineers / non-spies. The mapper
		// hand-tagged areas with FF_NAV_SENTRY_SPOT — those are likely SG
		// positions. Engineers want to BUILD there; spies want to SAP.
		// Everyone else should route around.
		if ( m_classSlot != CLASS_ENGINEER && m_classSlot != CLASS_SPY &&
			 ( attrs & FF_NAV_SENTRY_SPOT ) )
		{
			costMul *= 1.5f;
		}

		// Water-aware cost. The auto-tagger (CFFBotAutoTagger) stamps
		// FF_NAV_WATER (feet wet) and FF_NAV_UNDERWATER (must swim) on
		// every relevant nav area at level init. We use the tags here
		// instead of probing engine point contents per edge — that was
		// fast enough but did a real physics query 30+ times per path
		// computation, and pre-tagged is strictly better.
		//
		// Cost matrix (multiplier, before route-flavor):
		//   FF_NAV_WATER       — 1.4× (slowed wading)
		//   FF_NAV_UNDERWATER  — 2.0× (must swim, slowest)
		//
		// Class-specific extra:
		//   HWGuy / Soldier    — heavy classes, +20% on top of water cost
		//   Scout / Spy        — fast in water, water cost trimmed 25%
		//
		// Route-flavor adjustment (existing system, applied last):
		//   ROUTE_FLAVOR_DRY    — water 1.4× extra (avoids water entirely)
		//   ROUTE_FLAVOR_WATER  — water ×0.6 (prefers water)
		//   ROUTE_FLAVOR_NEUTRAL — no change
		const bool isWater = ( attrs & FF_NAV_WATER ) != 0;
		const bool isUnderwater = ( attrs & FF_NAV_UNDERWATER ) != 0;
		if ( isWater || isUnderwater )
		{
			float waterMul = isUnderwater ? 2.0f : 1.4f;
			switch ( m_classSlot )
			{
			case CLASS_HWGUY:
			case CLASS_SOLDIER:
				waterMul *= 1.2f;
				break;
			case CLASS_SCOUT:
			case CLASS_SPY:
				waterMul *= 0.75f;
				break;
			default:
				break;
			}
			switch ( m_routeFlavor )
			{
			case CFFBot::ROUTE_FLAVOR_DRY:
				waterMul *= 1.4f;
				break;
			case CFFBot::ROUTE_FLAVOR_WATER:
				waterMul *= 0.6f;
				break;
			default:
				break;
			}
			costMul *= waterMul;
		}

		// Choke avoidance for non-defensive classes. Engineers / spies /
		// snipers want to be near chokes (build / sap / hold sightlines);
		// running classes route around so they don't bunch up there.
		if ( ( attrs & FF_NAV_CHOKE ) &&
			 m_classSlot != CLASS_ENGINEER &&
			 m_classSlot != CLASS_SPY &&
			 m_classSlot != CLASS_SNIPER )
		{
			costMul *= 1.15f;
		}

		// Ladder-adjacent discount. Vertical traversal via ladder is more
		// reliable than free jumps (which need momentum, can be blocked by
		// teammates, and aren't always in the nav graph). When two paths
		// have equal length but only one passes a ladder, we'd rather take
		// the ladder route. Small (5%) discount — enough to break ties
		// without overriding genuine shortcuts.
		if ( attrs & FF_NAV_NEAR_LADDER )
			costMul *= 0.95f;

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

		// ---- Height-change check -----------------------------------------
		//
		// WATER/LADDER FIX 1 — ladders and elevators must skip this entirely.
		//
		// CNavArea::ComputeAdjacentConnectionHeightChange searches the FOUR
		// COMPASS adjacency lists and returns FLT_MAX when the destination
		// isn't in any of them (nav_area.cpp). Ladder connections live in a
		// separate NavLadderConnectVector and elevator links in another, so
		// for every ladder edge this returned FLT_MAX, which is >=
		// m_maxJumpHeight, so we returned -1 and A* discarded the edge.
		//
		// Net effect: bots could not path up or down ANY ladder on ANY map,
		// and the vertical half of every sewer/shaft route was invisible to
		// them. The locomotor's whole ladder state machine (PlayerLocomotion
		// ::TraverseLadder / ClimbLadder / DescendLadder) was dead code
		// because no path ever contained a ladder segment.
		//
		// The vertical extent of a ladder or elevator is the entire point of
		// it — it is not a "can I jump this?" question.
		if ( ladder != NULL || elevator != NULL )
			return dist * costMul + costFlat;

		float deltaZ = fromArea->ComputeAdjacentConnectionHeightChange( area );

		// Defensive: FLT_MAX means "these areas aren't compass-adjacent", not
		// "there is an infinitely tall wall". Treat it as no height change
		// rather than as impassable.
		if ( deltaZ >= FLT_MAX || deltaZ <= -FLT_MAX )
			deltaZ = 0.0f;

		if ( deltaZ >= m_stepHeight )
		{
			if ( deltaZ >= m_maxJumpHeight )
				return -1.0f;	// can't reach
			dist *= 2.0f;		// jumping is slower
		}
		else if ( deltaZ < -m_maxDropHeight )
		{
			// WATER/LADDER FIX 2 — a long drop INTO water is survivable, and
			// on FF maps it's the normal way into a sewer. Only reject the
			// drop when we'd land on something hard.
			if ( !( attrs & ( FF_NAV_WATER | FF_NAV_UNDERWATER ) ) )
				return -1.0f;	// drop too far onto dry ground
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
	bool m_avoidHazardZones;	// environmental damage is landing on us right now
	bool m_inWater;				// wading or swimming, so water exits matter

	// Caltrop positions cached at construction for per-edge avoidance.
	CUtlVector< Vector > m_caltrops;

	// Persistent grenade-aftermath danger zones (fire patches, gas, slow
	// fields, enemy stickies). Same per-path snapshot as caltrops.
	CUtlVector< Vector > m_dangers;
};


#endif // FF_BOT_PATH_COST_H
