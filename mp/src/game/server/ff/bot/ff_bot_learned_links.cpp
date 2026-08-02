//========= Fortress Forever Bot =============================================//
//
// FFBotLearnedLinks — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_learned_links.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "filesystem.h"
#include "utlbuffer.h"
#include "utlvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_learn_links( "ff_bot_learn_links", "1", FCVAR_NONE,
	"Learn missing nav connections by watching players move. "
	"0 = off, 1 = learn and apply, 2 = learn, apply and log every commit." );


//-----------------------------------------------------------------------------
// Tuning.
//-----------------------------------------------------------------------------

// Distance between motion samples. One nav-area width is 50-100u on typical FF
// geometry, so 64 gives us at most one sample per area crossed.
#define FFLINK_SAMPLE_DISTANCE		64.0f

// A player must be moving at least this fast for their motion to count. Below
// this they may be being pushed, riding a lift, or clipping.
#define FFLINK_MIN_SPEED			100.0f

// ...and must have been moving for this long continuously. Filters out the
// instant after a teleport / respawn, where position deltas are meaningless.
#define FFLINK_MIN_MOVING_TIME		2.0f

// Reject absurd deltas. A single sample step should never exceed roughly a
// long rocket-jump; anything larger is a teleport, a trigger_push launch, or a
// lag spike, none of which represent a walkable connection.
#define FFLINK_MAX_STEP_DISTANCE	400.0f

// How many independent observations before we trust a link enough to add it to
// the live graph.
#define FFLINK_CONFIRMATIONS		3

// Sidecar format version.
//
//   1 — records were a pair of nav area IDs.
//   2 — records are a pair of world positions.
//
// The change matters. Area IDs are assigned by nav generation and mean nothing
// against a different mesh, so every link learned before a nav_generate was
// silently discarded on the next load: the exact moment the mesh changed and
// the learned repairs were most needed was the moment they all went away.
//
// World positions survive regeneration for the same reason the manual marker
// sidecar uses them. Version 1 files still load — their IDs are resolved
// against the current mesh once and re-keyed to positions on the next save, so
// an existing server upgrades without losing anything it had learned, provided
// its mesh hasn't changed in between. If it has, those links were already lost.
#define FFLINK_FILE_VERSION			2
#define FFLINK_FILE_VERSION_IDS		1
#define FFLINK_FILE_MAGIC			0x464C4E4B	// 'FLNK'

// How far from a stored position we'll accept a nav area when re-resolving.
// Generous: the whole point is that the mesh may have changed shape, and an
// area whose centre moved sixty units is still the same place.
#define FFLINK_RESOLVE_RANGE		128.0f


//-----------------------------------------------------------------------------
// A candidate or confirmed connection between two nav areas.
//
// Stored by WORLD POSITION, resolved to areas at load time. The IDs are a
// runtime cache of that resolution and are never what gets written to disk.
//-----------------------------------------------------------------------------
struct LearnedLink
{
	Vector       fromPos;
	Vector       toPos;
	unsigned int fromID;		// runtime only; resolved from fromPos
	unsigned int toID;			// runtime only; resolved from toPos
	int          confirmations;
	bool         applied;		// already pushed into the live nav graph
};

static CUtlVector< LearnedLink > s_links;


//-----------------------------------------------------------------------------
// Per-player sampling state. Indexed by entindex.
//-----------------------------------------------------------------------------
struct PlayerTrail
{
	bool         valid;
	Vector       lastSamplePos;
	unsigned int lastAreaID;
	float        movingSince;	// 0 = not currently moving
};

static PlayerTrail s_trails[ MAX_PLAYERS + 1 ];

static bool  s_dirty = false;
static float s_nextSaveTime = 0.0f;


//-----------------------------------------------------------------------------
static void GetLinkFilename( char *out, int outSize )
{
	Q_snprintf( out, outSize, "maps/%s.ffnavlinks", STRING( gpGlobals->mapname ) );
}


