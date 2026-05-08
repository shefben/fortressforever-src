//========= Fortress Forever Bot =============================================//
//
// CFFDoorLinkRegistry — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_door_link.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "nav_mesh.h"
#include "entitylist.h"
#include "entityoutput.h"
#include "datamap.h"
#include "tier1/strtools.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Door classes that can be one-way. func_button is included because some maps
// use a button on each door panel (rare but exists).
//-----------------------------------------------------------------------------
static bool IsDoorClass( const char *classname )
{
	return FStrEq( classname, "func_door" )
		|| FStrEq( classname, "func_door_rotating" )
		|| FStrEq( classname, "func_movelinear" );
}

//-----------------------------------------------------------------------------
// Trigger / button classes whose touch outputs we follow.
//-----------------------------------------------------------------------------
static bool IsTriggerClass( const char *classname )
{
	return FStrEq( classname, "trigger_multiple" )
		|| FStrEq( classname, "trigger_once" )
		|| FStrEq( classname, "trigger_player_movement" )
		|| FStrEq( classname, "func_button" );
}

//-----------------------------------------------------------------------------
// Output names that fire when a player enters / presses the trigger entity.
//-----------------------------------------------------------------------------
static bool IsTouchOutputName( const char *externalName )
{
	if ( !externalName )
		return false;
	return FStrEq( externalName, "OnStartTouch" )
		|| FStrEq( externalName, "OnTouching" )
		|| FStrEq( externalName, "OnTrigger" )
		|| FStrEq( externalName, "OnPressed" )
		|| FStrEq( externalName, "OnUseLocked" )
		|| FStrEq( externalName, "OnIn" );
}

//-----------------------------------------------------------------------------
// Door inputs that cause the door to start opening.
//-----------------------------------------------------------------------------
static bool IsOpenInputName( const char *inputName )
{
	if ( !inputName )
		return false;
	return FStrEq( inputName, "Open" )
		|| FStrEq( inputName, "Toggle" )
		|| FStrEq( inputName, "Use" )
		|| FStrEq( inputName, "Unlock" );
}


//-----------------------------------------------------------------------------
// Walk an entity's datadesc for FIELD_OUTPUT entries whose external name is a
// touch-style output. Pushes (input-target name, input-name) pairs onto out.
//-----------------------------------------------------------------------------
struct OutputAction
{
	string_t targetName;
	string_t inputName;
};

