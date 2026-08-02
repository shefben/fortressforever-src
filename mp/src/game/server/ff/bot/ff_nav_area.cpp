//========= Fortress Forever Bot =============================================//
//
// CFFNavArea — see header. TF-style nav area for FF.
//
//===========================================================================//

#include "cbase.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "shareddefs.h"
#include "tier1/utlbuffer.h"
#include "nav_mesh.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Combat-intensity tunables. Mirror TF's cvars (tf_nav_in_combat_duration,
// tf_nav_combat_build_rate, tf_nav_combat_decay_rate) but scoped to FF so
// changing one mod doesn't affect the other.
static ConVar ff_nav_combat_build_rate( "ff_nav_combat_build_rate", "0.05",
	FCVAR_CHEAT, "Combat-intensity gain per OnCombat() call (caps at 1.0)" );
static ConVar ff_nav_combat_decay_rate( "ff_nav_combat_decay_rate", "0.022",
	FCVAR_CHEAT, "Combat-intensity decay per second toward zero" );


CFFNavArea::CFFNavArea( void )
{
	m_attributeFlags = 0;
	m_attributeFlags2 = 0;
	m_combatIntensity = 0.0f;
	m_classMask = 0;
	for ( int i = 0; i < FF_NAV_TEAM_COUNT; ++i )
	{
		m_distanceFromSpawnRoom[ i ] = -1.0f;
	}
}

//-----------------------------------------------------------------------------
void CFFNavArea::OnServerActivate( void )
{
	BaseClass::OnServerActivate();
}

//-----------------------------------------------------------------------------
void CFFNavArea::OnRoundRestart( void )
{
	BaseClass::OnRoundRestart();
	m_combatIntensity = 0.0f;
}

//-----------------------------------------------------------------------------
// Save persistent attributes only — entity-derived bits (spawn rooms, flags,
// caps, pickups) are re-stamped every load by the tagger.
//-----------------------------------------------------------------------------
void CFFNavArea::Save( CUtlBuffer &fileBuffer, unsigned int version ) const
{
	CNavArea::Save( fileBuffer, version );
	const unsigned int persistent = m_attributeFlags & FF_NAV_PERSISTENT_ATTRIBUTES;
	fileBuffer.PutUnsignedInt( persistent );
}

//-----------------------------------------------------------------------------
NavErrorType CFFNavArea::Load( CUtlBuffer &fileBuffer, unsigned int version, unsigned int subVersion )
{
	CNavArea::Load( fileBuffer, version, subVersion );

	if ( subVersion > TheNavMesh->GetSubVersionNumber() )
	{
		Warning( "Unknown FFNavArea sub-version number\n" );
		return NAV_INVALID_FILE;
	}
	if ( subVersion <= 1 )
	{
		// pre-attribute meshes — clear and accept
		m_attributeFlags = 0;
		return NAV_OK;
	}

	m_attributeFlags = fileBuffer.GetUnsignedInt();
	if ( !fileBuffer.IsValid() )
	{
		Warning( "Can't read FF-specific nav-area attributes\n" );
		return NAV_INVALID_FILE;
	}
	return NAV_OK;
}

//-----------------------------------------------------------------------------
int CFFNavArea::SpawnRoomAttributeForTeam( int team )
{
	switch ( team )
	{
	case TEAM_BLUE:		return FF_NAV_SPAWN_ROOM_BLUE;
	case TEAM_RED:		return FF_NAV_SPAWN_ROOM_RED;
	case TEAM_YELLOW:	return FF_NAV_SPAWN_ROOM_YELLOW;
	case TEAM_GREEN:	return FF_NAV_SPAWN_ROOM_GREEN;
	}
	return 0;
}

int CFFNavArea::FlagAttributeForTeam( int team )
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

int CFFNavArea::DefendAttributeForTeam( int team )
{
	switch ( team )
	{
	case TEAM_BLUE:		return FF_NAV2_DEFEND_BLUE;
	case TEAM_RED:		return FF_NAV2_DEFEND_RED;
	case TEAM_YELLOW:	return FF_NAV2_DEFEND_YELLOW;
	case TEAM_GREEN:	return FF_NAV2_DEFEND_GREEN;
	}
	return 0;
}

int CFFNavArea::CapAttributeForTeam( int team )
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

//-----------------------------------------------------------------------------
float CFFNavArea::GetIncursionDistance( int team ) const
{
	const int idx = FF_NAV_TEAM_INDEX( team );
	if ( idx < 0 || idx >= FF_NAV_TEAM_COUNT )
		return -1.0f;
	return m_distanceFromSpawnRoom[ idx ];
}

void CFFNavArea::SetIncursionDistance( int team, float dist )
{
	const int idx = FF_NAV_TEAM_INDEX( team );
	if ( idx < 0 || idx >= FF_NAV_TEAM_COUNT )
		return;
	m_distanceFromSpawnRoom[ idx ] = dist;
}

bool CFFNavArea::IsReachableByTeam( int team ) const
{
	const int idx = FF_NAV_TEAM_INDEX( team );
	if ( idx < 0 || idx >= FF_NAV_TEAM_COUNT )
		return false;
	return m_distanceFromSpawnRoom[ idx ] >= 0.0f;
}

