//========= Fortress Forever Bot =============================================//
//
// CFFBotSpyAttack — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_spy_attack.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_bot_retreat_to_cover.h"
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "NextBotKnownEntity.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"
#include "NextBotContextualQueryInterface.h"	// IS_ANY_HINDRANCE_POSSIBLE

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_spy_knife_range( "ff_bot_spy_knife_range", "300", FCVAR_CHEAT,
	"Spy switches to knife when threat is closer than this" );
ConVar ff_bot_spy_change_target_range_threshold( "ff_bot_spy_change_target_range_threshold", "300", FCVAR_CHEAT,
	"Difference in threat range that justifies swapping victims mid-attack" );


//-----------------------------------------------------------------------------
// FF rear-arc tolerance. BOT_IMPROVEMENTS_TODO calls for >0.643 dot
// (~50° from directly behind) — looser than TFBot's HARD/EXPERT but lets
// the bot commit without a difficulty system in place yet.
//-----------------------------------------------------------------------------
#define FFBOT_BACKSTAB_BEHIND_TOLERANCE	0.643f


//-----------------------------------------------------------------------------
// True if our cover is blown — bot should drop knife pretense and fight or flee.
//-----------------------------------------------------------------------------
static bool IsSpyCoverBlown( CFFBot *me, bool blownThisAction )
{
	if ( blownThisAction )
		return true;

	// Not disguised AND took damage from enemy team within last second.
	if ( !me->IsDisguised() )
	{
		if ( me->GetTimeSinceLastInjury( TEAM_ANY ) < 1.0f )
			return true;
	}

	// Medic infection ticks visibly — knife is no longer subtle.
	if ( me->IsInfected() )
		return true;

	return false;
}


