//========= Fortress Forever Bot =============================================//
//
// CFFNavArea — see ff_nav_area.h.
//
//===========================================================================//

#include "cbase.h"
#include "ff_nav_area.h"
#include "shareddefs.h"		// TEAM_BLUE..TEAM_GREEN

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


CFFNavArea::CFFNavArea( void )
{
	m_ffBotTags = 0;
	m_dangerScore = 0.0f;
	m_dangerUpdateTime = 0.0f;
	m_classMask = 0;	// 0 = all classes allowed
	for ( int i = 0; i < 4; ++i )
	{
		m_incursionDistance[ i ] = -1.0f;	// -1 = unreachable / unset
	}
}


//-----------------------------------------------------------------------------
// Incursion distance: travel distance from the given team's spawn rooms to
// this area along the nav graph. -1 means unreachable / not yet computed.
// Index team-2 maps TEAM_BLUE..TEAM_GREEN → 0..3.
//-----------------------------------------------------------------------------
float CFFNavArea::GetIncursionDistance( int team ) const
{
	const int idx = team - TEAM_BLUE;
	if ( idx < 0 || idx >= 4 )
		return -1.0f;
	return m_incursionDistance[ idx ];
}

void CFFNavArea::SetIncursionDistance( int team, float dist )
{
	const int idx = team - TEAM_BLUE;
	if ( idx < 0 || idx >= 4 )
		return;
	m_incursionDistance[ idx ] = dist;
}


//-----------------------------------------------------------------------------
// Invasion area accessor. Returns the precomputed list of areas the bot
// should look toward when watching for incoming enemies of myTeam.
//-----------------------------------------------------------------------------
const CUtlVector< CFFNavArea * > &CFFNavArea::GetEnemyInvasionAreaVector( int myTeam ) const
{
	const int idx = myTeam - TEAM_BLUE;
	static const CUtlVector< CFFNavArea * > empty;
	if ( idx < 0 || idx >= 4 )
		return empty;
	return m_invasionAreaVector[ idx ];
}


//-----------------------------------------------------------------------------
// For each team, find adjacent areas with HIGHER incursion distance for the
// enemy of myTeam — those are the areas enemies of myTeam are pushing in
// from. Mirrors TFBot's CTFNavArea::ComputeInvasionAreaVectors but without
// the PVS-visibility filter (FF nav doesn't have visibility encoded yet).
// Without visibility filtering this over-collects (gathers all "rising
// terrain" neighbors), but for the bot's "look toward enemy approach"
// default that's fine — they all point in the right direction.
//-----------------------------------------------------------------------------
void CFFNavArea::ComputeInvasionAreaVectors( void )
{
	for ( int i = 0; i < 4; ++i )
	{
		m_invasionAreaVector[ i ].RemoveAll();
	}

	// For each of MY team slots, we want to know: where do my enemies
	// approach from? An enemy of myTeam comes from areas whose
	// "incursion distance from enemy spawn" is LOWER than this area's
	// (they're closer to home, this area is deeper into our territory
	// from the enemy's POV). Equivalently: this area's incursion-distance
	// for the enemy team is greater than the adjacent area's.
	for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
	{
		const NavConnectVector *adj = GetAdjacentAreas( (NavDirType)dir );
		if ( !adj )
			continue;
		for ( int i = 0; i < adj->Count(); ++i )
		{
			CFFNavArea *neighbor = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
			if ( !neighbor )
				continue;

			for ( int myTeam = TEAM_BLUE; myTeam <= TEAM_GREEN; ++myTeam )
			{
				const int myIdx = myTeam - TEAM_BLUE;
				// "Enemy of myTeam" — for FF we don't have a clean
				// pairing concept, so use the convention "any team
				// that isn't ours". For 2-team maps this collapses to
				// the single opposing team; for 4-team maps the bot
				// looks at whichever invader is closest.
				for ( int enemyTeam = TEAM_BLUE; enemyTeam <= TEAM_GREEN; ++enemyTeam )
				{
					if ( enemyTeam == myTeam )
						continue;

					const float myInc = m_incursionDistance[ enemyTeam - TEAM_BLUE ];
					const float adjInc = neighbor->m_incursionDistance[ enemyTeam - TEAM_BLUE ];
					if ( myInc < 0.0f || adjInc < 0.0f )
						continue;	// unreachable from that team's spawn
					// We want neighbors that the enemy reaches BEFORE
					// this area (they path through there first).
					if ( adjInc < myInc - 1.0f )
					{
						// Avoid double-add when iterating multiple
						// enemy teams that would all flag the same
						// neighbor.
						if ( m_invasionAreaVector[ myIdx ].Find( neighbor ) == -1 )
						{
							m_invasionAreaVector[ myIdx ].AddToTail( neighbor );
						}
					}
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Heatmap helpers. Lazy decay: only do the math when somebody asks for the
// score, instead of iterating all areas per-frame.
//-----------------------------------------------------------------------------
#define FFBOT_DANGER_HALF_LIFE_SECONDS	30.0f

void CFFNavArea::IncrementDanger( float amount )
{
	// Refresh-decay the existing score before adding so old peaks aren't
	// double-counted as fresh.
	if ( m_dangerUpdateTime > 0.0f )
	{
		const float age = gpGlobals->curtime - m_dangerUpdateTime;
		if ( age > 0.0f )
		{
			const float decay = expf( -age * 0.69314718f / FFBOT_DANGER_HALF_LIFE_SECONDS );
			m_dangerScore *= decay;
		}
	}
	m_dangerScore += amount;
	if ( m_dangerScore > 50.0f ) m_dangerScore = 50.0f;
	m_dangerUpdateTime = gpGlobals->curtime;
}

float CFFNavArea::GetDangerScore( void ) const
{
	if ( m_dangerScore <= 0.001f )
		return 0.0f;
	if ( m_dangerUpdateTime > 0.0f )
	{
		const float age = gpGlobals->curtime - m_dangerUpdateTime;
		if ( age > 0.0f )
		{
			const float decay = expf( -age * 0.69314718f / FFBOT_DANGER_HALF_LIFE_SECONDS );
			m_dangerScore *= decay;
			m_dangerUpdateTime = gpGlobals->curtime;
		}
	}
	return m_dangerScore;
}

//-----------------------------------------------------------------------------
int CFFNavArea::SpawnTagForTeam( int team )
{
	switch ( team )
	{
	case TEAM_BLUE:		return FF_NAV_SPAWN_BLUE;
	case TEAM_RED:		return FF_NAV_SPAWN_RED;
	case TEAM_YELLOW:	return FF_NAV_SPAWN_YELLOW;
	case TEAM_GREEN:	return FF_NAV_SPAWN_GREEN;
	}
	return 0;
}

int CFFNavArea::FlagTagForTeam( int team )
{
	switch ( team )
	{
	case TEAM_BLUE:		return FF_NAV_FLAG_BLUE;
	case TEAM_RED:		return FF_NAV_FLAG_RED;
	case TEAM_YELLOW:	return FF_NAV_FLAG_YELLOW;
	case TEAM_GREEN:	return FF_NAV_FLAG_GREEN;
	}
	return 0;
}

int CFFNavArea::CapTagForTeam( int team )
{
	switch ( team )
	{
	case TEAM_BLUE:		return FF_NAV_CAP_BLUE;
	case TEAM_RED:		return FF_NAV_CAP_RED;
	case TEAM_YELLOW:	return FF_NAV_CAP_YELLOW;
	case TEAM_GREEN:	return FF_NAV_CAP_GREEN;
	}
	return 0;
}
