//========= Fortress Forever Bot =============================================//
//
// CFFBotVision — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_vision.h"
#include "ff_bot.h"
#include "ff_bot_helpers.h"
#include "ff_player.h"
#include "ff_info_script.h"
#include "shareddefs.h"
#include "entitylist.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// How close a cloaked enemy must be before the bot "notices" them.
#define FFBOT_CLOAKED_NOTICE_RANGE	96.0f


//-----------------------------------------------------------------------------
// Default IVision iterates ForEachActor (all CBaseCombatCharacter). FF only
// uses players + sentries. Sentries are CBaseAnimating, not actors, so we
// add them manually in a later phase. For now, players only.
//-----------------------------------------------------------------------------
void CFFBotVision::CollectPotentiallyVisibleEntities( CUtlVector< CBaseEntity * > *potentiallyVisible )
{
	potentiallyVisible->RemoveAll();

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pPlayer = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pPlayer )
			continue;
		if ( pPlayer == GetBot()->GetEntity() )
			continue;
		if ( !pPlayer->IsAlive() )
			continue;
		if ( pPlayer->GetTeamNumber() < FIRST_GAME_TEAM )
			continue;	// spectator / unassigned

		potentiallyVisible->AddToTail( pPlayer );
	}
}


//-----------------------------------------------------------------------------
// IsIgnored is called for every candidate before/after IsAbleToSee.
// Returning true makes the entity completely invisible to the AI brain
// (won't be added to known set, won't become a threat).
//
// FF-specific rules:
//   - Skip self / non-players / dead.
//   - Skip teammates entirely (no friendly fire awareness needed for combat).
//   - Skip enemies disguised as our own team (spy disguise — looks like a friend).
//-----------------------------------------------------------------------------
bool CFFBotVision::IsIgnored( CBaseEntity *subject ) const
{
	if ( !subject )
		return true;

	const CBaseEntity *self = GetBot()->GetEntity();
	if ( subject == self )
		return true;

	if ( !subject->IsPlayer() )
	{
		// Non-player CBCC (NPCs etc.) — let through, harmless default.
		return false;
	}

	CFFPlayer *pPlayer = ToFFPlayer( const_cast< CBaseEntity * >( subject ) );
	if ( !pPlayer || !pPlayer->IsAlive() )
		return true;

	const int myTeam = self ? self->GetTeamNumber() : TEAM_UNASSIGNED;
	const int theirTeam = pPlayer->GetTeamNumber();

	// Teammates are not threats.
	if ( theirTeam == myTeam )
		return true;

	// Enemy spy disguised as our team — looks friendly to us.
	if ( pPlayer->IsDisguised() && pPlayer->GetDisguisedTeam() == myTeam )
		return true;

	return false;
}


