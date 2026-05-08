//========= Fortress Forever Bot =============================================//
//
// FFBotClass — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_class.h"
#include "ff_bot.h"
#include "ff_bot_helpers.h"
#include "ff_bot_weapon.h"
#include "ff_player.h"
#include "ff_team.h"
#include "ff_info_script.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "ammodef.h"
#include "shareddefs.h"
#include "in_buttons.h"

#include "NextBotBodyInterface.h"
#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Pick a class enum (CLASS_SCOUT..CLASS_CIVILIAN) that's a plausible disguise
// for an enemy on the given team. We don't bother checking class limits — the
// disguise is cosmetic; even if the enemy team has 0 of that class, it works.
//-----------------------------------------------------------------------------
static int PickRandomClassForDisguise( void )
{
	// Avoid civilian (10) — sticks out as 'fake' in CTF maps. Pick from the
	// nine combat classes uniformly.
	return RandomInt( CLASS_SCOUT, CLASS_ENGINEER );
}

//-----------------------------------------------------------------------------
// Pick an enemy team for the given bot. For 2-team modes (most CTF maps) this
// is just "the other team". For 4-team maps, picks any team that isn't ours.
//-----------------------------------------------------------------------------
static int PickEnemyTeamForDisguise( int myTeam )
{
	int candidates[ 4 ];
	int n = 0;
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == myTeam )
			continue;
		CFFTeam *pTeam = GetGlobalFFTeam( t );
		if ( !pTeam )
			continue;
		if ( pTeam->GetTeamLimits() == -1 )
			continue;	// disabled by Lua
		candidates[ n++ ] = t;
	}
	if ( n == 0 )
		return TEAM_RED;	// fallback — shouldn't happen
	return candidates[ RandomInt( 0, n - 1 ) ];
}


//-----------------------------------------------------------------------------
// SPY: disguise on spawn, re-disguise periodically if undisguised; cloak when
// idle (no recent threats), uncloak when in combat.
//-----------------------------------------------------------------------------
static void UpdateSpy( CFFBot *me )
{
	// First-life init: pick a disguise after a short delay (lets the spawn
	// resupply / item-equip code finish). 1.5s is enough.
	if ( !me->m_bClassDidSpawnInit )
	{
		if ( me->m_classDisguiseTimer.HasStarted() && me->m_classDisguiseTimer.IsElapsed() )
		{
			me->m_bClassDidSpawnInit = true;
			const int enemyTeam  = PickEnemyTeamForDisguise( me->GetTeamNumber() );
			const int enemyClass = PickRandomClassForDisguise();
			me->SetDisguise( enemyTeam, enemyClass, false );
			// Disguise has a fade-in delay (~3.5s) during which IsDisguised
			// reports false; cooldown the timer well past that to avoid an
			// immediate spurious re-disguise.
			me->m_classDisguiseTimer.Start( 8.0f );
		}
		else if ( !me->m_classDisguiseTimer.HasStarted() )
		{
			me->m_classDisguiseTimer.Start( 1.5f );
		}
		return;
	}

	// If disguise dropped (took damage / fired weapon), refresh it after a
	// cooldown so we don't keep eating armor by re-disguising on every tick.
	if ( !me->IsDisguised() )
	{
		if ( !me->m_classDisguiseTimer.HasStarted() || me->m_classDisguiseTimer.IsElapsed() )
		{
			const int enemyTeam  = PickEnemyTeamForDisguise( me->GetTeamNumber() );
			const int enemyClass = PickRandomClassForDisguise();
			me->SetDisguise( enemyTeam, enemyClass, false );
			me->m_classDisguiseTimer.Start( RandomFloat( 8.0f, 12.0f ) );
		}
	}

	// Cloak decision: only when there's no visible threat AND we're not
	// already cloaked. Cloak auto-falls-off when we fire or take damage, so
	// we don't need to manually uncloak in combat.
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	const bool threatVisible = ( threat && threat->GetEntity() && threat->IsVisibleRecently() );

	if ( !threatVisible && !me->IsCloaked() && me->IsCloakable() )
	{
		if ( !me->m_classCloakTimer.HasStarted() )
		{
			me->m_classCloakTimer.Start( 2.0f );	// must be threat-free for 2s
		}
		else if ( me->m_classCloakTimer.IsElapsed() )
		{
			me->Command_SpySilentCloak();
			me->m_classCloakTimer.Start( RandomFloat( 5.0f, 10.0f ) );
		}
	}
	else if ( threatVisible )
	{
		// Reset the "no threat seen" timer so cloak only kicks in after a
		// continuous safe period.
		me->m_classCloakTimer.Invalidate();
	}
}


