//========= Fortress Forever Bot =============================================//
//
// FFBotHelpers — small free helpers shared across the bot subsystem (CTF
// objective, class driver, autobalance manager). All zero-state, all server-
// side.
//
//===========================================================================//

#ifndef FF_BOT_HELPERS_H
#define FF_BOT_HELPERS_H
#ifdef _WIN32
#pragma once
#endif

class CFFBot;
class CFFPlayer;
class CFFInfoScript;
class Vector;

namespace FFBotHelpers
{
	// True iff `me` is currently carrying any flag entity. If `outFlag` is
	// non-NULL, the carried flag is written to it on success.
	bool IsBotCarryingFlag( CFFBot *me, CFFInfoScript **outFlag = NULL );

	// True iff `flag` belongs to `myTeam`. Identification is via the flag's
	// touch-flags bitmask, NOT GetTeamNumber() — base_ctf.lua sets the
	// "team" field on the Lua table but does not propagate it to the C++
	// CFFInfoScript::m_iTeamNum, so GetTeamNumber() returns TEAM_UNASSIGNED
	// in practice. A flag's m_BotTeamFlags carries which teams CAN touch
	// (= grab) it; one's own flag is the one whose bitmask does NOT contain
	// our team.
	bool IsBotsOwnFlag( int myTeam, CFFInfoScript *flag );

	// True iff `flag` is grabbable by `myTeam` (i.e. it's an enemy's flag).
	// Inverse of IsBotsOwnFlag for kFlag entities; only valid for kFlag.
	bool CanBotGrabFlag( int myTeam, CFFInfoScript *flag );

	// Find this team's flag (any state). Returns NULL if none / removed.
	CFFInfoScript *FindOwnFlag( int myTeam );

	// Find this team's cap point — proximity-based: closest cap to our own
	// flag, since GetTeamNumber() on caps is unreliable for the same reason
	// flags are. Returns NULL if no caps exist.
	CFFInfoScript *FindOwnCapPoint( int myTeam, const Vector &myPos );

	// Find any cap point on the map. For AvD-style maps where the cap is
	// the only objective both teams care about.
	CFFInfoScript *FindAnyCapPoint( const Vector &myPos );

	// True iff any enemy player is within `radius` of our own flag. Used
	// for reactive defense — bots only switch to "defend home" when this
	// returns true.
	bool IsOwnFlagThreatened( int myTeam, float radius = 1000.0f );

	// Find a teammate (not the asking bot) currently carrying any enemy
	// flag. Used to make escort bots follow the flag-runner instead of
	// each independently chasing the flag entity.
	CFFPlayer *FindTeammateCarryingEnemyFlag( CFFPlayer *me );

	// Find a wounded teammate within `searchRange` of `from`. Wounded means
	// health < max - 10. Returns NULL if none.
	CFFPlayer *FindWoundedTeammate( CFFPlayer *me, float searchRange );

	// Find a friendly civilian (Hunted-mode VIP) on `myTeam`. Returns NULL
	// if no civilian on this team or the civilian is dead.
	CFFPlayer *FindFriendlyCivilian( int myTeam );

	// Find the nearest alive friendly medic to `fromPos` on `myTeam`. Used
	// when an infected bot needs to seek a cure.
	CFFPlayer *FindNearestFriendlyMedic( int myTeam, const Vector &fromPos, float maxRange = 4000.0f );

	// Find a healthy teammate (not the asker) within `radius` who could
	// receive a tossed flag. Returns the closest one.
	CFFPlayer *FindTeammateForFlagToss( CFFPlayer *carrier, float radius );

	// True iff any enemy player is within `radius` of `pos`. Used to
	// gate "panic cloak" — only safe when no nearby witnesses see us
	// drop into cloak.
	bool IsAnyEnemyNear( int myTeam, const Vector &pos, float radius );
}

#endif // FF_BOT_HELPERS_H
