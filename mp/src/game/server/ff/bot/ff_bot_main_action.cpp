//========= Fortress Forever Bot =============================================//
//
// CFFBotMainAction — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_main_action.h"
#include "ff_bot_dead.h"
#include "ff_bot_wander.h"
#include "ff_bot_attack.h"
#include "ff_bot_ctf.h"
#include "ff_bot_class.h"
#include "ff_bot_medic.h"
#include "ff_bot_helpers.h"
#include "ff_bot_weapon.h"
#include "ff_bot_intel.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_player.h"
#include "NextBotBodyInterface.h"
#include "ff_weapon_base.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Buffer added to the spawn delay before pressing fire — gives the
// LIFE_DEAD → LIFE_RESPAWNABLE transition (which requires "all buttons
// released" for one death-think tick) a chance to fire first.
#define FFBOT_RESPAWN_BUTTON_DELAY_BUFFER	0.5f

// Distance under which we want to stop and fight rather than chase.
#define FFBOT_ATTACK_HOLD_RANGE			600.0f

// How long we have to be stuck before we start mashing JUMP — avoids
// jumping at every door we pause in front of for a moment.
#define FFBOT_STUCK_JUMP_THRESHOLD		0.75f

// Sniper rifle: how long to keep IN_ATTACK held before letting it release
// to fire. FF caps charge at FF_SNIPER_MAXCHARGE (~3s) but full damage
// arrives well before then; 1.5s gives a strong hit without keeping the
// bot pinned in place forever.
#define FFBOT_SNIPER_CHARGE_DURATION	1.5f
// Recovery time after firing before next charge cycle starts.
#define FFBOT_SNIPER_COOLDOWN			0.5f


//-----------------------------------------------------------------------------
// Projectile speed (units/sec) for FF weapons that fire projectiles. Used by
// UpdateLookingAroundForEnemies to compute target lead. 0 means hitscan or
// melee — no lead needed.
//-----------------------------------------------------------------------------
static float ProjectileSpeedForWeapon( FFWeaponID id )
{
	switch ( id )
	{
	// Hitscan / melee.
	case FF_WEAPON_SNIPERRIFLE:
	case FF_WEAPON_TOMMYGUN:
	case FF_WEAPON_AUTORIFLE:
	case FF_WEAPON_SHOTGUN:
	case FF_WEAPON_SUPERSHOTGUN:
	case FF_WEAPON_ASSAULTCANNON:
	case FF_WEAPON_KNIFE:
	case FF_WEAPON_MEDKIT:
	case FF_WEAPON_SPANNER:
	case FF_WEAPON_UMBRELLA:
	case FF_WEAPON_CROWBAR:
	case FF_WEAPON_TRANQUILISER:
		return 0.0f;

	// Nailgun / supernailgun: nails fly at ~1500 ups.
	case FF_WEAPON_NAILGUN:
	case FF_WEAPON_SUPERNAILGUN:
		return 1500.0f;

	// Soldier rocket — ~1100 ups in TFC, FF likely similar.
	case FF_WEAPON_RPG:
		return 1100.0f;

	// Demoman GL/PL pipes — slow lobbed grenades, ~600 ups.
	case FF_WEAPON_GRENADELAUNCHER:
	case FF_WEAPON_PIPELAUNCHER:
		return 600.0f;

	// IC — slow incendiary, ~500 ups.
	case FF_WEAPON_IC:
		return 500.0f;

	// Flamethrower particles — short range, ~600 ups.
	case FF_WEAPON_FLAMETHROWER:
		return 600.0f;

	// Self-prop weapons / unknown — no projectile lead.
	default:
		return 0.0f;
	}
}


//-----------------------------------------------------------------------------
// CFFBotCtfObjective owns the path on every map — it picks flag/cap goals on
// CTF maps and falls through to wander-style random nav-area picking when no
// CTF goal is applicable. This means we don't need a separate code path for
// non-CTF maps.
//-----------------------------------------------------------------------------
Action< CFFBot > *CFFBotMainAction::InitialContainedAction( CFFBot *me )
{
	return new CFFBotCtfObjective;
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMainAction::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	return Continue();
}

//-----------------------------------------------------------------------------
// Press fire while LIFE_RESPAWNABLE so PlayerDeathThink calls respawn().
// PressFireButton(0.1f) auto-releases — important so we don't keep IN_ATTACK
// pinned while LIFE_DEAD still requires "all buttons released" before
// transitioning.
//-----------------------------------------------------------------------------
void CFFBotMainAction::HandleRespawnInput( CFFBot *me )
{
	const float allowedAt = me->GetDeathTime() + me->m_flNextSpawnDelay + FFBOT_RESPAWN_BUTTON_DELAY_BUFFER;
	if ( gpGlobals->curtime >= allowedAt )
	{
		me->PressFireButton( 0.1f );
	}
}