//-----------------------------------------------------------------------------
// ENGINEER: build a sentry gun, then a dispenser. Engineers are sent to a
// defensive position by CFFBotCtfObjective; once arrived, build.
//-----------------------------------------------------------------------------
static void UpdateEngineer( CFFBot *me )
{
	if ( !me->m_bClassDidSpawnInit )
	{
		// Just mark spawn-init complete; everything else is on a polling timer.
		me->m_bClassDidSpawnInit = true;
		me->m_classBuildTimer.Start( 2.5f );	// short delay before first build attempt
		return;
	}

	// Throttle build attempts. Each call to Command_Build* either kicks off a
	// build (which takes ~3s) or no-ops if already building or already built.
	if ( me->m_classBuildTimer.HasStarted() && !me->m_classBuildTimer.IsElapsed() )
		return;

	// Don't try to build while in active combat — a moving build cancels.
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	if ( threat && threat->GetEntity() && threat->IsVisibleRecently() )
	{
		me->m_classBuildTimer.Start( 1.5f );
		return;
	}

	// Bot has to be on the ground and not airborne; the build sequence requires
	// a stable position.
	ILocomotion *loco = me->GetLocomotionInterface();
	if ( !loco || !loco->IsOnGround() )
	{
		me->m_classBuildTimer.Start( 1.0f );
		return;
	}

	// And not actively moving — PreBuildGenericThink captures m_vecBuildOrigin
	// at call time, and PostBuildGenericThink cancels the build if the player
	// drifts >128u from that origin during the ~3s build animation. Wait
	// until we've come to rest at our path goal.
	const float speedSq = me->GetAbsVelocity().LengthSqr();
	if ( speedSq > ( 40.0f * 40.0f ) )
	{
		me->m_classBuildTimer.Start( 0.5f );
		return;
	}

	// Anchor: only build when we're near our defensive position. Prefer own
	// flag (CTF), fall back to own cap (AvD-defender), then any cap (AvD-
	// attacker). On maps with neither, build wherever the bot wandered to.
	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();
	CFFInfoScript *anchor = FFBotHelpers::FindOwnFlag( myTeam );
	if ( !anchor )
		anchor = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
	if ( !anchor )
		anchor = FFBotHelpers::FindAnyCapPoint( myPos );
	if ( anchor )
	{
		const float distSq = ( anchor->GetAbsOrigin() - myPos ).LengthSqr();
		if ( distSq > ( 700.0f * 700.0f ) )
		{
			// Too far from defensive anchor — keep moving, don't build yet.
			me->m_classBuildTimer.Start( 1.5f );
			return;
		}
	}

	// Priority: sentry first, then dispenser, then upgrade/repair loop.
	CFFSentryGun *sg = me->GetSentryGun();
	if ( !sg )
	{
		// One more pre-build sanity: never drop a fresh SG within 300u of
		// an existing friendly one. Path may have led us close to another
		// engy's SG; better to wait and re-path than make a redundant SG.
		const Vector myPos = me->GetAbsOrigin();
		const int myTeam = me->GetTeamNumber();
		bool friendlyNear = false;
		CBaseEntity *otherSG = NULL;
		while ( ( otherSG = gEntList.FindEntityByClassname( otherSG, "FF_SentryGun" ) ) != NULL )
		{
			if ( otherSG->GetTeamNumber() != myTeam )
				continue;
			if ( ( otherSG->GetAbsOrigin() - myPos ).LengthSqr() < ( 300.0f * 300.0f ) )
			{
				friendlyNear = true;
				break;
			}
		}
		if ( friendlyNear )
		{
			// Don't pile up. Wait — CtfObjective will route us to a
			// different unoccupied sentry hint on its next eval tick.
			me->m_classBuildTimer.Start( 2.0f );
			return;
		}

		me->Command_BuildSentryGun();
		me->m_classBuildTimer.Start( 5.0f );	// build animation + cooldown
		return;
	}

	if ( !me->GetDispenser() )
	{
		me->Command_BuildDispenser();
		me->m_classBuildTimer.Start( 5.0f );
		return;
	}

	// Both built — upgrade / repair loop. Spanner melee on the SG transfers
	// cells per swing: cells progress upgrades 1→2→3 and refill the SG's
	// ammo. So we MUST have cells in reserve before swinging — otherwise
	// the bot is just whiffing the wrench against full-armor metal and
	// looking dumb. If cells are short, fall back to resupply (path cost
	// already discounts resupply areas when m_isLowAmmo is set).
	const bool sgWounded = sg->GetHealth() < sg->GetMaxHealth();
	const bool sgUpgrading = sg->GetLevel() < 3;
	const int cellsHeld = me->GetAmmoCount( GetAmmoDef()->Index( AMMO_CELLS ) );
	const bool haveCellsForUpgrade = ( cellsHeld >= 10 );	// 10 = one decent swing's worth

	if ( ( sgWounded || sgUpgrading ) && haveCellsForUpgrade )
	{
		const float sgDist = ( sg->GetAbsOrigin() - me->GetAbsOrigin() ).Length();
		if ( sgDist < 80.0f )
		{
			// In melee range — equip spanner and swing.
			CBaseCombatWeapon *spanner = me->Weapon_OwnsThisType( "ff_weapon_spanner" );
			CFFWeaponBase *active = me->GetActiveFFWeapon();
			if ( spanner && ( !active || !FStrEq( active->GetClassname(), "ff_weapon_spanner" ) ) )
				me->Weapon_Switch( spanner );

			// Aim at SG so the swing connects.
			IBody *body = me->GetBodyInterface();
			if ( body )
				body->AimHeadTowards( sg->WorldSpaceCenter(), IBody::CRITICAL,
					0.5f, NULL, "Spannering sentry" );
			me->PressFireButton( 0.2f );
			me->m_classBuildTimer.Start( 0.3f );
			return;
		}
	}
	// Out of cells with upgrade/repair pending — let the path-cost
	// resupply discount draw us toward an ammo pickup. We don't manually
	// path here; CtfObjective.STATE_DEFEND_OWN_FLAG keeps us in the
	// defensive area, and the bot's path will favor resupply areas en
	// route since m_isLowAmmo is true (see CFFBotPathCost).

	// Both built and topped up. Tick again later to recheck (someone may
	// damage the SG; CtfObjective handles whether we should return).
	me->m_classBuildTimer.Start( 1.5f );
}