//-----------------------------------------------------------------------------
// IsVisibleEntityNoticed runs after IsAbleToSee passes (FOV + LOS clear).
// Used here to drop cloaked spies outside short range — even with LOS, you
// just don't see them.
//-----------------------------------------------------------------------------
bool CFFBotVision::IsVisibleEntityNoticed( CBaseEntity *subject ) const
{
	if ( !subject || !subject->IsPlayer() )
		return true;

	CFFPlayer *pPlayer = ToFFPlayer( subject );
	if ( !pPlayer )
		return true;

	if ( pPlayer->IsCloaked() )
	{
		// Only notice cloaked enemies if they're very close.
		const Vector myPos = GetBot()->GetEntity()->GetAbsOrigin();
		const Vector theirPos = pPlayer->GetAbsOrigin();
		const float distSq = ( theirPos - myPos ).LengthSqr();
		if ( distSq > FFBOT_CLOAKED_NOTICE_RANGE * FFBOT_CLOAKED_NOTICE_RANGE )
			return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Score a known threat for priority ranking. Higher = more urgent.
//
// Components:
//   - Carrying our flag (+200) — kill priority above all else
//   - Class tier (+10..+50) — civilian/spy/medic/sniper get bumps
//   - Distance falloff: closer is more urgent
//   - Visible-now bump
//   - Aiming-at-me bump (they have a bead on us, fire first)
//-----------------------------------------------------------------------------
static float ScoreThreat( CBaseEntity *me, const CKnownEntity *known )
{
	if ( !known || known->IsObsolete() )
		return -1.0f;
	CBaseEntity *ent = known->GetEntity();
	if ( !ent )
		return -1.0f;

	float score = 100.0f;

	// Distance — closer = higher (linear falloff to 2000u).
	const float dist = ( ent->GetAbsOrigin() - me->GetAbsOrigin() ).Length();
	score += MAX( 0.0f, 2000.0f - dist ) * 0.05f;	// up to +100 at point-blank

	// Visibility recency.
	if ( known->IsVisibleInFOVNow() )
		score += 50.0f;
	else if ( known->IsVisibleRecently() )
		score += 25.0f;

	// Class tier.
	CFFPlayer *pp = ToFFPlayer( ent );
	if ( pp )
	{
		switch ( pp->GetClassSlot() )
		{
		case CLASS_CIVILIAN:	score += 80.0f;  break;	// VIP — kill is the win condition
		case CLASS_SPY:			score += 35.0f;  break;	// stab risk
		case CLASS_MEDIC:		score += 30.0f;  break;	// support cut
		case CLASS_SNIPER:		score += 30.0f;  break;	// long-range threat
		case CLASS_DEMOMAN:		score += 25.0f;  break;
		case CLASS_HWGUY:		score += 25.0f;  break;
		case CLASS_SOLDIER:		score += 20.0f;  break;
		case CLASS_ENGINEER:	score += 20.0f;  break;	// + their sentry threat
		case CLASS_PYRO:		score += 15.0f;  break;
		case CLASS_SCOUT:		score += 10.0f;  break;
		}

		// Carrying ANY flag — particularly worse if it's ours.
		const int myTeam = me->GetTeamNumber();
		CBaseEntity *flagEnt = NULL;
		while ( ( flagEnt = gEntList.FindEntityByClassT( flagEnt, CLASS_INFOSCRIPT ) ) != NULL )
		{
			CFFInfoScript *flag = static_cast< CFFInfoScript * >( flagEnt );
			if ( flag->GetBotGoalType() != Omnibot::kFlag )
				continue;
			if ( !flag->IsCarried() )
				continue;
			if ( flag->GetCarrier() != ent )
				continue;
			// They're carrying *something*. Is it ours?
			if ( FFBotHelpers::IsBotsOwnFlag( myTeam, flag ) )
			{
				score += 200.0f;	// our flag is being walked out — top priority
			}
			else
			{
				score += 60.0f;		// they're a flag-carrier of any kind — visible target
			}
			break;
		}

		// Aiming-at-me check: if their forward direction points at us, they'll
		// fire first — bump priority.
		Vector theirForward;
		AngleVectors( pp->EyeAngles(), &theirForward );
		Vector toMe = me->WorldSpaceCenter() - pp->EyePosition();
		toMe.NormalizeInPlace();
		const float dot = DotProduct( theirForward, toMe );
		if ( dot > 0.85f )
			score += 40.0f;
	}

	return score;
}


//-----------------------------------------------------------------------------
// Functor for ForEachKnownEntity that scores each known entity and keeps the
// pointer to the best one. Pointer is into the vision's owned vector — safe
// for the duration of the GetPrimaryKnownThreat call (we don't add/remove).
//-----------------------------------------------------------------------------
class FFBotThreatRanker : public IVision::IForEachKnownEntity
{
public:
	FFBotThreatRanker( CBaseEntity *self, CFFBotVision *vis, bool onlyVisible )
		: m_self( self ), m_vis( vis ), m_onlyVisible( onlyVisible ),
		  m_best( NULL ), m_bestScore( 0.0f ) {}

	virtual bool Inspect( const CKnownEntity &known ) OVERRIDE
	{
		if ( known.IsObsolete() )
			return true;
		if ( m_vis->IsIgnored( known.GetEntity() ) )
			return true;
		if ( m_onlyVisible && !known.IsVisibleInFOVNow() )
			return true;
		const float s = ScoreThreat( m_self, &known );
		if ( s > m_bestScore )
		{
			m_bestScore = s;
			m_best = &known;
		}
		return true;
	}

	const CKnownEntity *GetBest() const { return m_best; }

private:
	CBaseEntity *m_self;
	CFFBotVision *m_vis;
	bool m_onlyVisible;
	const CKnownEntity *m_best;
	float m_bestScore;
};


const CKnownEntity *CFFBotVision::GetPrimaryKnownThreat( bool onlyVisibleThreats ) const
{
	CBaseEntity *self = GetBot() ? GetBot()->GetEntity() : NULL;
	if ( !self )
		return IVision::GetPrimaryKnownThreat( onlyVisibleThreats );

	// ForEachKnownEntity is non-const; we don't actually mutate vision state,
	// just inspect the known list. Cast away const for the iteration call.
	FFBotThreatRanker ranker( self, const_cast< CFFBotVision * >( this ), onlyVisibleThreats );
	const_cast< CFFBotVision * >( this )->ForEachKnownEntity( ranker );
	return ranker.GetBest();
}
