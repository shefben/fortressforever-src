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
#include "ff_bot_get_ammo.h"
#include "ff_bot_get_health.h"
#include "ff_bot_retreat_to_cover.h"
#include "ff_bot_destroy_enemy_sentry.h"
#include "ff_bot_spy_attack.h"
#include "ff_bot_spy_infiltrate.h"
#include "ff_bot_medic_follow.h"
#include "ff_bot_sniper_lurk.h"
#include "ff_bot_demoman_sticky_trap.h"
#include "ff_bot_demoman_detpack.h"
#include "ff_bot_hazard.h"
#include "ff_bot_ride_lift.h"
#include "ff_bot_gamemode.h"
#include "ff_nav_builder.h"
#include "ff_bot_helpers.h"
#include "ff_bot_weapon.h"
#include "ff_bot_intel.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_player.h"
#include "NextBotBodyInterface.h"
#include "NextBotLocomotionInterface.h"
#include "ff_weapon_base.h"
#include "debugoverlay_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Defined in ff_bot_ride_lift.cpp. Button pressing and lift riding are the
// same feature from a server operator's point of view — both are "let the bots
// operate the map's machinery" — so they share the switch.
extern ConVar ff_bot_use_lifts;

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
// FF rifle charges by holding IN_ATTACK and fires on release. We let the
// charge run up to MAX (matches the engine cap) and force a release at
// that point. The bot fires sooner — at MIN_USEFUL — once the target is
// actually in the crosshair, since a partial-charge headshot beats a
// full-charge miss. Pre-charging starts as soon as a threat enters our
// vision (even before we've finished aiming), so the rifle is hot the
// moment we acquire.
#define FFBOT_SNIPER_CHARGE_MAX			5.0f	// engine cap on charge time
#define FFBOT_SNIPER_CHARGE_MIN_USEFUL	1.0f	// don't release below this
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
// Class-aware default behavior. CtfObjective is the offense/defense default
// (flag carry / defend / wander). Sniper bots lurk in vantage spots, spies
// infiltrate and ambush, medics follow a teammate. Engineer/demoman/scout
// /soldier/HWGuy/pyro/civilian all use CtfObjective and rely on the per-tick
// FFBotClass::Update driver for class-specific tactics on top.
//-----------------------------------------------------------------------------
Action< CFFBot > *CFFBotMainAction::InitialContainedAction( CFFBot *me )
{
	switch ( me ? me->GetClassSlot() : 0 )
	{
	case CLASS_SNIPER:
		return new CFFBotSniperLurk;
	case CLASS_SPY:
		return new CFFBotSpyInfiltrate;
	case CLASS_MEDIC:
		return new CFFBotMedicFollow;
	default:
		return new CFFBotCtfObjective;
	}
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

	// WATER FIX — SWIM AIM IS THREE-DIMENSIONAL.
	//
	// FF's CGameMovement::WaterMove builds
	//     wishvel = forward*forwardMove + right*sideMove + up*upMove
	// with `forward` taken from mv->m_vecViewAngles. Vertical swim therefore
	// comes almost entirely from VIEW PITCH — and NextBotPlayer only ever
	// produces a non-negative m_flUpMove (it's runSpeed when IN_JUMP is held,
	// zero otherwise), so there is no "swim down" button either.
	//
	// Every other aim path in this file flattens .z to zero. Result: a
	// submerged bot has forward.z == 0 and literally cannot generate downward
	// wish velocity. It can only ever swim horizontally or rise. That is why
	// bots could never follow an underwater tunnel down and through.
	//
	// So when we're submerged and the route needs real vertical travel, pitch
	// the view at the goal. Runs ahead of threat aim because at that point
	// swimming is not optional — but only when |dz| is large enough to matter,
	// so ordinary surface swimming still looks at enemies.
	if ( me->GetWaterLevel() >= WL_Waist )
	{
		Vector swimGoal;
		if ( me->GetPathGoal( &swimGoal ) )
		{
			const Vector eye = me->EyePosition();
			Vector toGoal = swimGoal - eye;

			if ( fabsf( toGoal.z ) > 32.0f && toGoal.Length() > 1.0f )
			{
				// Clamp the pitch. PlayerLocomotion::Approach decomposes the
				// goal against the *2D* projection of our forward vector, so a
				// near-vertical view would collapse that projection and destroy
				// horizontal steering. +/-60 degrees keeps a usable horizontal
				// component while still giving a strong vertical wish.
				Vector dir = toGoal;
				dir.NormalizeInPlace();

				const float horiz = sqrtf( dir.x * dir.x + dir.y * dir.y );
				const float kMaxSwimPitchTan = 1.732f;	// tan(60 degrees)
				if ( horiz > 0.01f && fabsf( dir.z ) / horiz > kMaxSwimPitchTan )
				{
					dir.z = ( dir.z > 0.0f ? 1.0f : -1.0f ) * kMaxSwimPitchTan * horiz;
					dir.NormalizeInPlace();
				}

				body->AimHeadTowards( eye + dir * 200.0f, IBody::CRITICAL, 0.3f,
					NULL, "Swimming along path" );
				return;
			}
		}
	}

	const CKnownEntity *known = vision->GetPrimaryKnownThreat( false );

	// FIX 3 — stuck "look around" is a CLAMPED sweep around the path
	// direction, never a free spin.
	//
	// The old code computed `yawDeg = remaining * 90.0f * 4.0f` with remaining
	// running 2.5 -> 0, i.e. a 900-degree sweep at 360 deg/s: two and a half
	// full revolutions, in absolute world yaw. With CFFBotBody's 3000 deg/s
	// head cap the body tracked it exactly — that was the "bot spins in
	// place". It also actively hurt movement: PlayerLocomotion::Approach
	// quantizes to 8 view-relative directions, so a rotating view wobbles the
	// world-space move direction by +/-22.5 degrees at the rotation rate,
	// which is enough to keep re-colliding with a doorframe.
	//
	// Now: sweep at most +/-60 degrees, centred on the direction we actually
	// want to travel, at a human-plausible rate.
	if ( me->m_lookAroundUntil > gpGlobals->curtime )
	{
		Vector baseDir;
		Vector pathGoal;
		if ( me->GetPathGoal( &pathGoal ) )
		{
			baseDir = pathGoal - me->EyePosition();
			baseDir.z = 0.0f;
		}
		if ( baseDir.IsZero() || baseDir.NormalizeInPlace() < 0.5f )
		{
			// No path to anchor on — sweep around our current facing.
			AngleVectors( me->EyeAngles(), &baseDir );
			baseDir.z = 0.0f;
			baseDir.NormalizeInPlace();
		}

		// One full left-right-left cycle per 2 seconds, clamped to +/-60 deg.
		const float kSweepHalfAngle = 60.0f;
		const float offsetDeg = kSweepHalfAngle * sinf( gpGlobals->curtime * M_PI );
		const float rad = DEG2RAD( offsetDeg );
		const float c = cosf( rad );
		const float s = sinf( rad );
		Vector lookDir( baseDir.x * c - baseDir.y * s,
		                baseDir.x * s + baseDir.y * c,
		                0.0f );
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
		// FIX 4 — DEFAULT AIM IS ALONG THE PATH.
		//
		// Nothing in the engine does this for a player bot. PathFollower::
		// Update calls mover->FaceTowards( goalPos ), but ILocomotion::
		// FaceTowards is an empty stub and PlayerLocomotion never overrides it
		// (only NextBotGroundLocomotion does). So the bot's view was driven
		// entirely by "look at interesting things" heuristics that point away
		// from where it is walking, which is why travelling bots looked lost
		// and walked sideways into geometry.
		//
		// This runs FIRST among the no-threat cases and short-circuits the
		// rest while the bot is genuinely travelling. The old "face movement
		// direction" fallback was gated on already having speed > 120 u/s —
		// chicken-and-egg: a blocked bot has zero speed, so it could never use
		// its own facing recovery and fell through to staring at the enemy
		// invasion area instead.
		Vector pathGoal;
		if ( me->GetPathGoal( &pathGoal ) )
		{
			Vector toGoal = pathGoal - me->GetAbsOrigin();
			toGoal.z = 0.0f;
			if ( toGoal.NormalizeInPlace() > 0.1f )
			{
				// Aim at eye height along the route, not down at the goal
				// point on the floor.
				Vector lookAt = me->EyePosition() + toGoal * 300.0f;
				body->AimHeadTowards( lookAt, IBody::IMPORTANT, 0.3f, NULL,
					"Looking along path" );
				return;
			}
		}

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

		// Authored aim hint (FF_NAV2_AIM_HINT). An author standing on a spot,
		// facing a corridor, and pressing the aim key is saying "when you are
		// here and have nothing better to look at, look down there". That is
		// knowledge no heuristic recovers: which of the four exits from this
		// room the attack actually comes through is a fact about how the map is
		// played, not about its shape.
		//
		// Above the pre-aim heuristics below because they are guesses at the
		// same question; below the threat blocks above because a real enemy
		// always wins. Applies to any class — a marker is about the position.
		{
			CFFNavArea *hintArea = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
			if ( hintArea && hintArea->HasAttributeFF2( FF_NAV2_AIM_HINT ) )
			{
				// The yaw lives on the area. It got there either from an
				// authored marker or from CFFBotAnalyzer working out which
				// visible area carries the most traffic — and the bot has no
				// reason to care which.
				const QAngle hintAngles( 0.0f, hintArea->GetAimYaw(), 0.0f );
				Vector hintDir;
				AngleVectors( hintAngles, &hintDir );
				body->AimHeadTowards( me->EyePosition() + hintDir * 600.0f,
					IBody::BORING, 0.5f, NULL, "Aim hint" );
				return;
			}
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

		// Velocity-based view tracking: face the direction we're actually
		// moving, so the model doesn't visibly slide sideways/backwards.
		//
		// FIX 4 (part 2) — the speed gate here used to be 120 u/s, which made
		// this unreachable for a bot that had already stopped: no speed means
		// no facing correction means it keeps facing the wall it stalled on.
		// The threshold is now just above "standing still", so a bot that is
		// barely creeping still corrects its facing. A genuinely stationary
		// bot (defending, building, healing) has no movement direction to face
		// and falls through to the invasion-area glance below, which is the
		// correct behavior for it.
		Vector myVel = me->GetAbsVelocity();
		myVel.z = 0.0f;
		if ( myVel.LengthSqr() > ( 20.0f * 20.0f ) )
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
		else
		{
			// Difficulty-based Gaussian aim error. Tighter the longer
			// we've held this threat (HasReactedToThreat updates the
			// "first seen" timestamp). Disabled when concussed / tranqed
			// since those impose their own much-larger wobble.
			const float holdTime = ( me->m_currentThreatId == known->GetEntity()->entindex() )
				? ( gpGlobals->curtime - me->m_threatFirstSeenTime )
				: 0.0f;
			aimPos = me->ApplyAimError( aimPos, holdTime );
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
static bool RunSniperFireStateMachine( CFFBot *me,
                                        bool threatVisibleAndAimed,
                                        bool threatRecentlyVisible )
{
	switch ( me->m_sniperFireState )
	{
	case CFFBot::SNIPER_FIRE_CHARGING:
	{
		const float held = gpGlobals->curtime - me->m_sniperFireStartTime;

		// Force release at max charge — the engine caps further damage
		// gain past this point, and holding longer just wastes time.
		if ( held >= FFBOT_SNIPER_CHARGE_MAX )
		{
			me->m_sniperFireState = CFFBot::SNIPER_FIRE_COOLDOWN;
			me->m_sniperFireStartTime = gpGlobals->curtime;
			return true;
		}

		// Aimed-on-target release: as soon as the target is in our
		// crosshair AND we've held a useful minimum, fire. Don't wait
		// for full charge — a partial-charge hit on a moving target is
		// far better than a full-charge miss.
		if ( threatVisibleAndAimed && held >= FFBOT_SNIPER_CHARGE_MIN_USEFUL )
		{
			me->m_sniperFireState = CFFBot::SNIPER_FIRE_COOLDOWN;
			me->m_sniperFireStartTime = gpGlobals->curtime;
			return true;
		}

		// Keep IN_ATTACK pressed (charge continues to build).
		me->PressFireButton( 0.2f );
		return true;
	}

	case CFFBot::SNIPER_FIRE_COOLDOWN:
		if ( gpGlobals->curtime - me->m_sniperFireStartTime >= FFBOT_SNIPER_COOLDOWN )
			me->m_sniperFireState = CFFBot::SNIPER_FIRE_IDLE;
		return true;

	case CFFBot::SNIPER_FIRE_IDLE:
		// Pre-charge as soon as ANY threat enters our vision — even
		// before we've finished slewing the head onto them. This is the
		// "I'm holding the trigger waiting for him to round the corner"
		// behavior. By the time aim catches up, the rifle is already
		// hot and the partial-charge release fires immediately.
		if ( threatVisibleAndAimed || threatRecentlyVisible )
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

	// Reaction-time gate: a freshly acquired threat gets a difficulty-scaled
	// delay before we open fire (see CFFBot::GetReactionTimeFloor). The
	// gate doesn't block tracking/aim updates — those keep settling onto
	// the target during the delay, so the first shot lands cleanly.
	// Charged weapons (sniper) bypass the gate via their own pre-charge
	// state machine; otherwise the rifle wouldn't be hot in time to fire.
	const bool reacted = me->HasReactedToThreat( threat );
	if ( !reacted && !isChargedWeapon )
		return;

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
		// We also feed in "threat is in our vision but maybe not aimed yet"
		// so the rifle pre-charges while we slew the head onto target.
		const bool threatRecentlyVisible =
			threat && threat->GetEntity() && threat->IsVisibleRecently();
		RunSniperFireStateMachine( me, canFireNow, threatRecentlyVisible );
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
// True if the bot is "water-stuck": standing in a tagged water/underwater
// area, moving very slowly for the last several frames. The locomotor's
// IsStuck() check uses a velocity threshold that water bots barely beat
// (water slows you to ~120u/s, IsStuck waits for total path stall) — so
// without this, a bot oscillating between two waypoints in a well bottom
// is technically "moving" and never tripped as stuck. We trip it.
static bool IsWaterStuck( CFFBot *me )
{
	CNavArea *here = me->GetLastKnownArea();
	if ( !here )
		return false;
	const unsigned int attrs = static_cast< CFFNavArea * >( here )->GetAttributesFF();
	if ( !( attrs & ( FF_NAV_WATER | FF_NAV_UNDERWATER ) ) )
		return false;

	// Speed check — average speed below 50 u/s for at least 2.5s of
	// continuous time. We use m_lastUnstuckTime as the proxy: it's
	// refreshed by other movement systems whenever the bot makes
	// real progress.
	if ( gpGlobals->curtime - me->m_lastUnstuckTime < 2.5f )
		return false;

	const Vector vel = me->GetAbsVelocity();
	if ( vel.LengthSqr() > ( 50.0f * 50.0f ) )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
// FIX 6 — DOORS ARE A BEHAVIOR, NOT AN OBSTACLE.
//
// FF respawn gates and func_door doorways open when a player walks into their
// trigger volume or presses +use. The old stuck recovery pressed IN_BACK at
// exactly this moment, which walks the bot back OUT of the trigger volume, so
// the door never opened and the bot looped forever. This is the "bots can't
// find the door of the spawn area" symptom.
//
// Returns true while a door interaction owns the bot, in which case stuck
// recovery must stand down entirely.
//-----------------------------------------------------------------------------
bool CFFBotMainAction::HandleDoors( CFFBot *me )
{
	// Where do we want to go? Prefer the path goal; fall back to our facing.
	Vector towards;
	if ( !me->GetPathGoal( &towards ) )
	{
		Vector forward;
		AngleVectors( me->EyeAngles(), &forward );
		forward.z = 0.0f;
		if ( forward.NormalizeInPlace() < 0.5f )
			return false;
		towards = me->GetAbsOrigin() + forward * 64.0f;
	}

	CBaseEntity *door = FFBotHelpers::FindBlockingDoor( me, towards );

	if ( door == NULL )
	{
		// Interaction over (door opened, or we routed away from it).
		if ( me->m_blockingDoor.Get() != NULL )
		{
			me->m_blockingDoor = NULL;
			me->m_doorPushTimer.Invalidate();
		}
		return false;
	}

	// New door — start a bounded interaction window. Bounded so a genuinely
	// locked / team-restricted door eventually falls through to the normal
	// escalation ladder instead of pinning the bot forever.
	if ( me->m_blockingDoor.Get() != door )
	{
		me->m_blockingDoor = door;
		me->m_doorPushTimer.Start( 4.0f );
	}

	if ( me->m_doorPushTimer.IsElapsed() )
	{
		// Gave it a fair try; let the ladder route around instead. Keep the
		// handle so we don't immediately restart the window on the same door.
		return false;
	}

	// Push INTO the door and hold +use. Both matter: trigger-touch respawn
	// gates need the hull inside the volume, func_button style doors need the
	// use press.
	Vector doorPos = door->WorldSpaceCenter();
	Vector toDoor = doorPos - me->GetAbsOrigin();
	toDoor.z = 0.0f;
	if ( toDoor.NormalizeInPlace() < 0.1f )
		toDoor = ( towards - me->GetAbsOrigin() );

	me->PressUseButton( 0.2f );

	// Drive through the arbiter, not through PressForwardButton: a raw
	// IN_FORWARD is view-relative and would send us wherever the head happens
	// to be pointing. A world-space override point cannot be misaimed.
	me->SetMoveOverride( me->GetAbsOrigin() + toDoor * 96.0f, 0.2f, "Opening door" );

	// The door counts as progress, not as being stuck.
	me->m_lastUnstuckTime = gpGlobals->curtime;
	me->m_stuckStage = 0;
	me->m_stuckStageTime = gpGlobals->curtime;
	return true;
}


//-----------------------------------------------------------------------------
// BUTTONS.
//
// A door you walk into is handled by HandleDoors. A door opened by a button
// somewhere else is not: the button never shows up as a blocker, so nothing
// looks at it, and the +use the stuck ladder presses is aimed at whatever the
// bot happens to be facing — which is the door, which does nothing.
//
// Source buttons are used, not touched. CBasePlayer::PlayerUse traces out from
// the eye and requires the player to be close and looking at the thing, so
// pressing one is a small piece of deliberate behaviour: walk to it, aim at it,
// press. That is what FFBotLift::WorkButton does.
//
// Gated on actually being stuck, and on the button being visible from where we
// are. Both matter: a bot that detours to every button it passes would never
// arrive anywhere, and a button through a wall is one that belongs to a
// different room.
//-----------------------------------------------------------------------------
#define FFBOT_BUTTON_HUNT_RADIUS	320.0f

bool CFFBotMainAction::HandleButtons( CFFBot *me )
{
	if ( ff_bot_use_lifts.GetInt() <= 0 )
		return false;

	// Only when the planner has already given the ordinary route a fair go. A
	// button is a last resort before abandoning the goal, not a first thought.
	if ( me->m_stuckStage < 1 )
		return false;

	CBaseEntity *button = FFBotLift::FindButtonNear( me, me->GetAbsOrigin(),
	                                                 FFBOT_BUTTON_HUNT_RADIUS );
	if ( !button )
		return false;

	return FFBotLift::WorkButton( me, button );
}


//-----------------------------------------------------------------------------
// FIX 5 — STUCK ESCALATION AT THE PLANNER LEVEL.
//
// The old ladder was: press buttons -> press more buttons -> teleport at 15s.
// It could not work, for two reasons:
//
//   1. Every press was view-relative and NextBotPlayer collapses
//      IN_FORWARD+IN_BACK to IN_FORWARD, so the path follower's Approach()
//      cancelled the recovery outright in 15 of the 16 actions (only
//      CFFBotCtfObjective checked the inhibit flag).
//   2. When the inhibit DID apply it skipped path.Update() wholesale, which
//      also disabled PathFollower::Avoid() (the whisker-trace obstacle
//      steering that actually works) and PathFollower::Climbing() — exactly
//      when the bot needed them.
//
// The new ladder escalates the *plan*, and every physical motion goes through
// the world-space movement arbiter so nothing can cancel it:
//
//   stage 0  (<0.75s)  do nothing — let PathFollower::Avoid do its job
//   stage 1  (0.75s)   penalize the area ahead, force an immediate repath
//   stage 2  (2.0s)    back-track in WORLD space to the last spot we had speed
//   stage 3  (4.0s)    abandon the goal; the owning action picks a new one
//   stage 4  (15s)     teleport (handled in HandleMobility, unchanged floor)
//-----------------------------------------------------------------------------
void CFFBotMainAction::HandleStuckState( CFFBot *me )
{
	ILocomotion *loco = me->GetLocomotionInterface();
	if ( !loco )
		return;

	// LADDER/WATER FIX — vertical travel is progress.
	//
	// Climbing a ladder or swimming up a shaft produces almost no HORIZONTAL
	// velocity, so the old horizontal-only checks read it as "not moving": the
	// stuck ladder escalated through its stages mid-climb and the 15s recovery
	// teleport in HandleMobility eventually fired on a bot that was simply on
	// a ladder. Count total speed when we're on a ladder or in water.
	const bool onLadder = loco->IsUsingLadder() || loco->IsAscendingOrDescendingLadder();
	const bool inWater = ( me->GetWaterLevel() >= WL_Waist );

	Vector vel = me->GetAbsVelocity();
	Vector horizVel = vel;
	horizVel.z = 0.0f;

	const float progressSpeedSq = ( onLadder || inWater ) ? vel.LengthSqr()
	                                                      : horizVel.LengthSqr();
	if ( progressSpeedSq > ( 100.0f * 100.0f ) )
	{
		me->m_lastGoodPos = me->GetAbsOrigin();
		me->m_lastGoodPosTime = gpGlobals->curtime;
		me->m_lastUnstuckTime = gpGlobals->curtime;
	}

	// A bot on a ladder is committed: the locomotor owns it through
	// PlayerLocomotion::TraverseLadder, which only runs from inside
	// PathFollower::Update. Publishing a move override here would suppress the
	// path follower and strand the bot halfway up.
	if ( onLadder )
	{
		me->ClearMoveOverride();
		me->m_stuckStage = 0;
		me->m_stuckStageTime = gpGlobals->curtime;
		return;
	}

	// Doors get first refusal. If one is being worked, recovery stands down.
	if ( HandleDoors( me ) )
		return;

	// Then buttons. Second rather than first because a door we can walk into is
	// cheaper to open than a button we have to walk to, and because most of the
	// blockers on most maps are the former.
	if ( HandleButtons( me ) )
		return;

	// WATER FIX — water-stuck must NOT blacklist the water route.
	//
	// This used to stamp m_recentStuckPos with a 30s expiry, which makes
	// CFFBotPathCost charge dist*4 + 200 for every area within 256u. On a map
	// where the sewer IS the route to the enemy flag, one failed swim poisoned
	// the entire underwater path — and since the bot could not descend at all,
	// every swim failed, so the water route stayed permanently poisoned. The
	// bots' apparent "refusal to use the water" was largely this feedback loop.
	//
	// Now that swimming actually works, a slow patch of water is a reason to
	// re-aim, not a reason to abandon the route.
	if ( IsWaterStuck( me ) )
	{
		// Re-drive straight at the path goal in world space. The pitched swim
		// aim in UpdateLookingAroundForEnemies supplies the vertical component.
		Vector out;
		if ( me->GetPathGoal( &out ) )
		{
			me->SetMoveOverride( out, 0.3f, "Water: pushing along route" );
		}
		else
		{
			// No route to follow — surface so we don't drown while the owning
			// action re-plans.
			me->PressJumpButton( 0.3f );
		}

		me->m_lastUnstuckTime = gpGlobals->curtime;
		return;
	}

	if ( !loco->IsStuck() )
	{
		// Recovered. Reset the ladder so the next episode starts at stage 0,
		// and drop any un-consumed abandon request — actions that don't handle
		// it (sniper lurk, medic follow, ...) would otherwise leave it set and
		// have an unrelated action act on it minutes later.
		if ( me->m_stuckStage != 0 )
		{
			me->m_stuckStage = 0;
			me->m_stuckStageTime = gpGlobals->curtime;
			me->m_abandonGoalRequest = false;
			me->ClearMoveOverride();
		}
		return;
	}

	const float stuckDur = loco->GetStuckDuration();

	// +use costs nothing and handles button-doors / plates we are touching.
	me->PressUseButton( 0.2f );

	// ---- stage 0: let PathFollower::Avoid work --------------------------
	// Critically we do NOT suppress the path here. Avoid() is the one piece of
	// obstacle steering in this codebase that is known-good, and it only runs
	// from inside PathFollower::Update.
	if ( stuckDur < FFBOT_STUCK_JUMP_THRESHOLD )
	{
		me->m_stuckStage = 0;
		return;
	}

	// A single hop clears the majority of ledge/prop catches, and is safe:
	// IN_JUMP is not view-relative.
	if ( loco->IsOnGround() )
		me->PressJumpButton( 0.2f );

	// ---- stage 1: penalize what's AHEAD, force a repath ------------------
	if ( stuckDur < 2.0f )
	{
		if ( me->m_stuckStage < 1 )
		{
			me->m_stuckStage = 1;
			me->m_stuckStageTime = gpGlobals->curtime;

			// Penalize the area we are trying to ENTER, not the one we are
			// standing in. The old code stamped our own position, which
			// penalized the (perfectly fine) area under our feet and left the
			// actual blockage untouched, so the repath produced the same route.
			Vector ahead;
			if ( me->GetPathGoal( &ahead ) )
				me->m_recentStuckPos = ahead;
			else
				me->m_recentStuckPos = me->GetAbsOrigin();
			me->m_recentStuckExpireTime = gpGlobals->curtime + 30.0f;

			// Force an immediate repath in whichever action owns the path.
			me->m_pathInhibitTimer.Invalidate();
		}
		return;
	}

	// ---- stage 2: world-space back-track ---------------------------------
	if ( stuckDur < 4.0f )
	{
		if ( me->m_stuckStage < 2 )
		{
			me->m_stuckStage = 2;
			me->m_stuckStageTime = gpGlobals->curtime;

			// Clamped head sweep so the bot visibly searches (and so a human
			// watching can tell it is recovering, not bugged). FIX 3 keeps
			// this to +/-60 degrees around the path direction.
			me->m_lookAroundUntil = gpGlobals->curtime + 2.0f;
		}

		// Reverse toward the last place we had real velocity. If we never had
		// any (spawned into a wedge), reverse away from the blockage instead.
		Vector target;
		if ( me->m_lastGoodPosTime > 0.0f &&
		     ( me->m_lastGoodPos - me->GetAbsOrigin() ).IsLengthGreaterThan( 32.0f ) )
		{
			target = me->m_lastGoodPos;
		}
		else
		{
			Vector away = me->GetAbsOrigin();
			Vector goal;
			if ( me->GetPathGoal( &goal ) )
			{
				Vector back = me->GetAbsOrigin() - goal;
				back.z = 0.0f;
				if ( back.NormalizeInPlace() > 0.1f )
					away = me->GetAbsOrigin() + back * 128.0f;
			}
			target = away;
		}

		me->SetMoveOverride( target, 0.3f, "Stuck: backtracking" );
		me->PressCrouchButton( 0.3f );	// clears low ledges; not view-relative
		return;
	}

	// ---- stage 3: abandon the goal ---------------------------------------
	if ( me->m_stuckStage < 3 )
	{
		me->m_stuckStage = 3;
		me->m_stuckStageTime = gpGlobals->curtime;

		me->m_recentStuckPos = me->GetAbsOrigin();
		me->m_recentStuckExpireTime = gpGlobals->curtime + 30.0f;

		// Consumed by the owning action on its next Update.
		me->m_abandonGoalRequest = true;
	}

	// Keep reversing while the action re-plans, so we're not still wedged when
	// the new path arrives.
	if ( me->m_lastGoodPosTime > 0.0f )
		me->SetMoveOverride( me->m_lastGoodPos, 0.3f, "Stuck: abandoning goal" );
}

//-----------------------------------------------------------------------------
// FIX 2 — WALL AVOIDANCE STEERS THE GOAL POINT, NOT THE VIEW.
//
// The old implementation snapped eye angles toward the most open whisker and
// commented that "PlayerLocomotion::Approach reads EyeVectors next tick and
// will press IN_FORWARD aligned with the actually-walkable direction".
//
// That premise is false. Approach (NextBotPlayerLocomotion.cpp) uses the eye
// vectors ONLY as a basis to decompose (goalPos - feet) into forward/side
// button presses. Rotating the view changes which buttons fire; it does not
// change where the bot goes, which is always the goal within the 22.5-degree
// button quantization. So the snap did zero steering.
//
// What it DID do was fight PlayerBody::Upkeep, which slews the view back
// toward m_lookAtPos every frame at CFFBotBody's 3000 deg/s. Snap, slew back,
// snap, slew back — every tick the bot was blocked. That is the visible
// "spins in place", and the resulting view rotation also wobbles the
// quantized move direction enough to keep re-colliding with a doorframe.
//
// Real steering means moving the POINT we approach. That is exactly what
// PathFollower::Avoid() already does with its own whiskers, so this function
// now only handles the case Avoid cannot: no usable path at all. It publishes
// a world-space waypoint offset toward open space and never touches the view.
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

	// Don't fight an override that some other system already published.
	if ( me->IsMoveOverrideActive() )
		return;

	// Steer relative to where we WANT to go, not where the head points. If
	// there's no live path goal we have nothing to steer around, and the stuck
	// ladder will take it from here.
	Vector pathGoal;
	if ( !me->GetPathGoal( &pathGoal ) )
		return;

	const Vector start = me->GetAbsOrigin() + Vector( 0, 0, 36 );

	Vector forward = pathGoal - me->GetAbsOrigin();
	forward.z = 0.0f;
	if ( forward.NormalizeInPlace() < 0.5f )
		return;

	const float traceDist = 80.0f;	// "is there a wall right in front of me?"
	const Vector hullMin( -16, -16, -36 );
	const Vector hullMax( 16, 16, 0 );

	// Forward whisker first — if it's clear, no avoidance needed.
	{
		trace_t tr;
		UTIL_TraceHull( start, start + forward * traceDist, hullMin, hullMax,
			MASK_PLAYERSOLID, me, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
		if ( tr.fraction >= 0.9f )
			return;	// open toward the goal, no avoidance needed
	}

	// Blocked. Sweep angular offsets to find the most open direction.
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
		UTIL_TraceHull( start, start + probe * traceDist, hullMin, hullMax,
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

	if ( !foundOpen && bestFraction < 0.45f )
	{
		// Nothing clear within 80u — genuinely wedged. Leave it to the stuck
		// ladder rather than issuing a move we know is blocked.
		return;
	}

	// Publish a world-space waypoint in the open direction. Short duration:
	// this is a nudge past the obstacle, after which the path resumes.
	const float rad = DEG2RAD( bestAngleOffset );
	const float c = cosf( rad );
	const float s = sinf( rad );
	Vector openDir(
		forward.x * c - forward.y * s,
		forward.x * s + forward.y * c,
		0.0f );
	openDir.NormalizeInPlace();

	me->SetMoveOverride( me->GetAbsOrigin() + openDir * 128.0f, 0.25f,
		"Steering around obstacle" );
}


//-----------------------------------------------------------------------------
// FIX 1 — THE SINGLE MOVEMENT AUTHORITY.
//
// Runs last, after every other per-tick concern has had its chance to publish
// a move override. Issues at most one locomotor->Approach() per tick.
//
// The behavior tree updates children before parents (Action::InvokeUpdate),
// so by the time we get here the contained action's PathFollower may already
// have driven movement this tick. In that case we do nothing and the override
// takes effect next tick, when FFBotHelpers::CanDrivePath will refuse the
// path. Either way: exactly one Approach per tick, so no two systems can issue
// contradictory button presses that collapse to "walk into the wall" (recall
// NextBotPlayer resolves IN_FORWARD+IN_BACK as IN_FORWARD).
//-----------------------------------------------------------------------------
void CFFBotMainAction::DriveMovementArbiter( CFFBot *me )
{
	if ( !me->IsMoveOverrideActive() )
		return;

	// The path already moved us this tick. Let the override land next tick.
	if ( me->m_pathDrivenTick == gpGlobals->tickcount )
		return;

	ILocomotion *loco = me->GetLocomotionInterface();
	if ( !loco )
		return;

	// LADDER FIX — never steal control from a ladder traversal. The ladder
	// state machine lives in PlayerLocomotion::TraverseLadder and is only
	// pumped from PathFollower::Update; an active override suppresses the path
	// follower (FFBotHelpers::CanDrivePath), which would freeze the bot
	// halfway up. Drop the override and let the path run.
	if ( loco->IsUsingLadder() || loco->IsAscendingOrDescendingLadder() )
	{
		me->ClearMoveOverride();
		return;
	}

	loco->Approach( me->GetMoveOverridePos() );
	loco->Run();
}


//-----------------------------------------------------------------------------
// bot_show_path / bot_show_threat overlays. Per-bot toggles set by the
// console commands in ff_bot_commands.cpp.
//
// These exist because every navigation failure in this subsystem was
// invisible: "bot has no path at all" and "bot has a path it can't follow"
// look identical from the outside.
//-----------------------------------------------------------------------------
void CFFBotMainAction::DrawDebugOverlays( CFFBot *me )
{
	const float kDuration = 0.15f;

	if ( me->m_debugShowPath )
	{
		const Vector eye = me->EyePosition();

		Vector goal;
		if ( me->GetPathGoal( &goal ) )
		{
			// Yellow: current path goal segment.
			NDebugOverlay::Line( eye, goal, 255, 255, 0, true, kDuration );
			NDebugOverlay::Cross3D( goal, 8.0f, 255, 255, 0, true, kDuration );
		}
		else
		{
			// Red box over the head: NO PATH. This is the state that used to
			// be indistinguishable from "stuck".
			NDebugOverlay::Box( eye + Vector( 0, 0, 16 ), Vector( -6, -6, -6 ), Vector( 6, 6, 6 ),
				255, 0, 0, 100, kDuration );
		}

		if ( me->IsMoveOverrideActive() )
		{
			// Cyan: the arbiter is driving. Label says which system published.
			const Vector ov = me->GetMoveOverridePos();
			NDebugOverlay::Line( eye, ov, 0, 255, 255, true, kDuration );
			NDebugOverlay::Cross3D( ov, 10.0f, 0, 255, 255, true, kDuration );
			const char *why = me->GetMoveOverrideReason();
			if ( why )
				NDebugOverlay::Text( ov + Vector( 0, 0, 12 ), why, false, kDuration );
		}

		if ( me->m_stuckStage > 0 )
		{
			char buf[ 64 ];
			Q_snprintf( buf, sizeof( buf ), "STUCK stage %d", me->m_stuckStage );
			NDebugOverlay::Text( eye + Vector( 0, 0, 24 ), buf, false, kDuration );
		}
	}

	if ( me->m_debugShowThreat )
	{
		IVision *vision = me->GetVisionInterface();
		const CKnownEntity *known = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
		if ( known && known->GetEntity() )
		{
			// Magenta: current primary threat.
			NDebugOverlay::Line( me->EyePosition(), known->GetEntity()->WorldSpaceCenter(),
				255, 0, 255, true, kDuration );
		}
		if ( me->m_lastThreatTime > 0.0f &&
		     ( gpGlobals->curtime - me->m_lastThreatTime ) < 8.0f )
		{
			// Orange: last-known-position memory.
			NDebugOverlay::Cross3D( me->m_lastThreatPos, 12.0f, 255, 128, 0, true, kDuration );
		}
	}
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

	// Order matters here. Both of these may publish a move override; the stuck
	// ladder is authoritative, so it runs last and overwrites the wall-avoid
	// nudge if we're genuinely wedged rather than just brushing something.
	HandleWallAvoidance( me );
	HandleStuckState( me );

	// FIX 1 — arbiter runs after every override producer. At most one
	// Approach() per tick from this point on.
	DriveMovementArbiter( me );

	DrawDebugOverlays( me );

	// WATER FIX — DON'T PIN THE BOT TO THE SURFACE.
	//
	// This used to press JUMP on every single tick the bot was waist-deep or
	// worse. In FF that is not a gentle upward nudge: CGameMovement::WaterMove
	// treats IN_JUMP as "sharking" and HARD-ASSIGNS
	//     mv->m_vecVelocity[2] = 100.0f;
	// so holding it clamps vertical velocity to +100 every tick. A bot in
	// water physically could not descend, no matter what its path said. Between
	// that and the flattened aim, underwater tunnels were unreachable by
	// construction.
	//
	// Now we only surface when there's an actual reason to:
	//   - air is genuinely running out, or
	//   - the route wants us higher than we are.
	// Otherwise we leave vertical control to the pitched swim aim above.
	if ( me->GetWaterLevel() >= WL_Waist )
	{
		bool wantSurface = false;

		// Real drown clock, not a guess. PlayerDrownTime() is the time at
		// which we start taking damage; head for air with a margin.
		if ( me->GetWaterLevel() >= WL_Eyes &&
		     ( me->PlayerDrownTime() - gpGlobals->curtime ) < 4.0f )
		{
			wantSurface = true;
		}

		// Route wants us higher — rise toward it.
		Vector goal;
		if ( !wantSurface && me->GetPathGoal( &goal ) )
		{
			if ( goal.z > me->GetAbsOrigin().z + 32.0f )
				wantSurface = true;
		}

		// No path at all and submerged: don't just sit on the bottom.
		if ( !wantSurface && !me->GetPathGoal( &goal ) && me->GetWaterLevel() >= WL_Eyes )
			wantSurface = true;

		if ( wantSurface )
			me->PressJumpButton( 0.2f );
	}

	// Class-specific per-tick driver: spy cloak/disguise, engineer build,
	// sniper zoom, etc. Layered on top of the main objective.
	FFBotClass::Update( me );

	// ---- Environmental hazard --------------------------------------------
	//
	// Outranks combat, and that is not a mistake. A bot trading shots while the
	// room fills with gas loses the trade and then dies anyway; the enemy is in
	// the same room and has the same problem. This is the highest-priority
	// suspend in the tree for the same reason a human's first move when the gas
	// alarm goes is to leave.
	if ( CFFBotEscapeHazard::IsPossible( me ) )
	{
		return SuspendFor( new CFFBotEscapeHazard,
			"Taking environmental damage — fetching gear or getting out" );
	}

	// ---- Lifts ------------------------------------------------------------
	//
	// Checked before the combat gate below, because a bot that stops to fight
	// halfway onto a platform gets left behind by it, and because riding is a
	// state every other movement system in this file would otherwise try to
	// correct. CFFBotRideLift's own timeouts bound how long it can hold the
	// bot if the lift turns out to be unusable.
	if ( CFFBotRideLift::IsPossible( me ) )
		return SuspendFor( new CFFBotRideLift, "Using a lift" );

	// Sub-action dispatch: when a threat is in close range and visible,
	// suspend movement (Attack stops the chase path so we hold position).
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	const bool hasVisibleThreat =
		( threat && threat->GetEntity() && !threat->IsObsolete() && threat->IsVisibleRecently() );

	if ( hasVisibleThreat )
	{
		const int classSlot = me->GetClassSlot();
		const float distSq = ( threat->GetEntity()->GetAbsOrigin() - me->GetAbsOrigin() ).LengthSqr();

		// SNIPER FIX 1 — THE CHASE GATE IS CLASS-AWARE.
		//
		// This used to be a flat 600u for every class, so any threat farther
		// than that suspended the bot into CFFBotAttack. For a sniper — whose
		// whole job is 2000u+ engagement — that meant abandoning the perch the
		// moment a target appeared anywhere in the yard.
		//
		// CFFBotAttack itself already checks GetDesiredAttackRange() and bails
		// out with "In attack range", so at first glance the sniper should just
		// bounce straight back. It doesn't, because that early-out ALSO
		// requires IsLineOfFireClear( threat eye ). A sniper shooting down at
		// someone behind a crate, a wall lip, or briefly screened by a
		// teammate fails that test — and then Attack falls through to
		// m_chasePath.Update() and physically walks the bot off the high
		// ground toward the enemy. That is the "snipers run off the
		// battlements" behaviour.
		//
		// GetDesiredAttackRange() already encodes the right number per class
		// (sniper 2000, soldier 750, scout 250, ...). Use it. Keep the old
		// 600u as a floor so short-range classes don't become passive.
		float holdRange = me->GetDesiredAttackRange();
		if ( holdRange < FFBOT_ATTACK_HOLD_RANGE )
			holdRange = FFBOT_ATTACK_HOLD_RANGE;

		// Snipers never chase at all. Holding the angle IS the class. Close
		// quarters is handled by CFFBotSniperLurk, which switches to melee
		// inside ff_bot_sniper_melee_range.
		const bool willChase = ( classSlot != CLASS_SNIPER ) &&
		                       ( distSq > holdRange * holdRange );

		if ( willChase )
		{
			// Spies use a knife-aware attack that prefers backstabs from the
			// rear arc; everyone else uses the generic chase.
			if ( classSlot == CLASS_SPY )
			{
				CFFPlayer *victim = ToFFPlayer( threat->GetEntity() );
				return SuspendFor( new CFFBotSpyAttack( victim ), "Spy engaging — backstab approach" );
			}
			return SuspendFor( new CFFBotAttack, "Engaging primary threat" );
		}
	}

	// Resupply peel-off (no visible threat): GetAmmo when low ammo, GetHealth
	// when sub-50% HP. IsPossible() walks gEntList + nav areas; throttle to
	// every 1.5s so we don't burn cycles every tick.
	if ( !hasVisibleThreat )
	{
		if ( !me->m_resupplyCheckTimer.HasStarted() || me->m_resupplyCheckTimer.IsElapsed() )
		{
			me->m_resupplyCheckTimer.Start( 1.5f );

			if ( me->IsAmmoLow() && CFFBotGetAmmo::IsPossible( me ) )
				return SuspendFor( new CFFBotGetAmmo, "Low ammo — fetching ammo" );

			const int hp = me->GetHealth();
			const int hpMax = me->GetMaxHealth();
			if ( hpMax > 0 && hp * 2 < hpMax && CFFBotGetHealth::IsPossible( me ) )
				return SuspendFor( new CFFBotGetHealth, "Low HP — fetching health" );

			// Demoman opportunistic sticky trap — when no immediate threat,
			// not low HP/ammo, and we're standing in a defensive area, lay
			// stickies on the inbound chokepoint.
			if ( me->GetClassSlot() == CLASS_DEMOMAN &&
			     CFFBotDemomanStickyTrap::IsPossible( me ) )
			{
				CFFNavArea *here = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
				if ( here )
				{
					const unsigned int attrs = here->GetAttributesFF();
					const int myTeam = me->GetTeamNumber();
					const unsigned int ownFlagAttr = CFFNavArea::FlagAttributeForTeam( myTeam );
					const unsigned int ownCapAttr  = CFFNavArea::CapAttributeForTeam( myTeam );
					if ( ( attrs & ownFlagAttr ) || ( attrs & ownCapAttr ) )
					{
						return SuspendFor( new CFFBotDemomanStickyTrap,
							"Demoman laying sticky trap at defensive choke" );
					}
				}
			}

			// Demoman offensive detpack — push past midfield and place a
			// detpack at an enemy chokepoint to clear sentries / breach
			// doors. Throttled (~30s) by m_classBuildTimer; the per-action
			// 30s timeout in CFFBotDemomanDetpack also bounds runtime.
			//
			// Eligibility: in enemy half (incursion past midfield) so the
			// candidate target is a real forward push, not a walk-back.
			if ( me->GetClassSlot() == CLASS_DEMOMAN &&
			     ( !me->m_classBuildTimer.HasStarted() || me->m_classBuildTimer.IsElapsed() ) &&
			     CFFBotDemomanDetpack::IsPossible( me ) )
			{
				CFFNavArea *here = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
				if ( here )
				{
					const int myTeam = me->GetTeamNumber();
					// "Past midfield" = our area's incursion-from-some-enemy
					// is below the average enemy spawn-room incursion. Cheap
					// proxy: not in our spawn or our flag/cap room.
					const unsigned int attrs = here->GetAttributesFF();
					const unsigned int ownSpawn = CFFNavArea::SpawnRoomAttributeForTeam( myTeam );
					const unsigned int ownFlagAttr = CFFNavArea::FlagAttributeForTeam( myTeam );
					const unsigned int ownCapAttr  = CFFNavArea::CapAttributeForTeam( myTeam );
					const bool inDefensiveHalf =
						( attrs & ownSpawn ) || ( attrs & ownFlagAttr ) || ( attrs & ownCapAttr );

					if ( !inDefensiveHalf )
					{
						const Vector target = CFFBotDemomanDetpack::PickTargetChoke( me );
						if ( target != vec3_origin )
						{
							me->m_classBuildTimer.Start( 30.0f );
							return SuspendFor( new CFFBotDemomanDetpack( target ),
								"Demoman detpacking enemy chokepoint" );
						}
					}
				}
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
// Environmental damage detection.
//
// This is the "when" that rock2's gas response was missing. FF has no
// engine-level signal for a hazard being live and no way to read the Lua
// schedule that switches one on — but the damage it deals arrives here with its
// type bits and its attacker, and that is enough to tell a trigger_hurt from a
// rocket without knowing anything about the map.
//
// Deliberately does not consume the event: everything else that responds to
// being hurt still needs to see it.
//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMainAction::OnInjured( CFFBot *me, const CTakeDamageInfo &info )
{
	FFBotHazard::OnInjured( me, info );
	return TryContinue();
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

	// FIX 8 — don't bunny-hop into a turn.
	//
	// Airborne movement is still view-relative (game movement air control uses
	// cmd->viewangles), so hopping through a corner or a doorway curves the bot
	// into the wall — one of the ways they "run into walls" while apparently
	// following a valid path. Suppress hopping when:
	//   - the next path segment turns more than ~45 degrees, or is a climb /
	//     gap jump (m_pathTurnAhead, published by FFBotHelpers::CanDrivePath)
	//   - we're in a tight space (no room to land clear of geometry)
	//   - a move override is live (recovery / door work needs precise ground
	//     movement, not ballistics)
	//
	// Also never in water or on a ladder: FF's WaterMove reads IN_JUMP as
	// "sharking" and hard-assigns vertical velocity to +100, which would fight
	// every attempt to swim down a tunnel; and jumping off a ladder is exactly
	// what we don't want mid-climb.
	bool bunnyGeometryOk = !me->m_pathTurnAhead && !me->IsMoveOverrideActive() &&
	                       me->GetWaterLevel() < WL_Waist &&
	                       !loco->IsUsingLadder();

	if ( bunnyGeometryOk )
	{
		// Clearance check ahead along our actual travel direction. 96u is
		// roughly one hop of horizontal travel at FF run speed.
		Vector travel = horizVel;
		if ( travel.NormalizeInPlace() > 0.1f )
		{
			trace_t tr;
			const Vector from = me->GetAbsOrigin() + Vector( 0, 0, 36 );
			UTIL_TraceHull( from, from + travel * 96.0f,
				Vector( -16, -16, -36 ), Vector( 16, 16, 0 ),
				MASK_PLAYERSOLID, me, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
			if ( tr.fraction < 0.9f )
				bunnyGeometryOk = false;
		}
	}

	if ( bunnyAllowed && bunnyGeometryOk && loco->IsOnGround() && horizSpeedSq > ( 180.0f * 180.0f ) )
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

	// ---- Authored jump spots (FF_NAV2_JUMP_SPOT) ---------------------
	//
	// An author placing ff_nav_place jump is saying "the way on from here is
	// upwards and you have to leave the ground for it". The locomotor will not
	// work that out on its own: PathFollower jumps when the nav graph says the
	// connection needs one, and the reason a jump spot got marked by hand is
	// usually that the graph does not say so.
	//
	// What this does NOT do is conc-jump or rocket-jump. Those are separate
	// movement verbs the bot does not have, and pretending otherwise would send
	// soldiers walking off ledges. This is the honest subset: on a marked spot,
	// heading for something above us, commit to a running duck-jump. It clears
	// the gaps a running jump can clear and no more.
	if ( loco->IsOnGround() && horizSpeedSq > ( 150.0f * 150.0f ) )
	{
		CFFNavArea *here = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
		if ( here && here->HasAttributeFF2( FF_NAV2_JUMP_SPOT ) )
		{
			Vector goal;
			if ( me->GetPathGoal( &goal ) && goal.z > ( me->GetAbsOrigin().z + 32.0f ) )
			{
				me->PressJumpButton( 0.2f );
				me->PressCrouchButton( 0.4f );
			}
		}
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
