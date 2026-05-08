//========= Fortress Forever Bot =============================================//
//
// FFBotPersistence — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_persistence.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_info_script.h"
#include "ff_player.h"
#include "nav_mesh.h"
#include "filesystem.h"
#include "entitylist.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
#define FFNAV_MAGIC				MAKEID( 'F', 'F', 'N', 'V' )
#define FFNAV_VERSION			3	// bumped when struct or tag bits change

struct FFNavHeader
{
	unsigned int magic;
	unsigned int version;
	unsigned int entryCount;
};

struct FFNavEntry
{
	unsigned int   areaID;
	unsigned int   tags;
	unsigned short classMask;
};


//-----------------------------------------------------------------------------
// Detect the active game mode from entity composition. Lets sidecar files
// be mode-specific (sniper hints + sentry placement may differ between
// CTF and Hunted on the same geometry).
//-----------------------------------------------------------------------------
static const char *DetectGameMode( void )
{
	int flags = 0, caps = 0, vipGoals = 0;
	bool hasCivilian = false;

	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		const int gt = s->GetBotGoalType();
		if ( gt == Omnibot::kFlag )			++flags;
		else if ( gt == Omnibot::kFlagCap )	++caps;
		else if ( gt == Omnibot::kHuntedEscape ) ++vipGoals;
	}

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if ( pp && pp->IsAlive() && pp->GetClassSlot() == CLASS_CIVILIAN )
		{
			hasCivilian = true;
			break;
		}
	}

	if ( vipGoals > 0 || hasCivilian )	return "hunted";
	if ( flags > 0 && caps > 0 )		return "ctf";
	if ( caps > 0 )						return "avd";
	return "dm";
}


static void GetSidecarPath( char *buf, int bufSize, bool useModeSpecific )
{
	const char *mapName = STRING( gpGlobals->mapname );
	if ( !mapName || !mapName[ 0 ] )
	{
		buf[ 0 ] = '\0';
		return;
	}
	if ( useModeSpecific )
	{
		Q_snprintf( buf, bufSize, "maps/%s_%s.ffnav", mapName, DetectGameMode() );
	}
	else
	{
		Q_snprintf( buf, bufSize, "maps/%s.ffnav", mapName );
	}
}


//-----------------------------------------------------------------------------
static bool LoadFromPath( CFFNavMesh *mesh, const char *path )
{
	CUtlBuffer buf;
	if ( !filesystem->ReadFile( path, "GAME", buf ) )
		return false;

	if ( buf.Size() < (int)sizeof( FFNavHeader ) )
		return false;

	FFNavHeader header;
	buf.Get( &header, sizeof( header ) );
	if ( header.magic != FFNAV_MAGIC )
	{
		Msg( "[FFNav] '%s' has bad magic — re-inference required.\n", path );
		return false;
	}
	if ( header.version != FFNAV_VERSION )
	{
		Msg( "[FFNav] '%s' is version %u (current %u) — re-inference required.\n",
			path, header.version, FFNAV_VERSION );
		return false;
	}

	int applied = 0;
	for ( unsigned int i = 0; i < header.entryCount; ++i )
	{
		if ( buf.GetBytesRemaining() < (int)sizeof( FFNavEntry ) )
			break;
		FFNavEntry entry;
		buf.Get( &entry, sizeof( entry ) );

		CNavArea *navArea = TheNavMesh->GetNavAreaByID( entry.areaID );
		if ( !navArea )
			continue;
		CFFNavArea *area = static_cast< CFFNavArea * >( navArea );
		area->AddFFTag( entry.tags );
		if ( entry.classMask != 0 )
			area->SetClassMask( entry.classMask );
		++applied;
	}

	Msg( "[FFNav] Loaded %d tagged areas from '%s'.\n", applied, path );
	return true;
}