//-----------------------------------------------------------------------------
// DEMOMAN: place a detpack near our own flag area when we're idle there. The
// detpack is FF's signature defensive ability — 5s fused explosive, big
// radius. Bot drops it then walks away naturally as the CTF objective re-
// evaluates and routes elsewhere.
//-----------------------------------------------------------------------------
static void UpdateDemoman( CFFBot *me )
{
	if ( !me->m_bClassDidSpawnInit )
	{
		me->m_bClassDidSpawnInit = true;
		me->m_classBuildTimer.Start( 8.0f );	// don't immediately drop on spawn
		return;
	}

	if ( me->m_classBuildTimer.HasStarted() && !me->m_classBuildTimer.IsElapsed() )
		return;

	// Don't bother dropping a detpack if we already have one out.
	if ( me->GetDetpack() )
	{
		me->m_classBuildTimer.Start( 5.0f );
		return;
	}

	// Don't drop while in combat; we'd kill ourselves on the fuse.
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	if ( threat && threat->GetEntity() && threat->IsVisibleRecently() )
	{
		me->m_classBuildTimer.Start( 4.0f );
		return;
	}

	ILocomotion *loco = me->GetLocomotionInterface();
	if ( !loco || !loco->IsOnGround() )
	{
		me->m_classBuildTimer.Start( 1.0f );
		return;
	}
	if ( me->GetAbsVelocity().LengthSqr() > ( 40.0f * 40.0f ) )
	{
		me->m_classBuildTimer.Start( 0.5f );
		return;
	}

	// Only place near a defensive anchor (own flag area, or own cap, or any
	// cap on AvD-style maps) — the choke we want to deny.
	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();
	CFFInfoScript *anchor = FFBotHelpers::FindOwnFlag( myTeam );
	if ( !anchor )
		anchor = FFBotHelpers::FindOwnCapPoint( myTeam, myPos );
	if ( !anchor )
		anchor = FFBotHelpers::FindAnyCapPoint( myPos );
	if ( !anchor )
	{
		me->m_classBuildTimer.Start( 8.0f );
		return;
	}
	const float distSq = ( anchor->GetAbsOrigin() - myPos ).LengthSqr();
	if ( distSq > ( 600.0f * 600.0f ) )
	{
		me->m_classBuildTimer.Start( 3.0f );
		return;
	}

	me->Command_BuildDetpack();
	// Long cooldown so we don't drop another detpack immediately if the first
	// is destroyed or detonates without us moving.
	me->m_classBuildTimer.Start( 30.0f );
}