static void CollectTouchOutputs( CBaseEntity *trigger, CUtlVector< OutputAction > &out )
{
	for ( datamap_t *dmap = trigger->GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap )
	{
		for ( int i = 0; i < dmap->dataNumFields; ++i )
		{
			const typedescription_t *td = &dmap->dataDesc[ i ];
			if ( td->fieldType != FIELD_CUSTOM )
				continue;
			if ( ( td->flags & FTYPEDESC_OUTPUT ) == 0 )
				continue;
			if ( !IsTouchOutputName( td->externalName ) )
				continue;

			CBaseEntityOutput *output = (CBaseEntityOutput *)( (intp)trigger + (intp)td->fieldOffset[ 0 ] );
			for ( CEventAction *ev = output->FirstAction(); ev != NULL; ev = ev->m_pNext )
			{
				if ( !IsOpenInputName( STRING( ev->m_iTargetInput ) ) )
					continue;
				OutputAction a;
				a.targetName = ev->m_iTarget;
				a.inputName  = ev->m_iTargetInput;
				out.AddToTail( a );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Per-door record: list of trigger entities that activate it.
//-----------------------------------------------------------------------------
struct DoorTriggerSet
{
	EHANDLE                    door;
	CUtlVector< Vector >       triggerCenters;	// world-space center of each activating trigger
};


//-----------------------------------------------------------------------------
// Find every (trigger, door) activation pair on the map.
//-----------------------------------------------------------------------------
static void GatherDoorTriggerPairs( CUtlVector< DoorTriggerSet > &out )
{
	out.RemoveAll();

	// First, walk all triggers and record the door they fire.
	CBaseEntity *trigger = gEntList.FirstEnt();
	while ( trigger != NULL )
	{
		const char *trigClass = trigger->GetClassname();
		if ( trigClass && IsTriggerClass( trigClass ) )
		{
			CUtlVector< OutputAction > actions;
			CollectTouchOutputs( trigger, actions );

			for ( int a = 0; a < actions.Count(); ++a )
			{
				const char *targetName = STRING( actions[ a ].targetName );
				if ( !targetName || !targetName[ 0 ] )
					continue;

				// A target name can match multiple entities (e.g. "spawn_door").
				CBaseEntity *target = NULL;
				while ( ( target = gEntList.FindEntityByName( target, targetName ) ) != NULL )
				{
					const char *targetClass = target->GetClassname();
					if ( !targetClass || !IsDoorClass( targetClass ) )
						continue;
					if ( target == trigger )
						continue;	// self-trigger — not a door we care about

					// Find existing entry for this door, or create one.
					int idx = -1;
					for ( int i = 0; i < out.Count(); ++i )
					{
						if ( out[ i ].door.Get() == target )
						{
							idx = i;
							break;
						}
					}
					if ( idx == -1 )
					{
						idx = out.AddToTail();
						out[ idx ].door.Set( target );
					}

					out[ idx ].triggerCenters.AddToTail( trigger->WorldSpaceCenter() );
				}
			}
		}
		trigger = gEntList.NextEnt( trigger );
	}
}


//-----------------------------------------------------------------------------
// Process one door + its triggers into blocked-edge entries.
//-----------------------------------------------------------------------------
static int ProcessDoor(
	CBaseEntity *door,
	const CUtlVector< Vector > &triggerCenters,
	CUtlVector< CFFDoorLinkRegistry::BlockedEdge > &blockedOut )
{
	if ( triggerCenters.Count() == 0 )
		return 0;

	const Vector doorCenter = door->WorldSpaceCenter();

	// Average trigger offset relative to door center. If triggers are split
	// across multiple sides of the door, the average shrinks toward zero —
	// that's a two-way door, no rule to apply.
	Vector avg = vec3_origin;
	for ( int i = 0; i < triggerCenters.Count(); ++i )
	{
		Vector d = triggerCenters[ i ] - doorCenter;
		d.z = 0.0f;	// horizontal classification only
		avg += d;
	}
	avg /= (float)triggerCenters.Count();

	// If any individual trigger is on the opposite side of the average,
	// it's a two-way door. Skip.
	const float MIN_DOMINANT_OFFSET = 24.0f;
	if ( avg.Length() < MIN_DOMINANT_OFFSET )
		return 0;

	Vector openSideDir = avg;
	openSideDir.NormalizeInPlace();

	for ( int i = 0; i < triggerCenters.Count(); ++i )
	{
		Vector d = triggerCenters[ i ] - doorCenter;
		d.z = 0.0f;
		if ( DotProduct( d, openSideDir ) < 0.0f )
			return 0;	// one trigger on opposite side — treat as two-way
	}

	// Door AABB, expanded so we catch areas that are adjacent rather than
	// exactly under the door brush.
	Vector mins = door->WorldAlignMins();
	Vector maxs = door->WorldAlignMaxs();
	mins += door->GetAbsOrigin();
	maxs += door->GetAbsOrigin();

	Extent doorExtent;
	doorExtent.lo = mins - Vector( 64.0f, 64.0f, 16.0f );
	doorExtent.hi = maxs + Vector( 64.0f, 64.0f, 16.0f );

	CUtlVector< CFFNavArea * > doorAreas;
	TheNavMesh->CollectAreasOverlappingExtent( doorExtent, &doorAreas );
	if ( doorAreas.Count() < 2 )
		return 0;	// can't form a closed-side / open-side pair

	int blockedAdded = 0;

	// For every connected pair (A -> B) of areas adjacent to the door:
	// if A is on the closed side and B on the open side, that traversal
	// requires the door to open and we're approaching from the wrong side.
	for ( int aIdx = 0; aIdx < doorAreas.Count(); ++aIdx )
	{
		CFFNavArea *A = doorAreas[ aIdx ];
		Vector toA = A->GetCenter() - doorCenter;
		toA.z = 0.0f;
		const float aProj = DotProduct( toA, openSideDir );
		if ( aProj >= 0.0f )
			continue;	// A is on the open side already

		for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
		{
			const NavConnectVector *adj = A->GetAdjacentAreas( (NavDirType)dir );
			if ( !adj )
				continue;
			for ( int j = 0; j < adj->Count(); ++j )
			{
				CFFNavArea *B = static_cast< CFFNavArea * >( ( *adj )[ j ].area );
				if ( !B )
					continue;
				// B must be one of the door-overlapping areas.
				if ( doorAreas.Find( B ) == doorAreas.InvalidIndex() )
					continue;
				Vector toB = B->GetCenter() - doorCenter;
				toB.z = 0.0f;
				if ( DotProduct( toB, openSideDir ) <= 0.0f )
					continue;	// B is also on closed side — not a door-crossing edge

				CFFDoorLinkRegistry::BlockedEdge edge;
				edge.fromID = A->GetID();
				edge.toID   = B->GetID();
				blockedOut.AddToTail( edge );
				++blockedAdded;
			}
		}
	}

	return blockedAdded;
}


//-----------------------------------------------------------------------------
CFFDoorLinkRegistry &CFFDoorLinkRegistry::Get( void )
{
	static CFFDoorLinkRegistry s_registry;
	return s_registry;
}

//-----------------------------------------------------------------------------
void CFFDoorLinkRegistry::Clear( void )
{
	m_blocked.RemoveAll();
}

//-----------------------------------------------------------------------------
void CFFDoorLinkRegistry::Build( CFFNavMesh *mesh )
{
	Clear();

	if ( !mesh || !mesh->IsLoaded() )
		return;

	CUtlVector< DoorTriggerSet > pairs;
	GatherDoorTriggerPairs( pairs );

	int doorsProcessed = 0;
	for ( int i = 0; i < pairs.Count(); ++i )
	{
		CBaseEntity *door = pairs[ i ].door.Get();
		if ( !door )
			continue;
		int n = ProcessDoor( door, pairs[ i ].triggerCenters, m_blocked );
		if ( n > 0 )
			++doorsProcessed;
	}

	Msg( "[CFFDoorLink] Analyzed %d (door,trigger) groups; %d one-way doors marked, %d blocked nav-edges.\n",
		pairs.Count(), doorsProcessed, m_blocked.Count() );
}

//-----------------------------------------------------------------------------
bool CFFDoorLinkRegistry::IsBlockedConnection( const CNavArea *fromArea, const CNavArea *toArea ) const
{
	if ( !fromArea || !toArea )
		return false;
	const unsigned int f = fromArea->GetID();
	const unsigned int t = toArea->GetID();
	for ( int i = 0; i < m_blocked.Count(); ++i )
	{
		if ( m_blocked[ i ].fromID == f && m_blocked[ i ].toID == t )
			return true;
	}
	return false;
}
