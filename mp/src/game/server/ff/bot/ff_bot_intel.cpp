//========= Fortress Forever Bot =============================================//
//
// FFBotIntel — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_intel.h"
#include "ff_bot.h"
#include "ff_bot_helpers.h"
#include "ff_player.h"
#include "ff_team.h"
#include "ff_info_script.h"
#include "ff_nav_area.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "nav_mesh.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Per-team alert cache. One slot per FF team; the freshest alert wins
// (newer pushes overwrite older). Keeps the data structure trivial.
//-----------------------------------------------------------------------------
struct TeamAlert
{
	bool   valid;
	Vector pos;
	float  time;
};

#define FFBOT_TEAM_SLOTS	4	// TEAM_BLUE..TEAM_GREEN
static TeamAlert s_teamAlerts[ FFBOT_TEAM_SLOTS ];

static int TeamSlot( int team )
{
	if ( team < TEAM_BLUE || team > TEAM_GREEN )
		return -1;
	return team - TEAM_BLUE;
}


void FFBotIntel::PushTeamAlert( int team, const Vector &pos )
{
	const int slot = TeamSlot( team );
	if ( slot < 0 )
		return;
	TeamAlert &a = s_teamAlerts[ slot ];
	a.valid = true;
	a.pos   = pos;
	a.time  = gpGlobals->curtime;
}


bool FFBotIntel::GetFreshTeamAlert( int team, float maxAge, Vector *outPos, float *outAge )
{
	const int slot = TeamSlot( team );
	if ( slot < 0 )
		return false;
	const TeamAlert &a = s_teamAlerts[ slot ];
	if ( !a.valid )
		return false;
	const float age = gpGlobals->curtime - a.time;
	if ( age > maxAge )
		return false;
	if ( outPos ) *outPos = a.pos;
	if ( outAge ) *outAge = age;
	return true;
}


//-----------------------------------------------------------------------------
// Aggression multiplier from team score delta. Cap at [0.5, 1.5] so even a
// big lead/deficit doesn't make the bot suicidal or completely passive.
//-----------------------------------------------------------------------------
float FFBotIntel::GetTeamAggression( int team )
{
	CFFTeam *us = GetGlobalFFTeam( team );
	if ( !us )
		return 1.0f;

	// Compute biggest enemy score and compare to ours.
	const int myScore = us->GetScore();
	int enemyScore = 0;
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		if ( t == team )
			continue;
		CFFTeam *other = GetGlobalFFTeam( t );
		if ( !other )
			continue;
		if ( other->GetScore() > enemyScore )
			enemyScore = other->GetScore();
	}

	const int delta = myScore - enemyScore;
	// Each point of deficit raises aggression by 0.05; each point of lead
	// drops it. Clamp to [0.5, 1.5].
	const float aggression = 1.0f - (float)delta * 0.05f;
	if ( aggression < 0.5f ) return 0.5f;
	if ( aggression > 1.5f ) return 1.5f;
	return aggression;
}


//-----------------------------------------------------------------------------
// Adaptive role reassignment: at respawn, swap class if our team is missing
// a key role. Conservative — only swaps when there's a clear vacancy and
// our current class is non-essential. Avoids thrashing where 4 bots each
// switch to medic on the same tick.
//-----------------------------------------------------------------------------
static int CountAliveOnTeam( int team, int classSlot )
{
	int n = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp || !pp->IsAlive() )
			continue;
		if ( pp->GetTeamNumber() != team )
			continue;
		if ( pp->GetClassSlot() != classSlot )
			continue;
		++n;
	}
	return n;
}


int FFBotIntel::PickRespawnClass( CFFBot *me, int currentClass )
{
	if ( !me )
		return currentClass;

	const int team = me->GetTeamNumber();
	if ( team < TEAM_BLUE || team > TEAM_GREEN )
		return currentClass;

	// Already filling a key role — keep it.
	if ( currentClass == CLASS_ENGINEER || currentClass == CLASS_MEDIC )
		return currentClass;

	// Sniper / HWGuy / Civilian have specific tactical purposes; don't
	// auto-swap them away unless the user manually moved them.
	if ( currentClass == CLASS_SNIPER || currentClass == CLASS_HWGUY ||
		 currentClass == CLASS_CIVILIAN )
		return currentClass;

	// Team has no engineer alive → take the role.
	if ( CountAliveOnTeam( team, CLASS_ENGINEER ) == 0 )
	{
		CFFTeam *pTeam = GetGlobalFFTeam( team );
		if ( pTeam && pTeam->GetClassLimit( CLASS_ENGINEER ) != -1 )
			return CLASS_ENGINEER;
	}
	// Team has no medic alive → take the role.
	if ( CountAliveOnTeam( team, CLASS_MEDIC ) == 0 )
	{
		CFFTeam *pTeam = GetGlobalFFTeam( team );
		if ( pTeam && pTeam->GetClassLimit( CLASS_MEDIC ) != -1 )
			return CLASS_MEDIC;
	}

	return currentClass;
}


