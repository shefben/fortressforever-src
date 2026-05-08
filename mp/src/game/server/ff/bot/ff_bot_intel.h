//========= Fortress Forever Bot =============================================//
//
// FFBotIntel — squad-level intelligence shared across all bots on a team:
//
//   - Team alert cache: when a bot spots an enemy aiming at it, the enemy's
//     position is broadcast to teammates so other bots converge.
//   - Sound LKP feed: incoming footstep / weapon-fire sound positions (from
//     CFFBotMainAction::OnSound) are pushed in here as transient threat
//     hints; defenders pre-aim at the freshest hint.
//   - Score-aware aggression: read team scores, return a multiplier that
//     adjusts per-bot retreat/push thresholds. Losing teams push harder.
//   - Adaptive role reassignment: at respawn, decide whether the bot's
//     current class still matches team needs (no medic / no engineer →
//     swap one bot in).
//
//===========================================================================//

#ifndef FF_BOT_INTEL_H
#define FF_BOT_INTEL_H
#ifdef _WIN32
#pragma once
#endif

class CFFBot;
class CFFPlayer;

namespace FFBotIntel
{
	// ---- Team alert cache ----------------------------------------------
	// Push an alert at `pos` for `team` (= the team that should react).
	// Sources: a teammate spotted an enemy aiming at them; a footstep was
	// heard at this location; the cap entity took damage. All collapse to
	// "we have a recent enemy hint at <pos>".
	void   PushTeamAlert( int team, const Vector &pos );

	// Get the most recent alert for `team` if any is still fresh (within
	// `maxAge` seconds). Returns true and writes outPos / outAge on hit.
	bool   GetFreshTeamAlert( int team, float maxAge, Vector *outPos, float *outAge );

	// ---- Score-aware aggression ----------------------------------------
	// Returns a multiplier in [0.5, 1.5] for retreat / push thresholds.
	// > 1 = team is losing badly (push harder, retreat less).
	// < 1 = team is winning comfortably (more cautious / defensive).
	// 1.0 = scores even.
	float  GetTeamAggression( int team );

	// ---- Adaptive role reassignment ------------------------------------
	// Decide whether this bot's class should be swapped at respawn. Returns
	// the new class slot, or `currentClass` if no change.
	int    PickRespawnClass( CFFBot *me, int currentClass );

	// ---- Per-frame manager hook ----------------------------------------
	// Called from FFBotManager_Tick. Decays old alerts, polls flag state
	// transitions for "your flag was just stolen" reactions.
	void   Tick( void );
}

#endif // FF_BOT_INTEL_H
