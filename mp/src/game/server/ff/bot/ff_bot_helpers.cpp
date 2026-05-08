//========= Fortress Forever Bot =============================================//
//
// FFBotHelpers — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_helpers.h"
#include "ff_bot.h"
#include "ff_player.h"
#include "ff_info_script.h"
#include "shareddefs.h"
#include "entitylist.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


bool FFBotHelpers::IsBotCarryingFlag( CFFBot *me, CFFInfoScript **outFlag )
{
	if ( !me )
		return false;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( s->IsCarried() && s->GetCarrier() == me )
		{
			if ( outFlag )
				*outFlag = s;
			return true;
		}
	}
	return false;
}


// Map FF team (TEAM_BLUE..TEAM_GREEN = 2..5) to the bit position used by
// CFFInfoScript::m_BotTeamFlags (Omnibot's TF_TEAM_BLUE..TF_TEAM_GREEN = 1..4).
static int BotTeamFlagBitForFFTeam( int ffTeam )
{
	if ( ffTeam < TEAM_BLUE || ffTeam > TEAM_GREEN )
		return 0;
	return 1 << ( ffTeam - TEAM_BLUE + Omnibot::TF_TEAM_BLUE );
}


bool FFBotHelpers::IsBotsOwnFlag( int myTeam, CFFInfoScript *flag )
{
	if ( !flag )
		return false;
	const int myBit = BotTeamFlagBitForFFTeam( myTeam );
	if ( myBit == 0 )
		return false;
	const int teamFlags = flag->GetBotTeamFlags();
	if ( teamFlags == 0 )
	{
		// Flag has no team-flag bits set at all (rare, e.g. unconfigured
		// neutral). Treat as not-mine — bots will go grab it as offense.
		return false;
	}
	// Own flag = touchflags do NOT include our team (we can't grab/touch it
	// for pickup, only enemies can).
	return ( teamFlags & myBit ) == 0;
}


bool FFBotHelpers::CanBotGrabFlag( int myTeam, CFFInfoScript *flag )
{
	if ( !flag )
		return false;
	const int myBit = BotTeamFlagBitForFFTeam( myTeam );
	if ( myBit == 0 )
		return false;
	const int teamFlags = flag->GetBotTeamFlags();
	if ( teamFlags == 0 )
	{
		// Neutral / unconfigured — anyone can grab.
		return true;
	}
	return ( teamFlags & myBit ) != 0;
}


CFFInfoScript *FFBotHelpers::FindOwnFlag( int myTeam )
{
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( s->IsRemoved() )
			continue;
		if ( IsBotsOwnFlag( myTeam, s ) )
			return s;
	}
	return NULL;
}


bool FFBotHelpers::IsOwnFlagThreatened( int myTeam, float radius )
{
	CFFInfoScript *ownFlag = FindOwnFlag( myTeam );
	if ( !ownFlag )
		return false;

	const Vector flagPos = ownFlag->GetAbsOrigin();
	const float radiusSq = radius * radius;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() == myTeam )
			continue;
		// Spy disguised as our team — assume hostile (real spies are still
		// a threat near our flag).
		const float distSq = ( pp->GetAbsOrigin() - flagPos ).LengthSqr();
		if ( distSq < radiusSq )
			return true;
	}
	return false;
}


CFFInfoScript *FFBotHelpers::FindOwnCapPoint( int myTeam, const Vector &myPos )
{
	// Caps inherit FF's "Lua-table-team-but-not-C++-team" issue, so
	// GetTeamNumber() on a cap is unreliable. In CTF, own cap is always
	// paired with own flag at the same base — use proximity to own flag as
	// the anchor. If no own flag (modes without flags), fall back to
	// closest cap to ourself.
	CFFInfoScript *ownFlag = FindOwnFlag( myTeam );
	const Vector anchor = ownFlag ? ownFlag->GetAbsOrigin() : myPos;

	CFFInfoScript *best = NULL;
	float bestDistSq = FLT_MAX;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlagCap )
			continue;
		if ( s->IsRemoved() )
			continue;
		const float dSq = ( s->GetAbsOrigin() - anchor ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = s;
		}
	}
	return best;
}