//-----------------------------------------------------------------------------
// HWGUY: keep the assault cannon's barrels spinning when an enemy is recently
// known. AC has a half-second spinup before firing — pre-spinning means the
// first shot fires the instant we're aimed and dot-locked. IN_ATTACK2 on the
// AC is the spinup input; IN_ATTACK starts firing.
//-----------------------------------------------------------------------------
static void UpdateHWGuy( CFFBot *me )
{
	if ( !me->m_bClassDidSpawnInit )
	{
		me->m_bClassDidSpawnInit = true;
	}

	// Only relevant if we're holding the assault cannon — for shotgun /
	// supernailgun spinup is a no-op.
	CFFWeaponBase *weapon = me->GetActiveFFWeapon();
	if ( !weapon || weapon->GetWeaponID() != FF_WEAPON_ASSAULTCANNON )
		return;

	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	if ( !threat || !threat->GetEntity() )
		return;
	// Recent contact (visible now or very recently) is enough — we pre-spin
	// even when the threat just ducked behind cover, so they catch fire the
	// moment they peek.
	if ( !threat->IsVisibleRecently() )
		return;

	// Don't burn ammo on alt-fire forever — only spin while threat is in a
	// reasonable engagement window.
	const float dist = ( threat->GetEntity()->WorldSpaceCenter() - me->EyePosition() ).Length();
	if ( dist > 1800.0f )
		return;

	me->PressAltFireButton( 0.2f );
}


//-----------------------------------------------------------------------------
// GRENADES: each class has a class-specialty secondary grenade. Demo MIRV,
// Pyro napalm, Engineer EMP, Scout caltrops, HWGuy MIRV, Soldier nail,
// Medic conc, Spy gas. Behaviorally similar despite the variety — prime
// secondary, cook for ~1.2s, throw at threat. The grenade type itself
// supplies the class-specific tactical value (area denial / cluster damage
// / anti-buildable / etc.).
//
// Conditions to throw:
//   - We have secondary ammo
//   - A primary threat is visible and within throw range (300..1400u)
//   - No teammate is between us and the throw direction (rough team-kill
//     guard — full friendly-fire blast prediction would need the radius
//     for each grenade type, deferred)
//   - Cooldown elapsed (8-12s between grenade throws)
//-----------------------------------------------------------------------------
#define FFBOT_GRENADE_COOK_TIME			1.2f
#define FFBOT_GRENADE_MIN_RANGE			300.0f
#define FFBOT_GRENADE_MAX_RANGE			1400.0f
#define FFBOT_GRENADE_TEAMMATE_GUARD	250.0f


static bool IsTeammateNearLine( CFFPlayer *me, const Vector &start, const Vector &end )
{
	// If a friendly is closer to us than the target along the throw direction,
	// they could eat the grenade. Reject in that case.
	const Vector dir = end - start;
	const float lineLenSq = dir.LengthSqr();
	if ( lineLenSq < 1.0f )
		return false;

	const int myTeam = me->GetTeamNumber();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || pp == me )
			continue;
		if ( !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;

		// Project teammate position onto the throw line; if their distance
		// to the line is small AND they sit between start and end, they're
		// in the path.
		const Vector toMate = pp->GetAbsOrigin() - start;
		const float t = DotProduct( toMate, dir ) / lineLenSq;
		if ( t < 0.05f || t > 1.05f )
			continue;	// behind us, or beyond target
		const Vector closest = start + dir * t;
		const float distSq = ( pp->GetAbsOrigin() - closest ).LengthSqr();
		if ( distSq < FFBOT_GRENADE_TEAMMATE_GUARD * FFBOT_GRENADE_TEAMMATE_GUARD )
			return true;
	}
	return false;
}