//-----------------------------------------------------------------------------
// Per-tick aim driver. Must run every tick to override
// PlayerLocomotion::FaceTowards (which calls AimHeadTowards with BORING
// priority while pathing). Only a CRITICAL/IMPORTANT refresh from us holds
// the look-at on threats.
//-----------------------------------------------------------------------------
// Compute a "look toward enemy approach" point for defenders without a
// current threat. Heuristic: centroid of enemy spawn-room areas, projected to
// a horizontal direction from our position. Failing that, our team's own
// spawn entry direction. Returns false if no useful direction.
static bool ComputePreAimChokePoint( CFFBot *me, Vector *out )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh )
		return false;
	const int myTeam = me->GetTeamNumber();

	Vector centroid = vec3_origin;
	int count = 0;
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == myTeam )
			continue;
		const CUtlVector< CFFNavArea * > *spawnAreas = mesh->GetSpawnRoomAreas( t );
		if ( !spawnAreas )
			continue;
		for ( int i = 0; i < spawnAreas->Count(); ++i )
		{
			centroid += ( *spawnAreas )[ i ]->GetCenter();
			++count;
		}
	}
	if ( count == 0 )
		return false;
	centroid *= ( 1.0f / (float)count );
	// Bring the look point to roughly eye-height of the bot, projected
	// outward — we want to look in the *direction* of enemy spawn, not at
	// the floor under it.
	Vector dir = centroid - me->GetAbsOrigin();
	dir.z = 0.0f;
	if ( dir.NormalizeInPlace() < 1.0f )
		return false;
	*out = me->EyePosition() + dir * 600.0f;
	return true;
}


// Spawn-exit override. CFFBot::Spawn already snapped eye angles toward an
// exit (BFS through the nav graph). This per-tick override KEEPS the body's
// m_lookAtPos seeded toward that direction so PlayerBody::Upkeep doesn't
// slew us back to whatever stale value it was tracking.
//
// IMPORTANT: termination must NOT depend on FF_NAV_SPAWN_ANY tag presence.
// The tagger over-tags on some maps (600u flood-fill consumes corridors as
// "spawn"), and on those maps the bot might still be inside a spawn-tagged
// area when 1.5s have elapsed; conversely the tagger sometimes UNDER-tags
// (the bot's actual nav area isn't tagged spawn at all). Using tags as the
// termination condition double-counts both failure modes. Use plain time
// + travel distance instead.
static bool TryExitSpawnOverride( CFFBot *me, IBody *body )
{
	if ( !me->m_spawnExitForceTimer.HasStarted() ||
		 me->m_spawnExitForceTimer.IsElapsed() )
		return false;
	if ( me->m_spawnExitDir.IsZero() )
		return false;

	// Terminate early once we've travelled a meaningful distance from
	// where we spawned — we're clearly out of the initial spawn pocket and
	// the path follower can take over. Do not terminate on velocity alone:
	// a bot can accelerate into the wrong wall before the body has held the
	// doorway aim long enough to correct the inherited spawn angles.
	Vector delta = me->GetAbsOrigin() - me->m_spawnExitStartPos;
	delta.z = 0.0f;
	if ( delta.LengthSqr() > ( 320.0f * 320.0f ) )
	{
		me->m_spawnExitForceTimer.Invalidate();
		return false;
	}

	// Keep aim seeded toward the exit so Body::Upkeep doesn't drift back
	// to a stale m_lookAtPos.
	Vector lookAt = me->EyePosition() + me->m_spawnExitDir * 200.0f;
	body->AimHeadTowards( lookAt, IBody::IMPORTANT, 0.3f, NULL, "Exiting spawn" );
	return true;
}