//-----------------------------------------------------------------------------
static LearnedLink *FindLink( unsigned int fromID, unsigned int toID )
{
	for ( int i = 0; i < s_links.Count(); ++i )
	{
		if ( s_links[ i ].fromID == fromID && s_links[ i ].toID == toID )
			return &s_links[ i ];
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Push one confirmed link into the live nav graph.
//
// Direction is derived from the relative position of the two area centres.
// CNavArea::ConnectTo takes a NavDirType because the adjacency lists are
// bucketed by compass direction; the actual pathfinding only walks those
// buckets, so any consistent choice works as long as it matches how the
// geometry lies.
//-----------------------------------------------------------------------------
static bool ApplyLink( LearnedLink &link )
{
	if ( link.applied )
		return true;

	if ( !TheNavMesh )
		return false;

	// IDs first when we have them — they're exact, and within a session they're
	// how the link was recorded. Positions are the fallback and the persistent
	// key: a link loaded from disk has no valid ID until this resolves one.
	CNavArea *from = link.fromID ? TheNavMesh->GetNavAreaByID( link.fromID ) : NULL;
	CNavArea *to   = link.toID   ? TheNavMesh->GetNavAreaByID( link.toID )   : NULL;

	if ( !from )
	{
		from = TheNavMesh->GetNearestNavArea( link.fromPos, false,
			FFLINK_RESOLVE_RANGE, false, true, TEAM_ANY );
		if ( from )
			link.fromID = from->GetID();
	}

	if ( !to )
	{
		to = TheNavMesh->GetNearestNavArea( link.toPos, false,
			FFLINK_RESOLVE_RANGE, false, true, TEAM_ANY );
		if ( to )
			link.toID = to->GetID();
	}

	if ( !from || !to || from == to )
		return false;

	Vector delta = to->GetCenter() - from->GetCenter();

	NavDirType dir;
	if ( fabsf( delta.x ) > fabsf( delta.y ) )
		dir = ( delta.x > 0.0f ) ? EAST : WEST;
	else
		dir = ( delta.y > 0.0f ) ? SOUTH : NORTH;

	// Already connected some other way? Nothing to do, and we should stop
	// carrying the record around.
	for ( int d = 0; d < NUM_DIRECTIONS; ++d )
	{
		if ( from->IsConnected( to, (NavDirType)d ) )
		{
			link.applied = true;
			return true;
		}
	}

	from->ConnectTo( to, dir );
	link.applied = true;

	if ( ff_bot_learn_links.GetInt() >= 2 )
	{
		Msg( "[FFBotLearnedLinks] connected area %u -> %u (dir %d)\n",
			link.fromID, link.toID, (int)dir );
	}
	return true;
}


//-----------------------------------------------------------------------------
// Record one observed traversal. Returns true if this promoted a candidate to
// a live connection.
//-----------------------------------------------------------------------------
static bool ObserveTraversal( unsigned int fromID, unsigned int toID )
{
	if ( fromID == 0 || toID == 0 || fromID == toID )
		return false;

	CNavArea *from = TheNavMesh->GetNavAreaByID( fromID );
	CNavArea *to   = TheNavMesh->GetNavAreaByID( toID );
	if ( !from || !to )
		return false;

	// Already in the graph — the mesh is fine here, nothing to learn.
	for ( int d = 0; d < NUM_DIRECTIONS; ++d )
	{
		if ( from->IsConnected( to, (NavDirType)d ) )
			return false;
	}

	LearnedLink *link = FindLink( fromID, toID );
	if ( !link )
	{
		LearnedLink fresh;
		fresh.fromID = fromID;
		fresh.toID = toID;
		// Area centres rather than the sample positions themselves. The sample
		// is wherever a player happened to be standing; the centre is the thing
		// we want to be able to find again on a different mesh.
		fresh.fromPos = from->GetCenter();
		fresh.toPos = to->GetCenter();
		fresh.confirmations = 0;
		fresh.applied = false;
		s_links.AddToTail( fresh );
		link = &s_links[ s_links.Count() - 1 ];
	}

	if ( link->applied )
		return false;

	++link->confirmations;
	if ( link->confirmations < FFLINK_CONFIRMATIONS )
		return false;

	if ( ApplyLink( *link ) )
	{
		s_dirty = true;
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
void FFBotLearnedLinks::ForgetPlayer( CBasePlayer *player )
{
	if ( !player )
		return;
	const int idx = player->entindex();
	if ( idx < 0 || idx > MAX_PLAYERS )
		return;
	s_trails[ idx ].valid = false;
	s_trails[ idx ].movingSince = 0.0f;
}


//-----------------------------------------------------------------------------
void FFBotLearnedLinks::Update( void )
{
	if ( ff_bot_learn_links.GetInt() <= 0 )
		return;
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		PlayerTrail &trail = s_trails[ i ];

		if ( !player || !player->IsAlive() )
		{
			trail.valid = false;
			trail.movingSince = 0.0f;
			continue;
		}

		// Must be on the ground and moving under their own power. Airborne
		// samples would happily "learn" a link across a chasm the bot cannot
		// actually cross from a standing start.
		Vector vel = player->GetAbsVelocity();
		vel.z = 0.0f;
		const bool movingFastEnough = vel.LengthSqr() > ( FFLINK_MIN_SPEED * FFLINK_MIN_SPEED );
		const bool onGround = ( player->GetGroundEntity() != NULL );

		if ( !movingFastEnough || !onGround )
		{
			// Losing contact with the ground doesn't invalidate the trail — a
			// bunny hop or a small drop is exactly the kind of traversal we
			// want to learn — but standing still does.
			if ( !movingFastEnough )
			{
				trail.movingSince = 0.0f;
				trail.valid = false;
			}
			continue;
		}

		if ( trail.movingSince <= 0.0f )
		{
			trail.movingSince = gpGlobals->curtime;
			trail.valid = false;
		}

		// Settle period after starting to move, so respawn / teleport frames
		// don't produce a bogus first sample.
		if ( ( gpGlobals->curtime - trail.movingSince ) < FFLINK_MIN_MOVING_TIME )
			continue;

		const Vector pos = player->GetAbsOrigin();

		if ( !trail.valid )
		{
			CNavArea *area = TheNavMesh->GetNearestNavArea( pos, false, 128.0f, false, true, TEAM_ANY );
			if ( !area )
				continue;
			trail.valid = true;
			trail.lastSamplePos = pos;
			trail.lastAreaID = area->GetID();
			continue;
		}

		const float stepSq = ( pos - trail.lastSamplePos ).LengthSqr();
		if ( stepSq < ( FFLINK_SAMPLE_DISTANCE * FFLINK_SAMPLE_DISTANCE ) )
			continue;

		// Teleport / push / lag guard.
		if ( stepSq > ( FFLINK_MAX_STEP_DISTANCE * FFLINK_MAX_STEP_DISTANCE ) )
		{
			trail.valid = false;
			continue;
		}

		CNavArea *area = TheNavMesh->GetNearestNavArea( pos, false, 128.0f, false, true, TEAM_ANY );
		if ( !area )
		{
			trail.valid = false;
			continue;
		}

		const unsigned int areaID = area->GetID();
		if ( areaID != trail.lastAreaID )
		{
			ObserveTraversal( trail.lastAreaID, areaID );
			trail.lastAreaID = areaID;
		}

		trail.lastSamplePos = pos;
	}

	// Throttled write-back so a busy server isn't doing disk IO in bursts.
	if ( s_dirty && gpGlobals->curtime > s_nextSaveTime )
	{
		s_nextSaveTime = gpGlobals->curtime + 30.0f;
		Save();
	}
}


//-----------------------------------------------------------------------------
void FFBotLearnedLinks::Save( void )
{
	if ( s_links.Count() == 0 )
		return;

	CUtlBuffer buf( 0, 0, 0 );	// binary
	buf.PutInt( FFLINK_FILE_MAGIC );
	buf.PutInt( FFLINK_FILE_VERSION );

	// Only persist links we actually trust.
	int confirmedCount = 0;
	for ( int i = 0; i < s_links.Count(); ++i )
	{
		if ( s_links[ i ].confirmations >= FFLINK_CONFIRMATIONS )
			++confirmedCount;
	}
	buf.PutInt( confirmedCount );

	for ( int i = 0; i < s_links.Count(); ++i )
	{
		const LearnedLink &link = s_links[ i ];
		if ( link.confirmations < FFLINK_CONFIRMATIONS )
			continue;
		buf.PutFloat( link.fromPos.x );
		buf.PutFloat( link.fromPos.y );
		buf.PutFloat( link.fromPos.z );
		buf.PutFloat( link.toPos.x );
		buf.PutFloat( link.toPos.y );
		buf.PutFloat( link.toPos.z );
		buf.PutInt( link.confirmations );
	}

	char filename[ MAX_PATH ];
	GetLinkFilename( filename, sizeof( filename ) );

	if ( filesystem->WriteFile( filename, "MOD", buf ) )
	{
		s_dirty = false;
		if ( ff_bot_learn_links.GetInt() >= 2 )
			Msg( "[FFBotLearnedLinks] saved %d links to %s\n", confirmedCount, filename );
	}
	else
	{
		Warning( "[FFBotLearnedLinks] failed to write %s\n", filename );
	}
}


//-----------------------------------------------------------------------------
void FFBotLearnedLinks::OnMapLoad( void )
{
	s_links.RemoveAll();
	s_dirty = false;
	s_nextSaveTime = 0.0f;
	for ( int i = 0; i <= MAX_PLAYERS; ++i )
	{
		s_trails[ i ].valid = false;
		s_trails[ i ].movingSince = 0.0f;
		s_trails[ i ].lastAreaID = 0;
	}

	if ( ff_bot_learn_links.GetInt() <= 0 )
		return;

	char filename[ MAX_PATH ];
	GetLinkFilename( filename, sizeof( filename ) );

	CUtlBuffer buf( 0, 0, 0 );
	if ( !filesystem->ReadFile( filename, "MOD", buf ) )
		return;	// no sidecar yet — normal on a map we've never played

	if ( buf.GetInt() != FFLINK_FILE_MAGIC )
	{
		Warning( "[FFBotLearnedLinks] %s is not a link file; ignoring.\n", filename );
		return;
	}
	const int version = buf.GetInt();
	if ( version != FFLINK_FILE_VERSION && version != FFLINK_FILE_VERSION_IDS )
	{
		Msg( "[FFBotLearnedLinks] %s is version %d, expected %d; ignoring.\n",
			filename, version, FFLINK_FILE_VERSION );
		return;
	}

	const int count = buf.GetInt();
	int applied = 0;
	for ( int i = 0; i < count && buf.IsValid(); ++i )
	{
		LearnedLink link;
		link.fromID = 0;
		link.toID = 0;
		link.fromPos.Init();
		link.toPos.Init();
		link.applied = false;

		if ( version == FFLINK_FILE_VERSION_IDS )
		{
			// Legacy record: a pair of area IDs, meaningful only against the
			// mesh they were recorded on. Resolve them once against the current
			// one and take the positions from there; the next save writes this
			// link out in the durable format.
			link.fromID = buf.GetUnsignedInt();
			link.toID = buf.GetUnsignedInt();
			link.confirmations = buf.GetInt();

			CNavArea *from = TheNavMesh ? TheNavMesh->GetNavAreaByID( link.fromID ) : NULL;
			CNavArea *to   = TheNavMesh ? TheNavMesh->GetNavAreaByID( link.toID )   : NULL;
			if ( from )
				link.fromPos = from->GetCenter();
			if ( to )
				link.toPos = to->GetCenter();

			s_dirty = true;	// force a re-save in the new format
		}
		else
		{
			link.fromPos.x = buf.GetFloat();
			link.fromPos.y = buf.GetFloat();
			link.fromPos.z = buf.GetFloat();
			link.toPos.x = buf.GetFloat();
			link.toPos.y = buf.GetFloat();
			link.toPos.z = buf.GetFloat();
			link.confirmations = buf.GetInt();
		}

		if ( ApplyLink( link ) )
			++applied;

		s_links.AddToTail( link );
	}

	Msg( "[FFBotLearnedLinks] %s: %d stored (format v%d), %d applied to the nav graph.\n",
		filename, count, version, applied );

	if ( version == FFLINK_FILE_VERSION_IDS )
	{
		Msg( "[FFBotLearnedLinks] ...upgrading that file to position-keyed "
		     "records, which survive nav_generate.\n" );
	}
}


//-----------------------------------------------------------------------------
void FFBotLearnedLinks::Clear( void )
{
	// Note: we do NOT tear the connections back out of the live nav graph —
	// CNavArea::Disconnect on a running mesh mid-path would leave PathFollowers
	// holding dangling segments. The links go away on the next map load.
	const int had = s_links.Count();
	s_links.RemoveAll();
	s_dirty = false;

	char filename[ MAX_PATH ];
	GetLinkFilename( filename, sizeof( filename ) );
	filesystem->RemoveFile( filename, "MOD" );

	Msg( "[FFBotLearnedLinks] cleared %d links and removed %s. "
	     "Links already applied stay live until the next map load.\n", had, filename );
}


//-----------------------------------------------------------------------------
void FFBotLearnedLinks::PrintReport( void )
{
	int confirmed = 0;
	int pending = 0;
	for ( int i = 0; i < s_links.Count(); ++i )
	{
		if ( s_links[ i ].confirmations >= FFLINK_CONFIRMATIONS )
			++confirmed;
		else
			++pending;
	}

	Msg( "==== FF bot learned links ====\n" );
	Msg( "  enabled=%d  confirmed=%d  pending=%d  (need %d observations to confirm)\n",
		ff_bot_learn_links.GetInt(), confirmed, pending, FFLINK_CONFIRMATIONS );

	for ( int i = 0; i < s_links.Count(); ++i )
	{
		const LearnedLink &link = s_links[ i ];
		Msg( "  %5u -> %-5u  obs=%d %-6s (%.0f %.0f %.0f) -> (%.0f %.0f %.0f)\n",
			link.fromID, link.toID, link.confirmations,
			link.applied ? "[live]" : "",
			link.fromPos.x, link.fromPos.y, link.fromPos.z,
			link.toPos.x, link.toPos.y, link.toPos.z );
	}
	Msg( "==============================\n" );
}