//-----------------------------------------------------------------------------
// Adjacent area with largest increase in incursion distance — i.e., the
// direction "deeper into enemy territory" for the given team. Mirrors
// CTFNavArea::GetNextIncursionArea.
//-----------------------------------------------------------------------------
CFFNavArea *CFFNavArea::GetNextIncursionArea( int team ) const
{
	CFFNavArea *next = NULL;
	float bestDist = GetIncursionDistance( team );

	for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
	{
		const NavConnectVector *adj = GetAdjacentAreas( (NavDirType)dir );
		if ( !adj )
			continue;
		for ( int i = 0; i < adj->Count(); ++i )
		{
			CFFNavArea *cand = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
			if ( !cand )
				continue;
			const float d = cand->GetIncursionDistance( team );
			if ( d > bestDist )
			{
				bestDist = d;
				next = cand;
			}
		}
	}
	return next;
}

//-----------------------------------------------------------------------------
void CFFNavArea::CollectPriorIncursionAreas( int team, CUtlVector< CFFNavArea * > *outVector )
{
	if ( !outVector )
		return;
	outVector->RemoveAll();

	const float myDist = GetIncursionDistance( team );
	for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
	{
		const NavConnectVector *adj = GetAdjacentAreas( (NavDirType)dir );
		if ( !adj )
			continue;
		for ( int i = 0; i < adj->Count(); ++i )
		{
			CFFNavArea *cand = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
			if ( cand && cand->GetIncursionDistance( team ) < myDist )
				outVector->AddToTail( cand );
		}
	}
}

void CFFNavArea::CollectNextIncursionAreas( int team, CUtlVector< CFFNavArea * > *outVector )
{
	if ( !outVector )
		return;
	outVector->RemoveAll();

	const float myDist = GetIncursionDistance( team );
	for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
	{
		const NavConnectVector *adj = GetAdjacentAreas( (NavDirType)dir );
		if ( !adj )
			continue;
		for ( int i = 0; i < adj->Count(); ++i )
		{
			CFFNavArea *cand = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
			if ( cand && cand->GetIncursionDistance( team ) > myDist )
				outVector->AddToTail( cand );
		}
	}
}

//-----------------------------------------------------------------------------
const CUtlVector< CFFNavArea * > &CFFNavArea::GetEnemyInvasionAreaVector( int myTeam ) const
{
	static const CUtlVector< CFFNavArea * > empty;
	const int idx = FF_NAV_TEAM_INDEX( myTeam );
	if ( idx < 0 || idx >= FF_NAV_TEAM_COUNT )
		return empty;
	return m_invasionAreaVector[ idx ];
}

//-----------------------------------------------------------------------------
bool CFFNavArea::IsAwayFromInvasionAreas( int myTeam, float safetyRange ) const
{
	const CUtlVector< CFFNavArea * > &invasion = GetEnemyInvasionAreaVector( myTeam );
	const float safeRangeSq = safetyRange * safetyRange;
	for ( int i = 0; i < invasion.Count(); ++i )
	{
		CFFNavArea *area = invasion[ i ];
		if ( area && ( area->GetCenter() - GetCenter() ).LengthSqr() < safeRangeSq )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Compute m_invasionAreaVector from incursion distances.
//
// For each of MY teams, collect adjacent areas that the ENEMY (any non-MY
// team) would reach with a LOWER incursion distance than this area — i.e.,
// the enemy passes through them on the way here. That's the invasion vector.
//
// FF has no PVS visibility data, so we don't filter by visibility (TFBot
// does). Without the filter we over-collect somewhat — every "rising terrain"
// neighbor counts. For "look toward enemy approach" it's still correct
// directionally.
//-----------------------------------------------------------------------------
void CFFNavArea::ComputeInvasionAreaVectors( void )
{
	for ( int i = 0; i < FF_NAV_TEAM_COUNT; ++i )
		m_invasionAreaVector[ i ].RemoveAll();

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
				const int myIdx = FF_NAV_TEAM_INDEX( myTeam );
				for ( int enemyTeam = TEAM_BLUE; enemyTeam <= TEAM_GREEN; ++enemyTeam )
				{
					if ( enemyTeam == myTeam )
						continue;

					const int enemyIdx = FF_NAV_TEAM_INDEX( enemyTeam );
					const float myInc  = m_distanceFromSpawnRoom[ enemyIdx ];
					const float adjInc = neighbor->m_distanceFromSpawnRoom[ enemyIdx ];
					if ( myInc < 0.0f || adjInc < 0.0f )
						continue;

					// Neighbor reachable by enemy with shorter travel
					// distance — they pass through it on the way here.
					if ( adjInc < myInc - 1.0f )
					{
						if ( m_invasionAreaVector[ myIdx ].Find( neighbor ) == m_invasionAreaVector[ myIdx ].InvalidIndex() )
							m_invasionAreaVector[ myIdx ].AddToTail( neighbor );
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Combat intensity. OnCombat() spikes the score; GetCombatIntensity reads
// with exponential-style decay (linear with elapsed time clamped at 0).
// Mirrors CTFNavArea's OnCombat / GetCombatIntensity.
//-----------------------------------------------------------------------------
void CFFNavArea::OnCombat( void )
{
	m_combatIntensity += ff_nav_combat_build_rate.GetFloat();
	if ( m_combatIntensity > 1.0f )
		m_combatIntensity = 1.0f;
	m_combatTimer.Start();
}

float CFFNavArea::GetCombatIntensity( void ) const
{
	if ( !m_combatTimer.HasStarted() )
		return 0.0f;

	const float decayed = m_combatIntensity -
		m_combatTimer.GetElapsedTime() * ff_nav_combat_decay_rate.GetFloat();
	return ( decayed < 0.0f ) ? 0.0f : decayed;
}

bool CFFNavArea::IsInCombat( void ) const
{
	return GetCombatIntensity() > 0.01f;
}