//-----------------------------------------------------------------------------
// Round-state polling: detect "our flag was just stolen" and push a defender
// alert. Bots near our flag area will read the alert and converge. Polled
// here instead of via IGameEventListener2 to keep coupling minimal.
//-----------------------------------------------------------------------------
struct FlagSnapshot
{
	EHANDLE flag;
	bool    wasCarried;	// state on prior tick
};
static CUtlVector< FlagSnapshot > s_flagSnapshots;


// Per-player snapshot for death-detection. We don't subscribe to the engine
// player_death event because a coupling-light approach is simpler: poll
// IsAlive() each tick.
struct PlayerSnapshot
{
	EHANDLE player;
	bool    wasAlive;
	Vector  lastAlivePos;
};
static CUtlVector< PlayerSnapshot > s_playerSnapshots;


static PlayerSnapshot *FindPlayerSnapshot( CFFPlayer *pp )
{
	for ( int i = 0; i < s_playerSnapshots.Count(); ++i )
	{
		if ( s_playerSnapshots[ i ].player.Get() == pp )
			return &s_playerSnapshots[ i ];
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Hot-zone heatmap update: detect alive→dead transitions, increment the
// nearest nav area's danger score by 1.0. Areas where lots of dying happens
// glow hot; bots avoid them mildly via path cost and stay alert there.
//-----------------------------------------------------------------------------
static void TickHeatmap( void )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( !pp )
			continue;

		PlayerSnapshot *snap = FindPlayerSnapshot( pp );
		if ( !snap )
		{
			PlayerSnapshot ns;
			ns.player.Set( pp );
			ns.wasAlive = pp->IsAlive();
			ns.lastAlivePos = pp->GetAbsOrigin();
			s_playerSnapshots.AddToTail( ns );
			continue;
		}

		const bool isAlive = pp->IsAlive();
		if ( snap->wasAlive && !isAlive )
		{
			// Death transition. Find the nav area at last-alive position
			// (current pos may be the spawn-respawn-fade origin which is
			// useless to us).
			if ( TheNavMesh && TheNavMesh->IsLoaded() )
			{
				CNavArea *area = TheNavMesh->GetNearestNavArea(
					snap->lastAlivePos, false, 256.0f, false, true, TEAM_ANY );
				if ( area )
					static_cast< CFFNavArea * >( area )->OnCombat();
			}
		}
		snap->wasAlive = isAlive;
		if ( isAlive )
			snap->lastAlivePos = pp->GetAbsOrigin();
	}

	// Trim snapshots whose player handle has gone bad (disconnect).
	for ( int i = s_playerSnapshots.Count() - 1; i >= 0; --i )
	{
		if ( !s_playerSnapshots[ i ].player.Get() )
			s_playerSnapshots.Remove( i );
	}
}


static FlagSnapshot *FindFlagSnapshot( CFFInfoScript *flag )
{
	for ( int i = 0; i < s_flagSnapshots.Count(); ++i )
	{
		if ( s_flagSnapshots[ i ].flag.Get() == flag )
			return &s_flagSnapshots[ i ];
	}
	return NULL;
}


void FFBotIntel::Tick( void )
{
	// Decay old alerts (cap at 12s).
	for ( int i = 0; i < FFBOT_TEAM_SLOTS; ++i )
	{
		TeamAlert &a = s_teamAlerts[ i ];
		if ( a.valid && ( gpGlobals->curtime - a.time ) > 12.0f )
			a.valid = false;
	}

	TickHeatmap();

	// Walk flags; on transition to "carried", push an alert to the OWNING
	// team (so its bots react to "our flag was just stolen").
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( s->IsRemoved() )
			continue;

		const bool isCarried = s->IsCarried();
		FlagSnapshot *snap = FindFlagSnapshot( s );
		if ( !snap )
		{
			FlagSnapshot ns;
			ns.flag.Set( s );
			ns.wasCarried = isCarried;
			s_flagSnapshots.AddToTail( ns );
			continue;
		}

		if ( !snap->wasCarried && isCarried )
		{
			// Just got picked up. Push alert to the team that owns this flag
			// (i.e., the team that does NOT have a touch bit on it).
			for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
			{
				if ( FFBotHelpers::IsBotsOwnFlag( t, s ) )
				{
					PushTeamAlert( t, s->GetAbsOrigin() );
					break;
				}
			}
		}
		snap->wasCarried = isCarried;
	}

	// Drop snapshots whose flag has been removed.
	for ( int i = s_flagSnapshots.Count() - 1; i >= 0; --i )
	{
		if ( !s_flagSnapshots[ i ].flag.Get() )
			s_flagSnapshots.Remove( i );
	}
}