bool FFBotPersistence::Load( CFFNavMesh *mesh )
{
	if ( !mesh || !mesh->IsLoaded() )
		return false;

	// Try mode-specific sidecar first (e.g., ff_2fort_ctf.ffnav). This
	// lets the same geometry have different hint sets per mode without
	// stepping on each other.
	char path[ 256 ];
	GetSidecarPath( path, sizeof( path ), true );
	if ( path[ 0 ] != '\0' && LoadFromPath( mesh, path ) )
		return true;

	// Fall back to mode-agnostic file.
	GetSidecarPath( path, sizeof( path ), false );
	if ( path[ 0 ] == '\0' )
		return false;

	CUtlBuffer buf;
	if ( !filesystem->ReadFile( path, "GAME", buf ) )
		return false;

	if ( buf.Size() < (int)sizeof( FFNavHeader ) )
		return false;

	FFNavHeader header;
	buf.Get( &header, sizeof( header ) );
	if ( header.magic != FFNAV_MAGIC )
	{
		Msg( "[FFNav] '%s' has bad magic — re-inference required.\n", path );
		return false;
	}
	if ( header.version != FFNAV_VERSION )
	{
		Msg( "[FFNav] '%s' is version %u (current %u) — re-inference required.\n",
			path, header.version, FFNAV_VERSION );
		return false;
	}

	int applied = 0;
	for ( unsigned int i = 0; i < header.entryCount; ++i )
	{
		if ( buf.GetBytesRemaining() < (int)sizeof( FFNavEntry ) )
			break;
		FFNavEntry entry;
		buf.Get( &entry, sizeof( entry ) );

		CNavArea *navArea = TheNavMesh->GetNavAreaByID( entry.areaID );
		if ( !navArea )
			continue;
		CFFNavArea *area = static_cast< CFFNavArea * >( navArea );
		// Or-in tags rather than overwrite, so anything the live tagger
		// already set (spawn/flag from entities) is preserved.
		area->AddFFTag( entry.tags );
		if ( entry.classMask != 0 )
			area->SetClassMask( entry.classMask );
		++applied;
	}

	Msg( "[FFNav] Loaded %d tagged areas from '%s'.\n", applied, path );
	return true;
}


//-----------------------------------------------------------------------------
bool FFBotPersistence::Save( CFFNavMesh *mesh )
{
	if ( !mesh || !mesh->IsLoaded() )
		return false;

	// Save mode-specific so per-mode tunings don't collide.
	char path[ 256 ];
	GetSidecarPath( path, sizeof( path ), true );
	if ( path[ 0 ] == '\0' )
		return false;

	// Collect tagged entries.
	CUtlVector< FFNavEntry > entries;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		const unsigned int tags = (unsigned int)area->GetFFTags();
		const unsigned short cm = area->GetClassMask();
		// Skip entries with neither tag nor class restriction. Saves space
		// and load time on big maps.
		if ( tags == 0 && cm == 0 )
			continue;
		// Strip transient runtime-only tags before persisting.
		const unsigned int persistableTags = tags & ~( FF_NAV_MANCANNON | FF_NAV_INTERCEPT_LANE );
		if ( persistableTags == 0 && cm == 0 )
			continue;
		FFNavEntry e;
		e.areaID = area->GetID();
		e.tags = persistableTags;
		e.classMask = cm;
		entries.AddToTail( e );
	}

	CUtlBuffer buf;
	FFNavHeader header;
	header.magic = FFNAV_MAGIC;
	header.version = FFNAV_VERSION;
	header.entryCount = entries.Count();
	buf.Put( &header, sizeof( header ) );
	for ( int i = 0; i < entries.Count(); ++i )
		buf.Put( &entries[ i ], sizeof( FFNavEntry ) );

	FileHandle_t fh = filesystem->Open( path, "wb", "GAME" );
	if ( fh == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "[FFNav] Could not open '%s' for writing.\n", path );
		return false;
	}
	filesystem->Write( buf.Base(), buf.TellPut(), fh );
	filesystem->Close( fh );

	Msg( "[FFNav] Saved %d tagged areas to '%s'.\n", entries.Count(), path );
	return true;
}