static void TickGrenades( CFFBot *me )
{
	// Skip class without secondary grenades (civilian, scout's secondary
	// is jumpgun-related — leave alone for now). Civilians shouldn't throw
	// grenades; they're VIPs.
	const int classSlot = me->GetClassSlot();
	if ( classSlot == CLASS_CIVILIAN )
		return;

	// If we already started a prime, see if it's been cooking long enough.
	if ( me->m_grenadePrimeStart > 0.0f )
	{
		const float held = gpGlobals->curtime - me->m_grenadePrimeStart;
		if ( held >= FFBOT_GRENADE_COOK_TIME )
		{
			me->ThrowPrimedGrenade();
			me->m_grenadePrimeStart = 0.0f;
			me->m_grenadeCooldownTimer.Start( RandomFloat( 8.0f, 12.0f ) );
		}
		return;
	}

	if ( me->m_grenadeCooldownTimer.HasStarted() && !me->m_grenadeCooldownTimer.IsElapsed() )
		return;

	// Need ammo.
	if ( me->GetSecondaryGrenades() <= 0 )
	{
		me->m_grenadeCooldownTimer.Start( 5.0f );
		return;
	}

	// Need a visible threat in throw range.
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat( false ) : NULL;
	if ( !threat || !threat->GetEntity() )
		return;
	if ( !threat->IsVisibleRecently() || !threat->IsVisibleInFOVNow() )
		return;

	const Vector myEye = me->EyePosition();
	const Vector threatCenter = threat->GetEntity()->WorldSpaceCenter();
	const float threatDist = ( threatCenter - myEye ).Length();
	if ( threatDist < FFBOT_GRENADE_MIN_RANGE || threatDist > FFBOT_GRENADE_MAX_RANGE )
		return;

	// Friendly-fire guard.
	if ( IsTeammateNearLine( me, me->GetAbsOrigin(), threat->GetEntity()->GetAbsOrigin() ) )
		return;

	// Already primed something else? Don't double-prime.
	if ( me->IsGrenadePrimed() )
		return;

	// Prime the secondary (class-specific) grenade. The throw itself happens
	// after the cook period above.
	me->PrimeGrenade2();
	me->m_grenadePrimeStart = gpGlobals->curtime;
}


//-----------------------------------------------------------------------------
// FLAG TOSS: when carrying an enemy flag at low HP and a healthy teammate is
// nearby, drop the flag so the teammate can pick it up. Command_DropItems
// runs the Lua "dropitemcmd" predicate on owned info_ff_script entities,
// which drops the flag at our feet. Throttled so we don't spam-drop.
//-----------------------------------------------------------------------------
static void TickFlagToss( CFFBot *me )
{
	// Toss only when we're actually carrying a flag.
	if ( !FFBotHelpers::IsBotCarryingFlag( me, NULL ) )
		return;

	const int health = me->GetHealth();
	const int maxHealth = me->GetMaxHealth();
	// Only toss when in real trouble — under 35% HP. Carriers shouldn't
	// drop on every minor scratch.
	if ( maxHealth <= 0 || health > ( maxHealth * 35 ) / 100 )
		return;

	// Need a healthy nearby teammate to receive the flag.
	CFFPlayer *catcher = FFBotHelpers::FindTeammateForFlagToss( me, 250.0f );
	if ( !catcher )
		return;

	me->Command_DropItems();
	// Use the build/disguise timer family to throttle (these are mutually
	// exclusive with the flag-toss case anyway). Simpler than adding a new
	// timer member.
	me->m_calloutTimer.Start( 5.0f );
}


//-----------------------------------------------------------------------------
// PANIC CLOAK: spy at low HP with no nearby witnesses goes silent-cloak as
// a soft "fake death". FF doesn't expose Command_FakeDeath, so this is the
// closest approximation: invisible body, drop animation, hold position.
//-----------------------------------------------------------------------------
static void TickPanicCloak( CFFBot *me )
{
	if ( me->GetClassSlot() != CLASS_SPY )
		return;
	if ( me->IsCloaked() || !me->IsCloakable() )
		return;

	const int health = me->GetHealth();
	const int maxHealth = me->GetMaxHealth();
	if ( maxHealth <= 0 || health > ( maxHealth * 25 ) / 100 )
		return;

	// Don't try this with an enemy point-blank — they'll see us un-render
	// and just keep firing. Need a tiny safe gap (>96u, beyond cloak-notice
	// range).
	if ( FFBotHelpers::IsAnyEnemyNear( me->GetTeamNumber(), me->GetAbsOrigin(), 192.0f ) )
		return;

	me->Command_SpySilentCloak();
	me->PressCrouchButton( 4.0f );	// drop into prone-ish posture
}