CFFInfoScript *FFBotHelpers::FindAnyCapPoint( const Vector &myPos )
{
	CFFInfoScript *best = NULL;
	float bestDistSq = FLT_MAX;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlagCap )
			continue;
		if ( s->IsRemoved() )
			continue;
		const float dSq = ( s->GetAbsOrigin() - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = s;
		}
	}
	return best;
}


CFFPlayer *FFBotHelpers::FindWoundedTeammate( CFFPlayer *me, float searchRange )
{
	if ( !me )
		return NULL;

	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();
	const float searchSq = searchRange * searchRange;

	CFFPlayer *best = NULL;
	int bestDeficit = 10;	// must be at least 10hp short of max to qualify

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || pp == me )
			continue;
		if ( !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		const float distSq = ( pp->GetAbsOrigin() - myPos ).LengthSqr();
		if ( distSq > searchSq )
			continue;

		const int deficit = pp->GetMaxHealth() - pp->GetHealth();
		if ( deficit > bestDeficit )
		{
			bestDeficit = deficit;
			best = pp;
		}
	}
	return best;
}


CFFPlayer *FFBotHelpers::FindFriendlyCivilian( int myTeam )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp )
			continue;
		if ( !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		if ( pp->GetClassSlot() == CLASS_CIVILIAN )
			return pp;
	}
	return NULL;
}


CFFPlayer *FFBotHelpers::FindNearestFriendlyMedic( int myTeam, const Vector &fromPos, float maxRange )
{
	CFFPlayer *best = NULL;
	float bestDistSq = maxRange * maxRange;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		if ( pp->GetClassSlot() != CLASS_MEDIC )
			continue;
		const float dSq = ( pp->GetAbsOrigin() - fromPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = pp;
		}
	}
	return best;
}


CFFPlayer *FFBotHelpers::FindTeammateForFlagToss( CFFPlayer *carrier, float radius )
{
	if ( !carrier )
		return NULL;
	const int myTeam = carrier->GetTeamNumber();
	const Vector myPos = carrier->GetAbsOrigin();
	const float radiusSq = radius * radius;

	CFFPlayer *best = NULL;
	float bestDistSq = radiusSq;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || pp == carrier || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != myTeam )
			continue;
		// Don't toss to a teammate who can't really run with it (low HP).
		if ( pp->GetMaxHealth() > 0 && pp->GetHealth() < pp->GetMaxHealth() / 3 )
			continue;
		// Don't toss to civilian (too slow / fragile in Hunted-style maps).
		if ( pp->GetClassSlot() == CLASS_CIVILIAN )
			continue;
		const float dSq = ( pp->GetAbsOrigin() - myPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = pp;
		}
	}
	return best;
}


bool FFBotHelpers::IsAnyEnemyNear( int myTeam, const Vector &pos, float radius )
{
	const float radiusSq = radius * radius;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() == myTeam )
			continue;
		const float dSq = ( pp->GetAbsOrigin() - pos ).LengthSqr();
		if ( dSq < radiusSq )
			return true;
	}
	return false;
}


CFFPlayer *FFBotHelpers::FindTeammateCarryingEnemyFlag( CFFPlayer *me )
{
	if ( !me )
		return NULL;
	const int myTeam = me->GetTeamNumber();

	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( !s->IsCarried() )
			continue;
		CBaseEntity *carrier = s->GetCarrier();
		CFFPlayer *cp = carrier ? ToFFPlayer( carrier ) : NULL;
		if ( !cp || cp == me )
			continue;
		if ( cp->GetTeamNumber() != myTeam )
			continue;
		// This carrier is on our team — make sure the flag they hold is an
		// enemy flag (i.e. one our team can grab — touchflags include us).
		if ( !CanBotGrabFlag( myTeam, s ) )
			continue;
		return cp;
	}
	return NULL;
}
