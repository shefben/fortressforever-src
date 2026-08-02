//========= Fortress Forever Bot =============================================//
//
// CFFBotSniperLurk — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_sniper_lurk.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_bot_melee_attack.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_info_script.h"
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "nav_mesh.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"
#include "NextBotKnownEntity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// SNIPER FIX 2 — patience was 10 seconds.
//
// A sniper holding an angle on a quiet lane is doing its job; ten seconds of
// nobody walking through is completely normal on a map like 2fort. At 10s the
// bot re-ran FindNewHome, and because that scorer mixes in
// TransientlyConsistentRandomValue (which changes every 30s window) it kept
// returning a DIFFERENT spot. Net effect: snipers left good high ground every
// ten seconds and spent most of the round walking between perches.
ConVar ff_bot_sniper_patience_duration( "ff_bot_sniper_patience_duration", "45",
	FCVAR_CHEAT, "How long a sniper bot waits without seeing an enemy before considering a new spot" );
ConVar ff_bot_sniper_melee_range( "ff_bot_sniper_melee_range", "300",
	FCVAR_CHEAT, "Sniper bot switches to melee when threat is within this range" );

// SNIPER FIX 3 — home tolerance was 60u, tested against the nav area CENTRE.
//
// Perches are cluttered: railings, crates, the lip of a battlement. A bot that
// can't physically stand within 60u of the area centre never set m_isAtHome,
// so it never stopped pathing — and the path follower kept nudging it around
// (and occasionally off) the perch forever. One nav area width is the honest
// tolerance here: if we're standing in the right area, we're home.
#define FFBOT_SNIPER_HOME_TOLERANCE	120.0f


//-----------------------------------------------------------------------------
CFFBotSniperLurk::CFFBotSniperLurk( void )
{
	m_homePosition.Init();
	m_isHomeValid = false;
	m_isAtHome = false;
	m_failCount = 0;
}


//-----------------------------------------------------------------------------
// Pick the best sniper home. Scoring:
//   + LOS to ≥1 enemy spawn exit (mandatory if any spot has it)
//   + close to our own cap point (anchors snipers near home base, not
//     deep in enemy territory where they get flanked)
//   + elevation advantage relative to the cap point (sniping perches
//     are by definition above the action — bonus +1.5 per Z unit)
//   - heavy penalty if another friendly sniper is already within 600u
//     (anti-cluster: spreads multiple snipers across multiple spots
//     instead of stacking on the closest one)
//   ± per-bot deterministic noise (TransientlyConsistentRandomValue)
//     so two snipers with otherwise-equal candidates pick differently
//
// Falls back to any FF_NAV_SNIPER_SPOT, then to the cap point itself.
//-----------------------------------------------------------------------------
// Collect "places where enemies will be" for LOS scoring. Enemy spawn exits
// alone are too restrictive — most FF maps have deep tunnel approaches where
// no perch on our side can see into the enemy's spawn doorway. Add our flag /
// our cap / their flag / their cap areas too: those are the high-traffic spots
// a flag-runner will pass through, which is what a sniper wants to cover.
//
// Factored out of FindNewHome so the "is my current perch still worth holding"
// check can score the bot's own position with exactly the same target set.
// SNIPER FIX 6 — FORWARD TARGETS AND HOME TARGETS ARE NOT THE SAME THING.
//
// These used to be one undifferentiated list, and "our own flag area" was in
// it. That meant an interior catwalk overlooking our OWN flag room counted as
// a valid sniping angle and collected the full +2000 LOS bonus — so the scorer
// actively rewarded perching inside the base. On 2fort that is exactly why
// snipers sat on the catwalks by the spawn room instead of going out to the
// position by the capture point.
//
// Forward targets = where the ENEMY comes from and goes to. Only these
// qualify a spot as a sniper perch.
static void CollectForwardTargetEyes( int myTeam, CUtlVector< Vector > &targetEyes )
{
	CFFNavMesh *ffMesh = TheFFNavMesh();
	if ( !ffMesh )
		return;

	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == myTeam )
			continue;

		// Enemy spawn exits (stops them right at the door if visible).
		const CUtlVector< CFFNavArea * > *exits = ffMesh->GetSpawnRoomExitAreas( t );
		if ( exits )
		{
			for ( int i = 0; i < exits->Count(); ++i )
				targetEyes.AddToTail( ( *exits )[ i ]->GetCenter() + Vector( 0, 0, 64.0f ) );
		}

		// Enemy flag/cap areas (covers retreating flag-carriers).
		const CUtlVector< CFFNavArea * > *flags = ffMesh->GetFlagAreas( t );
		if ( flags )
		{
			for ( int i = 0; i < flags->Count(); ++i )
				targetEyes.AddToTail( ( *flags )[ i ]->GetCenter() + Vector( 0, 0, 48.0f ) );
		}
		const CUtlVector< CFFNavArea * > *caps = ffMesh->GetCapAreas( t );
		if ( caps )
		{
			for ( int i = 0; i < caps->Count(); ++i )
				targetEyes.AddToTail( ( *caps )[ i ]->GetCenter() + Vector( 0, 0, 48.0f ) );
		}
	}
}