void CFFBotMainAction::UpdateLookingAroundForEnemies( CFFBot *me )
{
	IBody *body = me->GetBodyInterface();
	if ( !body )
		return;

	IVision *vision = me->GetVisionInterface();
	if ( !vision )
		return;

	// Spawn-exit override runs first. If we're in spawn, point at the door
	// and walk; threat detection still gets a chance to override below at
	// CRITICAL priority if an enemy is in our face.
	const bool inSpawn = TryExitSpawnOverride( me, body );

	const CKnownEntity *known = vision->GetPrimaryKnownThreat( false );

	// Stuck-scan override: when the stuck recovery has flagged "look around"
	// mode, sweep the head so the bot visibly searches for an exit instead
	// of staring at the wall it's wedged on. Higher priority than threat
	// look so this isn't fighting per-tick aim.
	if ( me->m_lookAroundUntil > gpGlobals->curtime )
	{
		const float remaining = me->m_lookAroundUntil - gpGlobals->curtime;
		// 90 deg/sec rotation around current pos, full 360 over ~2.5s.
		const float yawDeg = remaining * 90.0f * 4.0f;
		const float rad = DEG2RAD( yawDeg );
		Vector lookDir( cosf( rad ), sinf( rad ), 0.0f );
		Vector lookAt = me->EyePosition() + lookDir * 200.0f;
		body->AimHeadTowards( lookAt, IBody::IMPORTANT, 0.3f, NULL, "Looking for an exit (stuck)" );
		// Don't return — let normal threat aim still happen if a threat
		// pops up mid-scan, but our IMPORTANT call sticks if no threat.
	}

	// No current threat — fall back to either our cached LKP (recently lost
	// sight) or a pre-aim choke point if we're a defender. But if we're
	// still inside spawn, the exit-spawn override above already pinned aim
	// at the door — don't let team alerts / LKP / choke pre-aim drag the
	// view back into the wall.
	if ( inSpawn )
		return;

	if ( !known || !known->GetEntity() )
	{
		// Prefer a fresh team alert (a teammate spotted an enemy aiming at
		// them, or we heard a footstep). 8s window — beyond that the alert
		// is stale and noise.
		Vector alertPos;
		float  alertAge = 0.0f;
		if ( FFBotIntel::GetFreshTeamAlert( me->GetTeamNumber(), 8.0f, &alertPos, &alertAge ) )
		{
			body->AimHeadTowards( alertPos, IBody::IMPORTANT, 0.5f, NULL, "Reacting to team alert" );
			return;
		}

		// Personal LKP cache — bot keeps watching the spot it last saw an
		// enemy for ~8 seconds.
		if ( me->m_lastThreatTime > 0.0f &&
			 ( gpGlobals->curtime - me->m_lastThreatTime ) < 8.0f )
		{
			body->AimHeadTowards( me->m_lastThreatPos, IBody::IMPORTANT, 0.5f, NULL, "Watching last-known threat" );
			return;
		}

		// Defenders: pre-aim toward enemy approach. Only for classes whose
		// job is to watch chokes — running offense bots don't pre-aim, they
		// path forward and react.
		const int classSlot = me->GetClassSlot();
		if ( classSlot == CLASS_SNIPER || classSlot == CLASS_ENGINEER ||
			 classSlot == CLASS_HWGUY  || classSlot == CLASS_DEMOMAN )
		{
			// First preference: hottest nearby kill-zone. If players have
			// been dying in an area within ~1500u, that's a fresher
			// signal than "enemies come from over there in general".
			const Vector myPos = me->GetAbsOrigin();
			CFFNavArea *hotArea = NULL;
			float hotScore = 0.1f;	// minimum threshold to count as hot
			for ( int i = 0; i < TheNavAreas.Count(); ++i )
			{
				CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
				if ( !area )
					continue;
				const float distSq = ( area->GetCenter() - myPos ).LengthSqr();
				if ( distSq > ( 1500.0f * 1500.0f ) )
					continue;
				const float intensity = area->GetCombatIntensity();
				if ( intensity > hotScore )
				{
					hotScore = intensity;
					hotArea = area;
				}
			}
			if ( hotArea )
			{
				body->AimHeadTowards( hotArea->GetCenter() + Vector( 0, 0, 32 ),
					IBody::BORING, 0.5f, NULL, "Watching combat zone" );
				return;
			}

			Vector chokeAim;
			if ( ComputePreAimChokePoint( me, &chokeAim ) )
			{
				body->AimHeadTowards( chokeAim, IBody::BORING, 0.5f, NULL, "Pre-aiming choke" );
				return;
			}
		}

		// Velocity-based view tracking: when we're moving but have no
		// threat to aim at, face the direction we're actually moving.
		// Without this, PlayerLocomotion::Approach can press IN_BACK
		// (because view is opposite goal direction) and the bot moves
		// correctly in world space but visibly walks BACKWARDS — the
		// model faces one way while sliding the other way.
		//
		// Threshold: 120 ups (bots run at ~225 ups, so anything > 120
		// is "definitely moving"). We use horizontal velocity so jumps
		// don't fool us.
		Vector myVel = me->GetAbsVelocity();
		myVel.z = 0.0f;
		if ( myVel.LengthSqr() > ( 120.0f * 120.0f ) )
		{
			Vector velDir = myVel;
			velDir.NormalizeInPlace();
			// Aim 200u in the direction we're moving, at eye height.
			Vector lookAt = me->EyePosition() + velDir * 200.0f;
			body->AimHeadTowards( lookAt, IBody::IMPORTANT, 0.3f, NULL,
				"Facing movement direction" );
			return;
		}

		// Universal fallback: look toward the nearest enemy invasion area —
		// per-area pre-computed list of nav areas where enemies are
		// approaching from (CFFNavArea::ComputeInvasionAreaVectors). Mirrors
		// TFBot's UpdateLookingAroundForIncomingPlayers and, crucially,
		// keeps the body aimed roughly along the path direction during
		// travel. Without this default, PlayerBody::Upkeep slews aim
		// toward stale m_lookAtPos values and PlayerLocomotion::Approach
		// reads the wrong view direction.
		if ( TheNavMesh && TheNavMesh->IsLoaded() )
		{
			CNavArea *navArea = TheNavMesh->GetNearestNavArea(
				me->GetAbsOrigin(), false, 256.0f, false, true, TEAM_ANY );
			if ( navArea )
			{
				CFFNavArea *here = static_cast< CFFNavArea * >( navArea );
				const CUtlVector< CFFNavArea * > &invasion =
					here->GetEnemyInvasionAreaVector( me->GetTeamNumber() );
				if ( invasion.Count() > 0 )
				{
					// Pick the closest invasion area to the bot — that's
					// the most likely arrival point. Picks deterministic
					// to avoid head-spinning between equidistant options.
					CFFNavArea *target = invasion[ 0 ];
					float bestDistSq = FLT_MAX;
					const Vector myPos = me->GetAbsOrigin();
					for ( int i = 0; i < invasion.Count(); ++i )
					{
						const float dSq = ( invasion[ i ]->GetCenter() - myPos ).LengthSqr();
						if ( dSq < bestDistSq )
						{
							bestDistSq = dSq;
							target = invasion[ i ];
						}
					}
					Vector lookAt = target->GetCenter();
					lookAt.z += 32.0f;	// roughly chest height
					body->AimHeadTowards( lookAt, IBody::BORING, 0.5f,
						NULL, "Looking toward enemy invasion" );
				}
			}
		}
		return;
	}

	if ( known->IsVisibleInFOVNow() && known->IsVisibleRecently() )
	{
		CBaseEntity *threatEnt = known->GetEntity();
		Vector aimPos = threatEnt->WorldSpaceCenter();

		// Update our personal LKP cache for "where to keep looking after
		// they duck behind cover".
		me->m_lastThreatPos = aimPos;
		me->m_lastThreatTime = gpGlobals->curtime;

		// If the threat is aiming back at us, broadcast their position to
		// the team so other bots can converge / take the shot.
		CFFPlayer *threatPlayer = ToFFPlayer( threatEnt );
		if ( threatPlayer )
		{
			Vector theirForward;
			AngleVectors( threatPlayer->EyeAngles(), &theirForward );
			Vector toMe = me->WorldSpaceCenter() - threatPlayer->EyePosition();
			toMe.NormalizeInPlace();
			if ( DotProduct( theirForward, toMe ) > 0.85f )
			{
				FFBotIntel::PushTeamAlert( me->GetTeamNumber(), threatPlayer->GetAbsOrigin() );
			}
		}

		// Compute projectile-aware lead: lead time = distance / projectile-
		// speed. Constant-time leads underlead at long range and overlead at
		// close range; this scales correctly with engagement distance.
		CFFWeaponBase *weapon = me->GetActiveFFWeapon();
		const float projSpeed = weapon ? ProjectileSpeedForWeapon( weapon->GetWeaponID() ) : 0.0f;
		if ( projSpeed > 0.0f )
		{
			const Vector eyePos = me->EyePosition();
			const float distance = ( aimPos - eyePos ).Length();
			const float timeOfFlight = distance / projSpeed;
			// Cap the predicted lead so a far-off, fast-strafing target
			// doesn't pull our aim off the screen.
			const float clampedToF = MIN( timeOfFlight, 2.0f );
			const Vector targetVel = threatEnt->GetSmoothedVelocity();
			aimPos += targetVel * clampedToF;
		}

		// Concussed: scramble the aim point so the bot thrashes their head
		// like a real player whose view-jerk is fighting them. Effect is
		// largest at long range (where small angular error becomes big
		// positional miss).
		if ( me->IsConcussed() )
		{
			const Vector eyePos = me->EyePosition();
			const float distance = ( aimPos - eyePos ).Length();
			const float wobble = MIN( 200.0f, distance * 0.15f );
			aimPos.x += RandomFloat( -wobble, wobble );
			aimPos.y += RandomFloat( -wobble, wobble );
			aimPos.z += RandomFloat( -wobble * 0.5f, wobble * 0.5f );
		}
		// Tranqed: smaller aim drift — like trying to track a target while
		// drugged. Less than concussion, still noticeable.
		else if ( me->IsTranqed() )
		{
			aimPos.x += RandomFloat( -20.0f, 20.0f );
			aimPos.y += RandomFloat( -20.0f, 20.0f );
		}

		body->AimHeadTowards( aimPos, IBody::CRITICAL, 1.0f, NULL, "Aiming at a visible threat" );
		return;
	}

	// Threat known but not currently visible — aim at last-known position
	// from IVision and refresh our own cache so the post-vision-forget
	// fallback (above) still has it after IVision drops the entity.
	Vector lastKnownAim = known->GetLastKnownPosition();
	lastKnownAim.z = known->GetEntity()->WorldSpaceCenter().z;
	me->m_lastThreatPos = lastKnownAim;
	me->m_lastThreatTime = gpGlobals->curtime;
	body->AimHeadTowards( lastKnownAim, IBody::IMPORTANT, 1.0f, NULL, "Hunting last-known threat position" );
}

