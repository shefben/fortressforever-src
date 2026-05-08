//========= Fortress Forever Bot =============================================//
//
// CFFDoorLinkRegistry — runtime analysis of one-way doors.
//
// FF spawn rooms typically have two doors: an "exit" door whose trigger sits
// inside the room (opens for outbound players) and an "entry" door whose
// trigger sits outside (opens for inbound players seeking resupply). Naive
// pathing routes the bot to either door equally, so half the time the bot
// gets stuck at a door it can't open from its current side.
//
// This module is run from CFFBotTagger at LevelInit. It walks every trigger /
// button entity, follows its m_OnStartTouch / m_OnPressed outputs to find
// which doors it activates, and from the trigger position vs door position
// infers the "open side" of each door. Nav-area connections crossing the door
// from the closed side to the open side are then blacklisted in a
// (fromAreaID -> toAreaID) registry which CFFBotPathCost queries.
//
//===========================================================================//

#ifndef FF_DOOR_LINK_H
#define FF_DOOR_LINK_H
#ifdef _WIN32
#pragma once
#endif

#include "utlvector.h"

class CNavArea;
class CFFNavMesh;

class CFFDoorLinkRegistry
{
public:
	static CFFDoorLinkRegistry &Get( void );

	// Wipe and rebuild from current entity world. Cheap (~ms on FF maps).
	void Build( CFFNavMesh *mesh );
	void Clear( void );

	// True if a path going from `fromArea` to `toArea` requires crossing a
	// door we know won't open from `fromArea`'s side.
	bool IsBlockedConnection( const CNavArea *fromArea, const CNavArea *toArea ) const;

	int  GetBlockedConnectionCount( void ) const { return m_blocked.Count(); }

	struct BlockedEdge
	{
		unsigned int fromID;
		unsigned int toID;
	};

private:
	CFFDoorLinkRegistry( void ) {}

	CUtlVector< BlockedEdge > m_blocked;
};

#endif // FF_DOOR_LINK_H