// Home targets = our own flag / cap. Worth a small bonus (a perch that also
// covers the approach to our own objective is a nice extra) but NEVER enough
// to qualify a spot on its own.
static void CollectHomeTargetEyes( int myTeam, CUtlVector< Vector > &targetEyes )
{
	CFFNavMesh *ffMesh = TheFFNavMesh();
	if ( !ffMesh )
		return;

	const CUtlVector< CFFNavArea * > *ownFlagAreas = ffMesh->GetFlagAreas( myTeam );
	if ( ownFlagAreas )
	{
		for ( int i = 0; i < ownFlagAreas->Count(); ++i )
			targetEyes.AddToTail( ( *ownFlagAreas )[ i ]->GetCenter() + Vector( 0, 0, 48.0f ) );
	}
	const CUtlVector< CFFNavArea * > *ownCapAreas = ffMesh->GetCapAreas( myTeam );
	if ( ownCapAreas )
	{
		for ( int i = 0; i < ownCapAreas->Count(); ++i )
			targetEyes.AddToTail( ( *ownCapAreas )[ i ]->GetCenter() + Vector( 0, 0, 48.0f ) );
	}
}

// Backwards-compatible combined list, used by the "is my current perch still
// live" check where either kind of target counts as a reason to stay.
static void CollectIngressTargetEyes( int myTeam, CUtlVector< Vector > &targetEyes )
{
	CollectForwardTargetEyes( myTeam, targetEyes );
	CollectHomeTargetEyes( myTeam, targetEyes );
}


// SNIPER FIX 7 — "is this spot outdoors?"
//
// This is the discriminator that actually separates a 2fort battlement from a
// 2fort catwalk. Both are elevated, both are near the objective, both are
// topologically between the two spawns — no distance or incursion metric tells
// them apart. The difference is that the real sniper position is open to the
// sky and overlooks the yard, and the catwalk is under a roof.
static bool AreaIsOutdoors( const Vector &spotPos )
{
	trace_t tr;
	UTIL_TraceLine( spotPos + Vector( 0, 0, 72.0f ),
	                spotPos + Vector( 0, 0, 4096.0f ),
	                MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );

	// Nothing overhead at all, or the thing overhead is the skybox.
	return ( tr.fraction >= 1.0f ) ||
	       ( ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) != 0 );
}


// Travel distance from the nearest ENEMY spawn. Larger = deeper inside our own
// territory. Used as a mild "get out toward the fight" pull that counteracts
// the -distanceToOurCap term, which otherwise drags perches inward.
// Returns -1 when unreachable / not yet computed.
static float EnemyIncursionDistance( CFFNavArea *area, int myTeam )
{
	float best = -1.0f;
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == myTeam )
			continue;
		const float d = area->GetIncursionDistance( t );
		if ( d < 0.0f )
			continue;
		if ( best < 0.0f || d < best )
			best = d;
	}
	return best;
}