//-----------------------------------------------------------------------------
// Sniper rifle is unique: fires on attack-RELEASE after a charge period.
// Default per-tick fire-press never releases. Run a dedicated state machine
// that always advances regardless of threat visibility — once we commit to a
// charge, we commit to a shot, even if the threat ducks out of sight mid-
// cycle.
//
// Returns true if we owned the firing decision (caller should not fall
// through to default press).
//-----------------------------------------------------------------------------
static bool RunSniperFireStateMachine( CFFBot *me, bool threatVisibleAndAimed )
{
	switch ( me->m_sniperFireState )
	{
	case CFFBot::SNIPER_FIRE_CHARGING:
	{
		const float held = gpGlobals->curtime - me->m_sniperFireStartTime;
		if ( held < FFBOT_SNIPER_CHARGE_DURATION )
		{
			// Keep IN_ATTACK pressed by extending the press timer.
			me->PressFireButton( 0.2f );
		}
		else
		{
			// Charge complete — STOP pressing so IN_ATTACK releases this
			// tick and the rifle reads the release event. Move to cooldown.
			me->m_sniperFireState = CFFBot::SNIPER_FIRE_COOLDOWN;
			me->m_sniperFireStartTime = gpGlobals->curtime;
		}
		return true;
	}

	case CFFBot::SNIPER_FIRE_COOLDOWN:
		if ( gpGlobals->curtime - me->m_sniperFireStartTime >= FFBOT_SNIPER_COOLDOWN )
			me->m_sniperFireState = CFFBot::SNIPER_FIRE_IDLE;
		return true;

	case CFFBot::SNIPER_FIRE_IDLE:
		if ( threatVisibleAndAimed )
		{
			me->m_sniperFireState = CFFBot::SNIPER_FIRE_CHARGING;
			me->m_sniperFireStartTime = gpGlobals->curtime;
			me->PressFireButton( 0.2f );
		}
		return true;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Per-tick fire driver. We don't use IBody::IsHeadAimingOnTarget for the
// fire decision — its dot > 0.98 tolerance is ~11° wide, which means a
// 1000u-distant target can be missed by 200u. Instead we compute the actual
// angle between our view direction and the direction to the target's
// center, with a range-tightened tolerance.
//-----------------------------------------------------------------------------
void CFFBotMainAction::FireWeaponAtEnemy( CFFBot *me )
{
	if ( !me->IsAlive() )
		return;

	// Concussed: enemy conc grenade has scrambled our view. Don't try to
	// fire — we'd miss wildly. Sniper state machine still runs (committed
	// charge cycles must complete), but everyone else holds fire.
	if ( me->IsConcussed() )
	{
		// Reset sniper firing state if we were charging — release without aim.
		if ( me->m_sniperFireState == CFFBot::SNIPER_FIRE_CHARGING )
		{
			me->m_sniperFireState = CFFBot::SNIPER_FIRE_COOLDOWN;
			me->m_sniperFireStartTime = gpGlobals->curtime;
		}
		return;
	}

	// Tranquilized: spy tranq dart has slowed our reactions. Engine handles
	// movement slowdown; we throttle effective fire rate to ~half by
	// skipping fire on alternating ~250ms windows.
	if ( me->IsTranqed() )
	{
		const int bucket = (int)( gpGlobals->curtime * 4.0f );
		if ( ( bucket & 1 ) != 0 )
			return;
	}

	CFFWeaponBase *weapon = me->GetActiveFFWeapon();
	const FFWeaponID weaponID = weapon ? weapon->GetWeaponID() : FF_WEAPON_NONE;
	const bool isChargedWeapon = ( weaponID == FF_WEAPON_SNIPERRIFLE );

	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;

	bool canFireNow = false;
	if ( threat && threat->GetEntity() && threat->IsVisibleRecently() && threat->IsVisibleInFOVNow() )
	{
		CBaseEntity *threatEnt = threat->GetEntity();

		const Vector eyePos = me->EyePosition();
		Vector toThreat = threatEnt->WorldSpaceCenter() - eyePos;
		const float threatRange = toThreat.NormalizeInPlace();

		Vector viewForward;
		AngleVectors( me->EyeAngles(), &viewForward );
		const float dot = DotProduct( viewForward, toThreat );

		float minDot;
		if ( threatRange < 256.0f )
			minDot = 0.95f;		// ~18° at point-blank — still trivially a hit
		else if ( threatRange < 1024.0f )
			minDot = 0.99f;		// ~8° at mid range
		else
			minDot = 0.998f;	// ~3.5° at long range

		if ( dot >= minDot )
		{
			// LOS check: don't fire through teammates / world.
			trace_t tr;
			UTIL_TraceLine( eyePos, threatEnt->WorldSpaceCenter(), MASK_SHOT, me, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 1.0f || tr.m_pEnt == threatEnt )
				canFireNow = true;
		}
	}

	if ( isChargedWeapon )
	{
		// Charge weapons must always run their state machine (commit-to-fire
		// is one-way — the rifle is in m_bInFire as soon as we start pressing).
		RunSniperFireStateMachine( me, canFireNow );
		return;
	}

	if ( !canFireNow )
		return;

	// Default weapons: continuous press while target is in sights. The 0.15s
	// timer is refreshed each tick so the button stays held across frames.
	me->PressFireButton( 0.15f );
}


//-----------------------------------------------------------------------------
// Weapon selection driver. At each tick, decide whether to switch to a
// better weapon for the current engagement. When no threat is visible, ask
// for the class's main / long-range weapon.
//-----------------------------------------------------------------------------
static void HandleWeaponSelection( CFFBot *me )
{
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;

	float threatRange = -1.0f;	// negative = "no threat"
	if ( threat && threat->GetEntity() && !threat->IsObsolete() && threat->IsVisibleRecently() )
	{
		threatRange = ( threat->GetEntity()->WorldSpaceCenter() - me->EyePosition() ).Length();
	}

	FFBotWeapon::TrySwitchToPreferred( me, threatRange );
}


//-----------------------------------------------------------------------------
// Combat strafing. While a threat is visible and within engagement range,
// alternate left/right side-strafes so the bot is harder to hit while
// shooting. Layered on top of the path follower — when path is pushing
// forward toward an objective, this adds lateral juke; when path has
// arrived (no movement), bot strafes in place.
//
// Exclude classes that need to stand still:
//   - Sniper: charge cycle requires stationary stance and precise aim.
//   - Engineer: building / repairing buildables requires staying within 128u
//     of build origin.
//   - HWGuy: assault cannon spinup is movement-sensitive.
//   - Civilian: VIP runs in straight lines, doesn't fight.
//-----------------------------------------------------------------------------
static void HandleCombatStrafe( CFFBot *me )
{
	const int classSlot = me->GetClassSlot();
	if ( classSlot == CLASS_SNIPER || classSlot == CLASS_ENGINEER ||
		 classSlot == CLASS_HWGUY  || classSlot == CLASS_CIVILIAN )
		return;

	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	if ( !threat || !threat->GetEntity() )
		return;
	if ( !threat->IsVisibleRecently() || !threat->IsVisibleInFOVNow() )
		return;

	const float threatRange = ( threat->GetEntity()->WorldSpaceCenter() - me->EyePosition() ).Length();
	if ( threatRange > 1500.0f )
		return;	// at long range, strafing makes us miss

	// Alternate strafe direction every ~0.6s based on a time bucket. The bot
	// keyboard helpers self-throttle (button stays pressed until the timer
	// expires) so a 0.25s press here ensures continuous lateral input.
	const int bucket = (int)( gpGlobals->curtime * 1.6f );
	if ( ( bucket & 1 ) == 0 )
		me->PressLeftButton( 0.25f );
	else
		me->PressRightButton( 0.25f );
}

//-----------------------------------------------------------------------------
// Per-tick stuck mashing. PathFollower's OnStuck fires only once per stuck
// episode, but FF spawn-room exits and func_button doors can require
// continuous +use / +forward to cycle the door open or push past the trigger
// volume. Mash every tick while the locomotor reports stuck.
//-----------------------------------------------------------------------------
void CFFBotMainAction::HandleStuckState( CFFBot *me )
{
	ILocomotion *loco = me->GetLocomotionInterface();
	if ( !loco || !loco->IsStuck() )
		return;

	const float stuckDur = loco->GetStuckDuration();

	// Phase 1 (under 1.5s): mash USE + FORWARD via direct button presses
	// (bypasses locomotor.Approach overrides) — this is enough for trigger-
	// touch doors and func_button doors.
	me->PressUseButton( 0.2f );

	if ( stuckDur < 1.5f )
	{
		me->PressForwardButton( 0.2f );
		if ( stuckDur > FFBOT_STUCK_JUMP_THRESHOLD && loco->IsOnGround() )
			me->PressJumpButton( 0.3f );
		return;
	}

	// Phase 2+ (≥1.5s): more aggressive. Inhibit the path follower for a
	// short window so its locomotor.Approach calls don't keep re-pressing
	// IN_FORWARD against our backward / lateral pushes (which would just
	// cancel out into "stop"). 0.8s is long enough for our reverse to
	// produce horizontal motion, short enough that we re-engage the path
	// quickly when freed.
	me->m_pathInhibitTimer.Start( 0.8f );

	// Trigger a head scan so the bot visibly looks around — gives them a
	// chance to spot the exit (and cues the human watching that the bot
	// is searching, not bugged).
	if ( me->m_lookAroundUntil < gpGlobals->curtime )
		me->m_lookAroundUntil = gpGlobals->curtime + 2.5f;

	if ( stuckDur < 4.0f )
	{
		// Lateral escape — alternate left/right by ~1s so both sides get
		// tried. With path inhibited, the bot strafes free of the wedge.
		const int side = ( ( (int)stuckDur ) & 1 );
		if ( side == 0 )
			me->PressLeftButton( 0.4f );
		else
			me->PressRightButton( 0.4f );
		// Add a backward press so we bias out of the corner rather than
		// strafing parallel against it.
		me->PressBackwardButton( 0.3f );
		if ( loco->IsOnGround() )
			me->PressJumpButton( 0.2f );
	}
	else
	{
		// Severely stuck (≥4s) — back away hard, crouch (clear low
		// ledges), and tag the position so path cost penalizes routes
		// through here for the next 30s.
		me->PressBackwardButton( 0.6f );
		me->PressCrouchButton( 0.4f );
		if ( loco->IsOnGround() )
			me->PressJumpButton( 0.2f );

		// Record stuck position for the per-bot path-cost penalty.
		me->m_recentStuckPos = me->GetAbsOrigin();
		me->m_recentStuckExpireTime = gpGlobals->curtime + 30.0f;
	}
}

//-----------------------------------------------------------------------------
// HandleWallAvoidance — proactive obstacle detection independent of the path
// planner. Casts a fan of trace lines from the bot's chest in their current
// facing direction; if the forward direction is blocked but a side direction
// is open, snap eye angles to the open direction so PlayerLocomotion::Approach
// reads the corrected view and presses IN_FORWARD into the open path.
//
// Lets the bot literally "see" walls — including other players standing in
// their way — and steer around them instead of blindly running into a wedge
// based on stale view direction.
//
// Runs every tick. No-op when the bot is making forward progress (horizontal
// speed > threshold). When idle/blocked, scans 7 angular offsets and picks
// the longest unobstructed direction.
//-----------------------------------------------------------------------------
static void HandleWallAvoidance( CFFBot *me )
{
	if ( !me->IsAlive() )
		return;

	ILocomotion *loco = me->GetLocomotionInterface();
	if ( !loco || !loco->IsOnGround() )
		return;

	// Only intervene when the bot is actually trying to move but isn't
	// making progress. If they're moving freely, the path follower has
	// things under control.
	if ( !loco->IsAttemptingToMove() )
		return;
	Vector vel = me->GetAbsVelocity();
	vel.z = 0.0f;
	const float horizSpeedSq = vel.LengthSqr();
	if ( horizSpeedSq > ( 80.0f * 80.0f ) )
		return;

	// Trace from chest height (so we don't catch our own feet on stairs).
	const Vector start = me->GetAbsOrigin() + Vector( 0, 0, 36 );

	Vector forward;
	AngleVectors( me->EyeAngles(), &forward );
	forward.z = 0.0f;
	if ( forward.NormalizeInPlace() < 0.5f )
		return;

	const float traceDist = 80.0f;	// "is there a wall right in front of me?"

	// Forward whisker first — if it's clear, no avoidance needed.
	{
		trace_t tr;
		UTIL_TraceHull( start, start + forward * traceDist,
			Vector( -16, -16, -36 ), Vector( 16, 16, 0 ),
			MASK_PLAYERSOLID, me, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
		if ( tr.fraction >= 0.9f )
			return;	// open ahead, no wall
	}

	// Forward is blocked. Sweep angular offsets to find the most open
	// direction.
	static const float kOffsetDegrees[] = { -22.5f, 22.5f, -45.0f, 45.0f, -67.5f, 67.5f, -90.0f, 90.0f };
	const int kOffsetCount = sizeof( kOffsetDegrees ) / sizeof( kOffsetDegrees[ 0 ] );

	float bestFraction = 0.0f;
	float bestAngleOffset = 0.0f;
	bool foundOpen = false;

	for ( int i = 0; i < kOffsetCount; ++i )
	{
		const float rad = DEG2RAD( kOffsetDegrees[ i ] );
		const float c = cosf( rad );
		const float s = sinf( rad );
		Vector probe(
			forward.x * c - forward.y * s,
			forward.x * s + forward.y * c,
			0.0f );
		probe.NormalizeInPlace();

		trace_t tr;
		UTIL_TraceHull( start, start + probe * traceDist,
			Vector( -16, -16, -36 ), Vector( 16, 16, 0 ),
			MASK_PLAYERSOLID, me, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );

		// Prefer the smallest absolute angle change among directions that
		// are clear "enough". Once we find a sufficiently open direction,
		// we take it — closer to forward beats wider but slightly more
		// open. Without this preference the bot oscillates between left
		// and right when both are open.
		if ( tr.fraction > bestFraction )
		{
			bestFraction = tr.fraction;
			bestAngleOffset = kOffsetDegrees[ i ];
			if ( tr.fraction >= 0.9f )
			{
				foundOpen = true;
				break;
			}
		}
	}

	if ( !foundOpen )
	{
		// Nothing is clear within 80u — every whisker hits geometry. Bot
		// is wedged in a tight corner. Fall back to backing up; the
		// stuck-recovery system will lateral-escape from there.
		me->PressBackwardButton( 0.3f );
		return;
	}

	// Snap eye angles toward the open direction. PlayerLocomotion::Approach
	// reads EyeVectors next tick and will press IN_FORWARD aligned with
	// the actually-walkable direction instead of pressing INTO a wall.
	const float rad = DEG2RAD( bestAngleOffset );
	const float c = cosf( rad );
	const float s = sinf( rad );
	Vector openDir(
		forward.x * c - forward.y * s,
		forward.x * s + forward.y * c,
		0.0f );
	openDir.NormalizeInPlace();

	QAngle desired;
	VectorAngles( openDir, desired );
	// Preserve our pitch — don't snap the head down/up while wall-avoiding.
	desired.x = me->EyeAngles().x;
	me->SnapEyeAngles( desired );

	// Force a forward press so we definitely move along the new direction
	// (the body's slewing will catch up over the next few ticks).
	me->PressForwardButton( 0.2f );
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMainAction::Update( CFFBot *me, float interval )
{
	// Dead bots: don't engage, don't path. Press fire after spawn-delay so
	// PlayerDeathThink calls respawn().
	if ( !me->IsAlive() )
	{
		HandleRespawnInput( me );
		return Continue();
	}

	// Always-on per-tick concerns. These run regardless of which sub-action
	// is currently active so the bot can aim+fire while wandering / pursuing
	// flag / chasing / whatever.
	HandleWeaponSelection( me );
	UpdateLookingAroundForEnemies( me );
	FireWeaponAtEnemy( me );
	HandleCombatStrafe( me );
	HandleMobility( me );
	HandleWallAvoidance( me );
	HandleStuckState( me );

	// Class-specific per-tick driver: spy cloak/disguise, engineer build,
	// sniper zoom, etc. Layered on top of the main objective.
	FFBotClass::Update( me );

	// Sub-action dispatch: when a threat is in close range and visible,
	// suspend movement (Attack stops the chase path so we hold position).
	IVision *vision = me->GetVisionInterface();
	if ( vision )
	{
		const CKnownEntity *threat = vision->GetPrimaryKnownThreat( false );
		if ( threat && threat->GetEntity() && !threat->IsObsolete() && threat->IsVisibleRecently() )
		{
			const float distSq = ( threat->GetEntity()->GetAbsOrigin() - me->GetAbsOrigin() ).LengthSqr();
			if ( distSq > FFBOT_ATTACK_HOLD_RANGE * FFBOT_ATTACK_HOLD_RANGE )
			{
				return SuspendFor( new CFFBotAttack, "Engaging primary threat" );
			}
		}
	}

	// Medic-specific: if a wounded teammate is nearby and we're not in
	// active combat, peel off to heal them. The action ends when target is
	// healed / lost / out of reach, then CtfObjective resumes.
	if ( me->GetClassSlot() == CLASS_MEDIC )
	{
		CFFPlayer *patient = FFBotHelpers::FindWoundedTeammate( me, 600.0f );
		if ( patient )
		{
			return SuspendFor( new CFFBotHealTeammate( patient ), "Healing teammate" );
		}
	}

	return Continue();
}

//-----------------------------------------------------------------------------
// PathFollower fires this event once when stuck-time crosses its threshold.
// The continuous mashing happens in HandleStuckState (called every tick from
// Update); this is just the initial kick. Wander's own OnStuck handles the
// path-replan side.
//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMainAction::OnStuck( CFFBot *me )
{
	me->PressUseButton( 0.5f );
	return TryContinue();
}


//-----------------------------------------------------------------------------
// On death, replace this entire action tree with CFFBotDead. CFFBotDead
// holds the bot while LIFE_DEAD and on respawn ChangeTo's a fresh
// CFFBotMainAction — every life starts the behavior tree from a clean
// slate. RESULT_CRITICAL ensures sub-actions (CtfObjective, etc.) can't
// veto the transition.
//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMainAction::OnKilled( CFFBot *me, const CTakeDamageInfo &info )
{
	return TryChangeTo( new CFFBotDead, RESULT_CRITICAL, "I died!" );
}


//-----------------------------------------------------------------------------
// MOBILITY: bunny-hopping, crouch-jumping, path-recovery teleport.
//
// Bunny-hop:    when running forward at speed and on ground, press jump on
//               a ~0.4s cadence. FF preserves horizontal speed across small
//               air-time hops.
// Crouch-jump:  if PathFollower needs an upward step that exceeds normal
//               jump height but is within crouch-jump range, hold IN_DUCK
//               while jumping.
// Path-recovery teleport: if we've been continuously stuck or motionless
//               for > 15s, teleport to a friendly spawn area as the last-
//               resort unsticker. Logs so the human knows.
//-----------------------------------------------------------------------------
void CFFBotMainAction::HandleMobility( CFFBot *me )
{
	if ( !me->IsAlive() )
		return;

	ILocomotion *loco = me->GetLocomotionInterface();
	if ( !loco )
		return;

	// Use HORIZONTAL velocity for "are we actually making progress"
	// checks. Vertical velocity from jumps in place would falsely register
	// as "moving" and prevent the teleport-recovery from firing (this is
	// what made bots stay wedged between containers indefinitely).
	Vector horizVel = me->GetAbsVelocity();
	horizVel.z = 0.0f;
	const float horizSpeedSq = horizVel.LengthSqr();

	// Track "moving" baseline so the teleport-recovery doesn't fire while
	// we're legitimately stationary (defending, building, healing).
	if ( horizSpeedSq > ( 50.0f * 50.0f ) || !loco->IsAttemptingToMove() )
	{
		me->m_lastUnstuckTime = gpGlobals->curtime;
	}

	// ---- Bunny-hop ---------------------------------------------------
	// Only when moving forward HORIZONTALLY at speed AND on ground.
	const int classSlot = me->GetClassSlot();
	const bool bunnyAllowed =
		( classSlot != CLASS_SNIPER ) &&
		( classSlot != CLASS_ENGINEER ) &&
		( classSlot != CLASS_HWGUY ) &&
		( classSlot != CLASS_CIVILIAN );

	if ( bunnyAllowed && loco->IsOnGround() && horizSpeedSq > ( 180.0f * 180.0f ) )
	{
		if ( !me->m_bunnyHopTimer.HasStarted() || me->m_bunnyHopTimer.IsElapsed() )
		{
			me->PressJumpButton( 0.1f );
			me->m_bunnyHopTimer.Start( RandomFloat( 0.35f, 0.5f ) );
		}
	}

	// ---- Crouch-jump auto -------------------------------------------
	// When the locomotor is climbing or has reported "I want to step up
	// past my jump height", press CROUCH so we get the +18u from a duck-
	// jump. We use IsClimbingOrJumping as a proxy for "we're attempting a
	// vertical that needed a jump", which catches both immediate jumps
	// and ledge-climb situations.
	if ( loco->IsClimbingOrJumping() )
	{
		me->PressCrouchButton( 0.4f );
	}

	// ---- Path-recovery teleport -------------------------------------
	const float stuckDur = gpGlobals->curtime - me->m_lastUnstuckTime;
	if ( stuckDur > 15.0f )
	{
		// Teleport to nearest own spawn area. Last-resort unsticker.
		CFFNavMesh *mesh = TheFFNavMesh();
		const CUtlVector< CFFNavArea * > *spawnAreas =
			mesh ? mesh->GetSpawnRoomAreas( me->GetTeamNumber() ) : NULL;
		if ( spawnAreas && spawnAreas->Count() > 0 )
		{
			CFFNavArea *target = ( *spawnAreas )[ RandomInt( 0, spawnAreas->Count() - 1 ) ];
			Vector tpPos = target->GetCenter();
			tpPos.z += 16.0f;	// avoid clipping into floor
			me->Teleport( &tpPos, NULL, NULL );
			Msg( "[CFFBot] Path-recovery teleport: '%s' was stuck for %.1fs.\n",
				me->GetPlayerName() ? me->GetPlayerName() : "?", stuckDur );
		}
		me->m_lastUnstuckTime = gpGlobals->curtime;
	}
}


//-----------------------------------------------------------------------------
// Sound event: register the source as a recent-enemy hint. Goes into our
// personal LKP cache (so we'll watch the spot for ~8s) and into the team
// alert cache (so other bots react too).
//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMainAction::OnSound( CFFBot *me, CBaseEntity *source, const Vector &pos, KeyValues *keys )
{
	if ( !source )
		return TryContinue();
	// Ignore our own footsteps and friendly sounds.
	if ( source == me )
		return TryContinue();
	if ( source->IsPlayer() )
	{
		CFFPlayer *pp = ToFFPlayer( source );
		if ( pp && pp->GetTeamNumber() == me->GetTeamNumber() )
			return TryContinue();
	}
	// Record as a recent-enemy hint.
	me->m_lastThreatPos = pos;
	me->m_lastThreatTime = gpGlobals->curtime;
	FFBotIntel::PushTeamAlert( me->GetTeamNumber(), pos );
	return TryContinue();
}