//-----------------------------------------------------------------------------
CFFBotSpyAttack::CFFBotSpyAttack( CFFPlayer *victim ) : m_path( ChasePath::LEAD_SUBJECT )
{
	m_victim = victim;
	m_isCoverBlown = false;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpyAttack::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_isCoverBlown = false;
	m_decloakTimer.Invalidate();
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpyAttack::Update( CFFBot *me, float interval )
{
	IVision *vision = me->GetVisionInterface();
	if ( !vision )
		return Done( "No vision" );

	const CKnownEntity *threat = NULL;
	if ( m_victim.Get() )
		threat = vision->GetKnown( m_victim );

	const CKnownEntity *closestThreat = vision->GetPrimaryKnownThreat();

	// Adopt the closest threat if we don't have a current victim, or if the
	// closest is significantly nearer than our victim.
	if ( !threat )
	{
		threat = closestThreat;
		m_isCoverBlown = false;
		if ( closestThreat && closestThreat->GetEntity() )
			m_victim = ToFFPlayer( closestThreat->GetEntity() );
	}
	else if ( closestThreat && closestThreat->GetEntity() && closestThreat != threat )
	{
		const float rangeOld = me->GetRangeTo( threat->GetLastKnownPosition() );
		const float rangeNew = me->GetRangeTo( closestThreat->GetLastKnownPosition() );
		if ( ( rangeOld - rangeNew ) > ff_bot_spy_change_target_range_threshold.GetFloat() &&
		     closestThreat->GetEntity()->IsPlayer() )
		{
			threat = closestThreat;
			m_victim = ToFFPlayer( closestThreat->GetEntity() );
			m_isCoverBlown = false;
		}
	}

	if ( !threat || threat->IsObsolete() )
		return Done( "No threat" );

	CFFPlayer *playerThreat = ToFFPlayer( threat->GetEntity() );
	if ( !playerThreat )
		return Done( "Threat is not a player" );

	m_victim = playerThreat;

	// Cover-blown dispatch — switch to tranq and retreat.
	if ( IsSpyCoverBlown( me, m_isCoverBlown ) )
	{
		m_isCoverBlown = true;
		CBaseCombatWeapon *tranq = me->Weapon_OwnsThisType( "ff_weapon_tranquiliser" );
		if ( tranq )
			me->Weapon_Switch( tranq );
		return ChangeTo( new CFFBotRetreatToCover, "Cover blown — escaping" );
	}

	// Uncloak so we can attack. Cloak auto-falls-off on attack but burning
	// the cloak meter early lets the parent main-action fire register.
	if ( me->IsCloaked() && m_decloakTimer.IsElapsed() )
	{
		me->Command_SpyCloak();	// toggle off
		m_decloakTimer.Start( 1.0f );
	}

	// Behind-victim test.
	Vector threatForward;
	playerThreat->EyeVectors( &threatForward );
	Vector toThreat = playerThreat->GetAbsOrigin() - me->GetAbsOrigin();
	const float threatRange = toThreat.NormalizeInPlace();
	const bool isBehindVictim =
		( DotProduct( threatForward, toThreat ) > FFBOT_BACKSTAB_BEHIND_TOLERANCE );

	// Choose knife vs tranq.
	bool isKnifeFight = false;
	if ( threatRange < ff_bot_spy_knife_range.GetFloat() )
		isKnifeFight = true;
	else if ( threat->IsVisibleInFOVNow() && isBehindVictim )
		isKnifeFight = true;
	if ( me->IsCloaked() || me->IsDisguised() )
		isKnifeFight = true;

	const char *desiredWeapon = isKnifeFight ? "ff_weapon_knife" : "ff_weapon_tranquiliser";
	CFFWeaponBase *active = me->GetActiveFFWeapon();
	if ( !active || !FStrEq( active->GetClassname(), desiredWeapon ) )
	{
		CBaseCombatWeapon *want = me->Weapon_OwnsThisType( desiredWeapon );
		if ( want )
			me->Weapon_Switch( want );
	}

	bool isMovingTowardVictim = true;

	if ( isKnifeFight )
	{
		if ( threat->IsVisibleInFOVNow() )
		{
			const float circleStrafeRange = 250.0f;
			if ( threatRange < circleStrafeRange )
			{
				// Aim the stab.
				IBody *body = me->GetBodyInterface();
				if ( body )
				{
					body->AimHeadTowards( playerThreat, IBody::MANDATORY, 0.1f, NULL,
						"Aiming my stab" );
				}

				if ( !isBehindVictim )
				{
					// Circle around to the rear arc — pick side via cross product.
					Vector myForward;
					me->EyeVectors( &myForward );
					Vector cross;
					CrossProduct( threatForward, myForward, cross );
					if ( cross.z < 0.0f )
						me->PressRightButton();
					else
						me->PressLeftButton();

					// Don't bump them — they'll feel us and turn.
					if ( threatRange < 100.0f )
						isMovingTowardVictim = false;
				}
			}

			if ( threatRange < me->GetDesiredAttackRange() )
			{
				if ( me->IsDisguised() )
				{
					// Disguised — only stab from rear arc to keep cover.
					if ( isBehindVictim || m_isCoverBlown )
						me->PressFireButton();
				}
				else
				{
					// Exposed — just stab.
					me->PressFireButton();
				}
			}
		}
	}
	else
	{
		// Tranq fight — main_action's per-tick aim+fire handles firing.
		IBody *body = me->GetBodyInterface();
		if ( body )
		{
			body->AimHeadTowards( playerThreat, IBody::MANDATORY, 0.1f, NULL,
				"Aiming tranq" );
		}
	}

	if ( isMovingTowardVictim )
	{
		if ( !threat->IsVisibleRecently() ||
		     me->IsRangeGreaterThan( threat->GetEntity()->GetAbsOrigin(), me->GetDesiredAttackRange() ) ||
		     !me->IsLineOfFireClear( threat->GetEntity()->EyePosition() ) )
		{
			if ( !threat->IsVisibleRecently() )
			{
				if ( me->IsRangeLessThan( threat->GetLastKnownPosition(), 20.0f ) )
				{
					vision->ForgetEntity( threat->GetEntity() );
					return Done( "Lost target" );
				}
			}
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			// FIX 1 — single movement authority. CanDrivePath publishes the
			// path goal for the aim driver and refuses while the movement
			// arbiter owns locomotion, so this can never issue a second,
			// contradictory Approach() in the same tick.
			if ( FFBotHelpers::CanDrivePath( me, m_path ) )
				m_path.Update( me, threat->GetEntity(), cost );
		}
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpyAttack::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	m_victim = NULL;
	m_path.Invalidate();
	m_isCoverBlown = false;
	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSpyAttack::OnInjured( CFFBot *me, const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker && attacker->GetTeamNumber() != me->GetTeamNumber() )
	{
		if ( !me->IsDisguised() )
		{
			m_isCoverBlown = true;
			CBaseCombatWeapon *tranq = me->Weapon_OwnsThisType( "ff_weapon_tranquiliser" );
			if ( tranq )
				me->Weapon_Switch( tranq );
			return TryChangeTo( new CFFBotRetreatToCover, RESULT_IMPORTANT,
				"Took damage while exposed — flee" );
		}
	}
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSpyAttack::OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result )
{
	// If we bumped an enemy who's looking at us, our cover is blown.
	if ( other && other->GetTeamNumber() != me->GetTeamNumber() )
	{
		CBaseCombatCharacter *bcc = other->MyCombatCharacterPointer();
		if ( bcc && bcc->IsLookingTowards( me ) )
			m_isCoverBlown = true;
	}
	return TryContinue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSpyAttack::ShouldAttack( const INextBot *meBot, const CKnownEntity *them ) const
{
	// Until cover is blown, we want to be sneaky — don't let the parent
	// per-tick fire driver fire the tranq prematurely.
	return m_isCoverBlown ? ANSWER_YES : ANSWER_NO;
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSpyAttack::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_YES;
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSpyAttack::IsHindrance( const INextBot *me, CBaseEntity *blocker ) const
{
	// blocker can be the IS_ANY_HINDRANCE_POSSIBLE sentinel (a non-null
	// magic pointer used by the path follower to ask "could anything be a
	// hindrance at all?"). Dereferencing it crashes — guard before any
	// member call. We do want to claim "yes, hindrances are possible"
	// since this action follows a moving victim and may need to wait for
	// teammates blocking the lane, so return ANSWER_UNDEFINED (let the
	// chain decide).
	if ( blocker == IS_ANY_HINDRANCE_POSSIBLE )
		return ANSWER_UNDEFINED;

	// Don't shy away from our victim — we want to bump into them to stab.
	if ( blocker && m_victim.Get() && blocker == m_victim.Get() )
		return ANSWER_NO;
	return ANSWER_UNDEFINED;
}
