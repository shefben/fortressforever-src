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
	// returns true. Default radius is the flag-room only, NOT the whole
	// half-of-map: 1200u was too aggressive and pulled every offensive
	// bot home as soon as one enemy crossed midfield.
	bool IsOwnFlagThreatened( int myTeam, float radius = 600.0f );

	// True iff any enemy is approaching our flag — within `radius` AND
	// either has line of sight to it or is moving toward it. Stricter
	// than IsOwnFlagThreatened; used to gate "stop offense, run home" so
	// only a real incoming push trips it, not an enemy passing through
	// our half. Returns the closest such enemy in *out (optional).
	bool IsEnemyApproachingOwnFlag( int myTeam, float radius,
	                                CFFPlayer **out = NULL );

	// Find an enemy player currently carrying our flag. Returns NULL if
	// no enemy is carrying it (flag at home, dropped, or carried by us
	// somehow). Used to switch from "defend the empty pedestal" to
	// "intercept the carrier" when the flag is stolen.
	CFFPlayer *FindEnemyCarryingOurFlag( int myTeam );

	// Count alive friendly engineers / snipers / hwguys on a team —
	// used by the CTF objective to enforce single-defender quotas.
	int CountAliveOnTeam( int myTeam, int classSlot );

	// Find the alive engineer on `myTeam` who is closest to `pos`. Used
	// to pick the "home" engineer (others go offense) without needing
	// a true squad-assignment system. NULL if no engineer alive.
	CFFPlayer *FindClosestAliveEngineer( int myTeam, const Vector &pos );

	// True iff any friendly sentry gun (built or building) exists within
	// `radius` of `pos`. Used to keep additional engineers from piling
	// onto an existing nest.
	bool IsFriendlySentryNear( int myTeam, const Vector &pos, float radius );

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