//-----------------------------------------------------------------------------
// ENGINEER EMP-SELF-DEFENSE: when our sentry is sabotaged, throw an EMP
// grenade at it immediately. FF EMP detonates ammo-carrying entities, so
// the spy who's still touching the sentry to maintain the sap eats it.
// (This nukes our sentry's ammo too, which is risky — but a sapped sentry
// is dead anyway.)
//-----------------------------------------------------------------------------
static void TickEngineerEMP( CFFBot *me )
{
	if ( me->GetClassSlot() != CLASS_ENGINEER )
		return;

	CFFSentryGun *sg = me->GetSentryGun();
	if ( !sg || !sg->IsSabotaged() )
		return;

	// Grenade ammo gate.
	if ( me->GetSecondaryGrenades() <= 0 )
		return;

	// Already primed — let it cook to throw.
	if ( me->m_grenadePrimeStart > 0.0f )
		return;
	if ( me->IsGrenadePrimed() )
		return;

	// Override normal cooldown — sapped sentry is an emergency.
	me->PrimeGrenade2();
	me->m_grenadePrimeStart = gpGlobals->curtime;

	// Aim at the sentry briefly so the throw lands roughly where the spy is.
	IBody *body = me->GetBodyInterface();
	if ( body )
	{
		body->AimHeadTowards( sg->WorldSpaceCenter(), IBody::CRITICAL,
			FFBOT_GRENADE_COOK_TIME, NULL, "EMP saboteur" );
	}
}


//-----------------------------------------------------------------------------
// CALLOUTS: low-HP teammates call SaveMe (medics route to them via the medic
// heal action); low-armor teammates call EngyMe (engineers should respond
// when this is wired in, currently engineers are anchored to flag). These
// are FF voice commands that emit on the team channel — useful even when no
// engineer/medic bot exists, since it cues human teammates too.
//-----------------------------------------------------------------------------
static void TickCallouts( CFFBot *me )
{
	if ( me->m_calloutTimer.HasStarted() && !me->m_calloutTimer.IsElapsed() )
		return;

	const int classSlot = me->GetClassSlot();

	const int health = me->GetHealth();
	const int maxHealth = me->GetMaxHealth();
	if ( maxHealth > 0 && health < ( maxHealth / 4 ) && classSlot != CLASS_MEDIC )
	{
		me->Command_SaveMe();
		me->m_calloutTimer.Start( RandomFloat( 18.0f, 25.0f ) );
		return;
	}

	const int armor = me->GetArmor();
	const int maxArmor = me->GetMaxArmor();
	if ( maxArmor > 0 && armor < ( maxArmor / 4 ) && classSlot != CLASS_ENGINEER )
	{
		me->Command_EngyMe();
		me->m_calloutTimer.Start( RandomFloat( 25.0f, 35.0f ) );
		return;
	}
}


//-----------------------------------------------------------------------------
void FFBotClass::Update( CFFBot *me )
{
	if ( !me || !me->IsAlive() )
		return;

	// One-shot post-spawn equip: switch to class main weapon so a fresh
	// soldier holds the RPG, sniper holds the rifle, etc. (FF's default
	// active weapon at spawn is often shotgun, which isn't always optimal.)
	if ( !me->m_bClassDidSpawnInit )
	{
		FFBotWeapon::EquipMainWeaponForSpawn( me );
	}

	// All classes: occasional voice-callouts for low health / armor. Lets
	// medic and engineer bots (and humans) hear the request.
	TickCallouts( me );

	// All classes: prime+throw class-specific secondary grenade when a
	// threat is in range. The grenade type itself supplies the tactical
	// flavor (MIRV / napalm / EMP / caltrops / etc).
	TickGrenades( me );

	// FF status-effect responses (run before the per-class switch so they
	// take precedence even on classes with no Update* function).
	TickFlagToss( me );			// drop carried flag for nearby ally when low HP
	TickPanicCloak( me );		// spy: silent-cloak as fake-death approximation
	TickEngineerEMP( me );		// engineer: EMP grenade when sentry is sapped

	switch ( me->GetClassSlot() )
	{
	case CLASS_SPY:
		UpdateSpy( me );
		break;

	case CLASS_ENGINEER:
		UpdateEngineer( me );
		break;

	case CLASS_DEMOMAN:
		UpdateDemoman( me );
		break;

	case CLASS_HWGUY:
		UpdateHWGuy( me );
		break;

	default:
		// First-life init still needs to fire so other class drivers (added
		// later) start cleanly even on the first tick of life.
		if ( !me->m_bClassDidSpawnInit )
			me->m_bClassDidSpawnInit = true;
		break;
	}
}