// How many ingress targets can be seen from `eyeFrom`. Used both to score
// candidate perches (more lanes covered = better perch) and to decide whether
// the perch we're already standing on is still worth holding.
static int CountVisibleIngressTargets( const Vector &eyeFrom,
                                       const CUtlVector< Vector > &targetEyes )
{
	int count = 0;
	for ( int j = 0; j < targetEyes.Count(); ++j )
	{
		trace_t tr;
		UTIL_TraceLine( eyeFrom, targetEyes[ j ],
			MASK_VISIBLE_AND_NPCS, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 0.99f )
			++count;
	}
	return count;
}


bool CFFBotSniperLurk::FindNewHome( CFFBot *me )
{
	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();

	CUtlVector< Vector > targetEyes;		// forward (enemy-side) targets only
	CollectForwardTargetEyes( myTeam, targetEyes );

	CUtlVector< Vector > homeEyes;			// our own flag / cap
	CollectHomeTargetEyes( myTeam, homeEyes );

	CFFNavMesh *ffMesh = TheFFNavMesh();

	// Anchor: own cap (or own flag if no cap exists). Snipers should defend
	// their own side, not advance with the team.
	CBaseEntity *cap = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
	const Vector anchorPos = cap ? cap->GetAbsOrigin() :
		( FFBotHelpers::FindOwnFlag( myTeam ) ?
			FFBotHelpers::FindOwnFlag( myTeam )->GetAbsOrigin() : myPos );

	// "Outward" direction — what direction is *away from our base toward
	// the enemy*. Used to penalize spots that are physically high but
	// behind our spawn-room exit (= inside our base, looking back at the
	// flag room). Real sniper perches are FORWARD of our exit on the
	// outbound path toward the enemy approach.
	//
	// We compute it as: average enemy ingress eye centroid minus average
	// of our spawn-room exit centers. Falls back to anchor → average
	// targetEye if either side is missing.
	Vector ourExitCentroid = anchorPos;
	bool haveOurExits = false;
	if ( ffMesh )
	{
		const CUtlVector< CFFNavArea * > *ourExits =
			ffMesh->GetSpawnRoomExitAreas( myTeam );
		if ( ourExits && ourExits->Count() > 0 )
		{
			Vector sum = vec3_origin;
			for ( int i = 0; i < ourExits->Count(); ++i )
				sum += ( *ourExits )[ i ]->GetCenter();
			ourExitCentroid = sum * ( 1.0f / (float)ourExits->Count() );
			haveOurExits = true;
		}
	}
	Vector enemyCentroid = vec3_origin;
	if ( targetEyes.Count() > 0 )
	{
		Vector sum = vec3_origin;
		for ( int i = 0; i < targetEyes.Count(); ++i )
			sum += targetEyes[ i ];
		enemyCentroid = sum * ( 1.0f / (float)targetEyes.Count() );
	}
	Vector outwardDir = enemyCentroid - ourExitCentroid;
	outwardDir.z = 0.0f;
	const bool haveOutward = ( haveOurExits && outwardDir.NormalizeInPlace() > 1.0f );

	// 1. Sniper-tagged areas — either mapper-set (FF_NAV_SNIPER_SPOT) or
	// auto-discovered by CFFBotAutoTagger (FF_NAV_AUTO_SNIPER_SPOT). Both
	// count equally as "this is a known sniping perch."
	const NavAreaVector &areas = TheNavAreas;
	CUtlVector< CFFNavArea * > snipeSpots;
	for ( int i = 0; i < areas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( areas[ i ] );
		const unsigned int attrs = area->GetAttributesFF();
		if ( !( attrs & ( FF_NAV_SNIPER_SPOT | FF_NAV_AUTO_SNIPER_SPOT ) ) )
			continue;
		// Don't lurk in our own spawn room.
		if ( attrs & FF_NAV_SPAWN_ROOM_ANY )
			continue;
		snipeSpots.AddToTail( area );
	}

	// Score every spot.
	//
	// SNIPER FIX 8 — three-tier preference, resolved in a single pass:
	//     best  = sees a FORWARD target AND is outdoors   (a real perch)
	//     good  = sees a FORWARD target                   (indoor window/slit)
	//     any   = anything else                           (last resort)
	// We take the strictest bucket that has an entry. This is what stops the
	// bot settling on an interior catwalk while an actual overlook exists:
	// the catwalk lands in `good` or `any` and never beats an outdoor perch,
	// no matter how much closer to our cap it is.
	CFFNavArea *bestSpot = NULL;			float bestScore = -FLT_MAX;
	CFFNavArea *bestFwdSpot = NULL;			float bestFwdScore = -FLT_MAX;
	CFFNavArea *bestFwdOutdoorSpot = NULL;	float bestFwdOutdoorScore = -FLT_MAX;
	bool anyHasLOS = false;

	for ( int i = 0; i < snipeSpots.Count(); ++i )
	{
		CFFNavArea *spot = snipeSpots[ i ];
		const Vector spotPos = spot->GetCenter();
		const Vector eyeFrom = spotPos + Vector( 0, 0, 64.0f );

		// SNIPER FIX 4 — score COVERAGE, not just "can see something".
		//
		// This used to break out of the LOS loop on the first hit, so a corner
		// that squints at exactly one doorway scored identically to a
		// battlement overlooking three approach lanes and the flag route. With
		// the tie broken only by distance-to-cap and per-bot noise, the good
		// perch frequently lost. Counting targets makes the commanding
		// position win on merit.
		const int losCount = CountVisibleIngressTargets( eyeFrom, targetEyes );
		const bool hasLOS = ( losCount > 0 );
		if ( hasLOS )
			anyHasLOS = true;

		const bool isOutdoor = AreaIsOutdoors( spotPos );

		// Distance from cap — closer is better (defending home).
		const float distToAnchor = ( spotPos - anchorPos ).Length();
		if ( distToAnchor > 3000.0f )
			continue;	// too far from our cap to actually defend it

		float score = -distToAnchor;
		if ( hasLOS )
			score += 2000.0f;	// big bonus — LOS-to-exits beats closer spots

		// Each additional lane covered is worth roughly 400 units of walking.
		score += ( losCount - 1 ) * 400.0f;

		// Covering our own objective is a bonus, never a qualification. Capped
		// so a spot that only watches our own flag room can't out-score one
		// that actually watches the enemy approach.
		const int homeLosCount = CountVisibleIngressTargets( eyeFrom, homeEyes );
		score += MIN( homeLosCount, 2 ) * 150.0f;

		if ( isOutdoor )
			score += 500.0f;

		// Mild pull toward the fight. -distToAnchor drags candidates back
		// toward our own cap, which on a base-interior map means "inward";
		// this offsets it by preferring spots the enemy reaches sooner.
		const float enemyIncursion = EnemyIncursionDistance( spot, myTeam );
		if ( enemyIncursion >= 0.0f )
			score -= enemyIncursion * 0.15f;

		// Elevation advantage. spotPos.z - anchorPos.z = how much higher
		// than the cap. Sniping perches are typically +64 to +256 above.
		// Heavy weight (×4) because elevated perches are *the* sniper
		// position — without this, a ground-level area near the flag was
		// winning over a battlement directly above it.
		const float zDelta = spotPos.z - anchorPos.z;
		if ( zDelta > 0 )
			score += zDelta * 4.0f;

		// Outward-facing bias. A sniper post is a place that looks AT
		// the enemy approach, not back at our own flag room. Project
		// (spot - our spawn exit centroid) onto the outward direction.
		//   forwardness > 0 → forward of our exit, points at enemy
		//   forwardness < 0 → behind our exit = inside the base
		// Hard reject on inward-facing — even a -6000 score penalty
		// wasn't enough when EVERY candidate was inward-facing (the
		// "least bad" still won). Skipping outright forces the picker
		// to fall through to auto-discovery / HIGH_GROUND tiers if no
		// outward-facing tagged spot exists.
		if ( haveOutward )
		{
			const float forwardness = ( spotPos - ourExitCentroid ).Dot( outwardDir );
			if ( forwardness < 0.0f )
				continue;
			score += forwardness * 0.5f;
		}

		// Anti-cluster penalty — 4000 units pushes a clustered spot
		// effectively out of contention.
		for ( int p = 1; p <= gpGlobals->maxClients; ++p )
		{
			CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( p ) );
			if ( !pp || pp == me || !pp->IsAlive() )
				continue;
			if ( pp->GetTeamNumber() != myTeam )
				continue;
			if ( pp->GetClassSlot() != CLASS_SNIPER )
				continue;
			if ( ( pp->GetAbsOrigin() - spotPos ).LengthSqr() < ( 600.0f * 600.0f ) )
			{
				score -= 4000.0f;
				break;
			}
		}

		// Per-bot deterministic noise so equal candidates differ between bots.
		score += me->TransientlyConsistentRandomValue( 30.0f, 7 ) * 250.0f;

		// SNIPER FIX 8 (cont.) — file into the strictest bucket this spot
		// qualifies for. Selection below prefers whole buckets over raw score,
		// so an interior spot can never beat a real outdoor overlook by being
		// closer to our cap.
		if ( hasLOS && isOutdoor )
		{
			if ( score > bestFwdOutdoorScore ) { bestFwdOutdoorScore = score; bestFwdOutdoorSpot = spot; }
		}
		if ( hasLOS )
		{
			if ( score > bestFwdScore ) { bestFwdScore = score; bestFwdSpot = spot; }
		}
		if ( score > bestScore )
		{
			bestScore = score;
			bestSpot = spot;
		}
	}

	// Strictest satisfiable bucket wins.
	if ( bestFwdOutdoorSpot )
		bestSpot = bestFwdOutdoorSpot;
	else if ( bestFwdSpot )
		bestSpot = bestFwdSpot;

	if ( bestSpot )
	{
		m_homePosition = bestSpot->GetCenter();
		m_isHomeValid = true;
		return true;
	}

	// 2. Auto-discover elevated nav areas near our cap. Most FF maps don't
	// hand-tag FF_NAV_SNIPER_SPOT (the tagger doesn't auto-stamp them), so
	// without this fallback the sniper would sit in spawn forever on every
	// un-edited map. Walks every nav area, prefers ones near our cap with
	// elevation advantage and LOS to enemy spawn exits.
	{
		CFFNavArea *autoBest = NULL;
		float autoBestScore = -FLT_MAX;
		for ( int i = 0; i < areas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( areas[ i ] );
			const unsigned int areaAttrs = area->GetAttributesFF();
			if ( areaAttrs & FF_NAV_SPAWN_ROOM_ANY )
				continue;
			if ( areaAttrs & ( FF_NAV_WATER | FF_NAV_UNDERWATER ) )
				continue;	// can't snipe from water — restricted FOV, slow turn
			if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
				continue;

			const Vector spotPos = area->GetCenter();
			const float horizDistSq =
				( spotPos.x - anchorPos.x ) * ( spotPos.x - anchorPos.x ) +
				( spotPos.y - anchorPos.y ) * ( spotPos.y - anchorPos.y );
			if ( horizDistSq > ( 2000.0f * 2000.0f ) )
				continue;	// too far from our cap to actually defend it

			// SNIPER FIX 9 — ELEVATION IS RELATIVE TO THE TERRAIN, NOT THE CAP.
			//
			// This used to require spotPos.z >= anchorPos.z + 32, i.e. "at
			// least 32u above our own capture point". That silently deletes
			// the correct answer on any map where the good sniping position
			// sits at roughly the SAME height as the cap — which is exactly
			// the 2fort case, where the sniping area is beside the capture
			// point. The genuine overlook scored zDelta ~= 0 and was rejected
			// outright, leaving the picker to choose among whatever happened
			// to be higher than the cap: interior catwalks near the spawn.
			//
			// "High ground" is a property of an area versus its SURROUNDINGS,
			// which is what CFFBotAutoTagger's median-of-neighbourhood
			// FF_NAV_HIGH_GROUND already measures. Accept either signal.
			const float zDelta = spotPos.z - anchorPos.z;
			const bool isLocalHighGround = ( areaAttrs & FF_NAV_HIGH_GROUND ) != 0;
			if ( !isLocalHighGround && zDelta < 32.0f )
				continue;

			// Need LOS to ≥1 ingress target. No LOS = no targets to shoot.
			const Vector eyeFrom = spotPos + Vector( 0, 0, 64.0f );
			const int losCount = CountVisibleIngressTargets( eyeFrom, targetEyes );
			if ( losCount == 0 )
				continue;

			float score = -sqrtf( horizDistSq );	// closer-to-cap = higher score
			if ( zDelta > 0.0f )
				score += zDelta * 4.0f;				// strong elevation bonus
			if ( isLocalHighGround )
				score += 400.0f;					// high relative to surroundings
			score += ( losCount - 1 ) * 400.0f;		// reward covering more lanes

			// Prefer an open overlook over an interior room with a sightline.
			if ( AreaIsOutdoors( spotPos ) )
				score += 500.0f;

			// Mild pull toward the fight, offsetting the inward -distToCap term.
			const float enemyIncursion = EnemyIncursionDistance( area, myTeam );
			if ( enemyIncursion >= 0.0f )
				score -= enemyIncursion * 0.15f;

			// Outward-facing bias (see snipeSpots loop above).
			if ( haveOutward )
			{
				const float forwardness = ( spotPos - ourExitCentroid ).Dot( outwardDir );
				if ( forwardness < 0.0f )
					continue;	// don't auto-pick inside-the-base perches
				score += forwardness * 0.5f;
			}

			// Anti-cluster: spread snipers across spots.
			for ( int p = 1; p <= gpGlobals->maxClients; ++p )
			{
				CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( p ) );
				if ( !pp || pp == me || !pp->IsAlive() )
					continue;
				if ( pp->GetTeamNumber() != myTeam )
					continue;
				if ( pp->GetClassSlot() != CLASS_SNIPER )
					continue;
				if ( ( pp->GetAbsOrigin() - spotPos ).LengthSqr() < ( 600.0f * 600.0f ) )
				{
					score -= 4000.0f;
					break;
				}
			}

			score += me->TransientlyConsistentRandomValue( 30.0f, 7 ) * 250.0f;

			if ( score > autoBestScore )
			{
				autoBestScore = score;
				autoBest = area;
			}
		}
		if ( autoBest )
		{
			m_homePosition = autoBest->GetCenter();
			m_isHomeValid = true;
			return true;
		}
	}

	// 3. Fallback: any sniper spot (even without LOS to spawn exits and
	// outside the 3000u cap radius). Better than standing in spawn.
	if ( snipeSpots.Count() > 0 )
	{
		CFFNavArea *pick = snipeSpots[ RandomInt( 0, snipeSpots.Count() - 1 ) ];
		m_homePosition = pick->GetCenter();
		m_isHomeValid = true;
		return true;
	}

	// 3b. Plain FF_NAV_HIGH_GROUND fallback. The auto-discovery filter
	// requires LOS to an ingress point; on some maps (deep tunnels) no
	// area passes that gate. But "elevated, near our cap, not in water"
	// is still a way better sniper post than a cap-area floor pick.
	{
		CFFNavArea *highBest = NULL;
		float highBestScore = -FLT_MAX;
		for ( int i = 0; i < areas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( areas[ i ] );
			const unsigned int a = area->GetAttributesFF();
			if ( !( a & FF_NAV_HIGH_GROUND ) )
				continue;
			if ( a & ( FF_NAV_SPAWN_ROOM_ANY | FF_NAV_WATER | FF_NAV_UNDERWATER ) )
				continue;
			if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
				continue;
			const Vector spotPos = area->GetCenter();
			const float horizDistSq =
				( spotPos.x - anchorPos.x ) * ( spotPos.x - anchorPos.x ) +
				( spotPos.y - anchorPos.y ) * ( spotPos.y - anchorPos.y );
			if ( horizDistSq > ( 3000.0f * 3000.0f ) )
				continue;
			const float zDelta = spotPos.z - anchorPos.z;
			float score = -sqrtf( horizDistSq ) + zDelta * 3.0f;

			// Same outward-facing bias as the primary loops — without
			// this the "any high-ground" fallback would still pick
			// inside-the-base balconies on flag-room maps.
			if ( haveOutward )
			{
				const float forwardness = ( spotPos - ourExitCentroid ).Dot( outwardDir );
				if ( forwardness < 0.0f )
					continue;
				score += forwardness * 0.5f;
			}

			score += me->TransientlyConsistentRandomValue( 30.0f, 13 ) * 200.0f;
			if ( score > highBestScore )
			{
				highBestScore = score;
				highBest = area;
			}
		}
		if ( highBest )
		{
			m_homePosition = highBest->GetCenter();
			m_isHomeValid = true;
			return true;
		}
	}

	// 4. Last resort: cap NAV AREA, not the cap entity origin. The cap
	// entity is often a trigger volume positioned slightly above the floor
	// or inside a wall, so its origin can fail Path::Compute. Use the nav
	// areas tagged FF_NAV_CAP_<myTeam> by the tagger — those are guaranteed
	// to be on real ground a bot can stand on.
	if ( ffMesh )
	{
		const CUtlVector< CFFNavArea * > *capAreas = ffMesh->GetCapAreas( myTeam );
		if ( capAreas && capAreas->Count() > 0 )
		{
			// Prefer dry cap area; only fall back to wet if every cap is wet.
			CFFNavArea *dry = NULL;
			for ( int i = 0; i < capAreas->Count(); ++i )
			{
				if ( !( *capAreas )[ i ]->HasAttributeFF( FF_NAV_WATER | FF_NAV_UNDERWATER ) )
				{
					dry = ( *capAreas )[ i ];
					break;
				}
			}
			CFFNavArea *pick = dry ? dry :
				( *capAreas )[ RandomInt( 0, capAreas->Count() - 1 ) ];
			m_homePosition = pick->GetCenter();
			m_isHomeValid = true;
			return true;
		}

		// 5. Truly last resort: spawn-room exit area for our team. Always
		// pathable because we just walked out of there. Doesn't snipe
		// anything, but at least the bot is somewhere defensible instead
		// of frozen in spawn.
		const CUtlVector< CFFNavArea * > *exits = ffMesh->GetSpawnRoomExitAreas( myTeam );
		if ( exits && exits->Count() > 0 )
		{
			CFFNavArea *pick = ( *exits )[ RandomInt( 0, exits->Count() - 1 ) ];
			m_homePosition = pick->GetCenter();
			m_isHomeValid = true;
			return true;
		}
	}

	(void)anyHasLOS;	// silenced: kept for future logging if needed.
	return false;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSniperLurk::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_boredTimer.Start( RandomFloat( 0.9f, 1.1f ) * ff_bot_sniper_patience_duration.GetFloat() );
	m_failCount = 0;

	FindNewHome( me );

	// Switch to sniper rifle so the per-tick fire driver charges it.
	CBaseCombatWeapon *rifle = me->Weapon_OwnsThisType( "ff_weapon_sniperrifle" );
	if ( rifle )
		me->Weapon_Switch( rifle );

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSniperLurk::Update( CFFBot *me, float interval )
{
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat() : NULL;

	// If a threat is in our face, melee.
	if ( threat && threat->GetEntity() && threat->IsVisibleInFOVNow() &&
		 me->IsRangeLessThan( threat->GetLastKnownPosition(), ff_bot_sniper_melee_range.GetFloat() ) )
	{
		const float giveUp = 1.25f * ff_bot_sniper_melee_range.GetFloat();
		return SuspendFor( new CFFBotMeleeAttack( giveUp ), "Threat in melee range" );
	}

	// Reset patience whenever a threat is visible — don't relocate while
	// engaged.
	if ( threat && threat->IsVisibleRecently() )
	{
		m_boredTimer.Reset();
	}

	if ( !m_isHomeValid )
	{
		// Re-pick periodically if the first pick failed.
		if ( m_boredTimer.IsElapsed() )
		{
			FindNewHome( me );
			m_boredTimer.Start( 1.0f );
		}
		return Continue();
	}

	const float distSqToHome = ( me->GetAbsOrigin() - m_homePosition ).LengthSqr();
	m_isAtHome = ( distSqToHome <= FFBOT_SNIPER_HOME_TOLERANCE * FFBOT_SNIPER_HOME_TOLERANCE );

	if ( m_isAtHome )
	{
		// In position — let main_action's per-tick aim+fire run the rifle.
		// The bot's m_sniperFireState handles charge-then-release.
		m_path.Invalidate();

		// SNIPER FIX 2 (cont.) — boredom alone is NOT a reason to leave.
		//
		// The old code relocated unconditionally every time the patience timer
		// elapsed. Since FindNewHome mixes in a random term that rerolls every
		// 30s, that produced a different destination almost every time, and the
		// sniper spent the round walking between perches instead of watching
		// one. A quiet lane is not a bad lane.
		//
		// Now we only give up the spot when it has genuinely gone dead — i.e.
		// it no longer has line of sight to ANY ingress target, which happens
		// when a door closes, a gate drops, or the objective moves. Otherwise
		// we reset patience and keep holding the angle.
		if ( m_boredTimer.IsElapsed() )
		{
			// FORWARD targets only. Using the combined list here would let a
			// spot that merely overlooks our own objective count as "still
			// live", so a bot that ended up somewhere inside our own base
			// would happily sit there for the rest of the round. Holding an
			// angle on the enemy approach is the job; watching our own flag
			// room is not a reason to stay put.
			CUtlVector< Vector > forwardEyes;
			CollectForwardTargetEyes( me->GetTeamNumber(), forwardEyes );

			const int stillCovering =
				CountVisibleIngressTargets( me->EyePosition(), forwardEyes );

			if ( stillCovering > 0 )
			{
				// Spot is still live. Stay put and keep watching.
				m_boredTimer.Start( RandomFloat( 0.9f, 1.1f ) *
					ff_bot_sniper_patience_duration.GetFloat() );
			}
			else
			{
				++m_failCount;
				if ( FindNewHome( me ) )
				{
					m_boredTimer.Start( RandomFloat( 0.9f, 1.1f ) * ff_bot_sniper_patience_duration.GetFloat() );
				}
				else
				{
					m_boredTimer.Start( 1.0f );
				}
			}
		}

		return Continue();
	}

	// Path to home.
	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		// FIX 7 — repath hysteresis: don't rebuild a path we're already
		// walking successfully; volatile cost terms would otherwise flip
		// the route between near-equal lanes every couple of seconds.
		if ( FFBotHelpers::ShouldRecomputePath( me, m_path, m_homePosition ) )
		{
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			if ( !m_path.Compute( me, m_homePosition, cost ) )
			{
				// Path failed — pick another home.
				m_isHomeValid = false;
				return Continue();
			}
		}
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
ActionResult< CFFBot > CFFBotSniperLurk::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	m_path.Invalidate();
	m_repathTimer.Invalidate();
	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSniperLurk::OnStuck( CFFBot *me )
{
	// Stuck — pick a new home next tick.
	m_isHomeValid = false;
	return TryContinue();
}
