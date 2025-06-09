//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_attack.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"

// Local bot utility headers
#include "../bot_constants.h"
#include "../bot_profile.h"
#include "../bot_util.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
void AttackState::OnEnter( CFFBot *me )
{
	if (!me || !me->GetGameState()) return;

	CFFPlayer *enemy = ToFFPlayer(me->GetBotEnemy()); // Ensure it's CFFPlayer

	me->PushPostureContext();
	me->DestroyPath();

	if (enemy && me->IsUsingKnife() && !me->IsPlayerFacingMe( enemy ))
		me->Walk();
	else
		me->Run();

	me->GetOffLadder();
	me->ResetStuckMonitor();

	m_repathTimer.Invalidate();
	m_haveSeenEnemy = me->IsEnemyVisible();
	m_nextDodgeStateTimestamp = 0.0f;
	m_firstDodge = true;
	m_isEnemyHidden = false;
	m_reacquireTimestamp = 0.0f;
	m_pinnedDownTimestamp = gpGlobals->curtime + RandomFloat( 7.0f, 10.0f );
	m_shieldToggleTimestamp = gpGlobals->curtime + RandomFloat( 2.0f, 10.0f );
	m_shieldForceOpen = false;

	if (me->IsUsingKnife())
	{
		m_crouchAndHold = false;
		me->StandUp();
	}
	else if (me->CanSeeSniper() && !me->IsSniper())
	{
		m_crouchAndHold = false;
		me->StandUp();
	}
	else
	{
		if (!m_crouchAndHold)
		{
			if (enemy && me->GetProfile())
			{
				const float crouchFarRange = 750.0f;
				float crouchChance;
				if (me->IsUsingSniperRifle()) crouchChance = 50.0f;
				else if ((GetCentroid( me ) - GetCentroid( enemy )).IsLengthGreaterThan( crouchFarRange )) crouchChance = 50.0f;
				else crouchChance = 20.0f * (1.0f - me->GetProfile()->GetAggression());
				if (RandomFloat( 0.0f, 100.0f ) < crouchChance)
				{
					trace_t result; Vector origin = GetCentroid( me );
					if (!me->IsCrouching()) origin.z -= 20.0f;
					UTIL_TraceLine( origin, enemy->EyePosition(), MASK_PLAYERSOLID, me, COLLISION_GROUP_NONE, &result );
					if (result.fraction == 1.0f) m_crouchAndHold = true;
				}
			}
		}
		if (m_crouchAndHold) { me->Crouch(); me->PrintIfWatched("Crouch and hold attack!\n" ); }
	}

	m_scopeTimestamp = 0;
	m_didAmbushCheck = false;

	if (me->GetProfile()) {
		float skill = me->GetProfile()->GetSkill();
		float dodgeChance = 80.0f * skill;
		if (skill > 0.5f && (me->IsOutnumbered() || me->CanSeeSniper())) dodgeChance = 100.0f;
		m_shouldDodge = (RandomFloat( 0, 100 ) <= dodgeChance);
		m_isCoward = (RandomFloat( 0, 100 ) > 100.0f * me->GetProfile()->GetAggression());
	} else {
		m_shouldDodge = false;
		m_isCoward = true;
	}

    // VIP Assassin specific behavior adjustments
    if (me->GetTask() == BOT_TASK_ASSASSINATE_VIP_FF && enemy && enemy == me->GetGameState()->GetVIP()) // Use global BotTaskType
    {
        me->PrintIfWatched("AttackState (Assassin): Engaging VIP %s! Adjusting combat style.\n", enemy->GetPlayerName());
        // TODO_FF: Make these actual behavior modifiers if desired
        // m_isCoward = false; // Be less likely to retreat
        // m_shouldDodge = false; // Focus more on offense than evasion (or use specific dodge patterns)
        // SetCrouchAndHold(false); // Stay mobile unless tactically sound
        me->PrintIfWatched("AttackState (Assassin): Forcing more aggressive stance (less cowardice, less general dodging, stand up).\n");
    }
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::StopAttacking( CFFBot *me ) { /* ... (implementation unchanged) ... */ }
//--------------------------------------------------------------------------------------------------------------
void AttackState::Dodge( CFFBot *me ) { /* ... (implementation unchanged) ... */ }
//--------------------------------------------------------------------------------------------------------------
void AttackState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetProfile() || !TheFFBots() || !me->GetGameState()) return;

	me->ResetStuckMonitor();

	CFFPlayer *enemy = ToFFPlayer(me->GetBotEnemy()); // Ensure CFFPlayer type
	if (enemy == NULL) { StopAttacking( me ); return; }

    BotTaskType currentTask = me->GetTask();
    FFGameState *gameState = me->GetGameState();

    // --- VIP Assassin Logic ---
    if (currentTask == BOT_TASK_ASSASSINATE_VIP_FF) // Use global BotTaskType
    {
        CFFPlayer* pVIP = gameState->GetVIP();
        if (!pVIP || !gameState->IsVIPAlive() || enemy != pVIP) {
            // VIP is dead, invalid, or our current enemy is no longer the VIP
            me->PrintIfWatched("AttackState (Assassin): VIP target %s is invalid/dead/escaped or not current enemy. Idling to re-evaluate.\n", pVIP ? pVIP->GetPlayerName() : "UNKNOWN");
            StopAttacking(me); // Stops attack state specific actions
            me->Idle();        // Re-evaluate overall objectives
            return;
        }

        if (!me->IsEnemyVisible()) // VIP (our enemy) is hidden
        {
            float notSeenEnemyTime = gpGlobals->curtime - me->GetLastSawEnemyTimestamp();
            me->PrintIfWatched("AttackState (Assassin): VIP %s hidden for %.2f. Moving to last known or hunting.\n", enemy->GetPlayerName(), notSeenEnemyTime);
            // Aggressively pursue the VIP
            me->MoveTo(me->GetLastKnownEnemyPosition(), FASTEST_ROUTE);
            // Note: MoveTo will transition state. If already in MoveTo, it will update path.
            // If the bot is already successfully pathing via a previous MoveTo from HuntState,
            // this call might be redundant unless we want to force re-pathing.
            // For now, this ensures pursuit if visibility is lost mid-attack.
            return;
        }
        // TODO_FF: If other enemies appear, should the assassin ignore them more than usual to focus on VIP?
        // This might involve modifying how LookForEnemies works when task is ASSASSINATE_VIP_FF
        // or by adding a check here: if (me->GetBotEnemy() != pVIP) me->SetBotEnemy(pVIP);
    }
    // --- End VIP Assassin Logic ---


	Vector myOrigin = GetCentroid( me );
	Vector enemyOrigin = GetCentroid( enemy );
	if (!m_haveSeenEnemy) m_haveSeenEnemy = me->IsEnemyVisible();

	if (m_retreatTimer.IsElapsed()) { /* ... (retreat logic unchanged) ... */ }

	if (me->IsAwareOfEnemyDeath()) { /* ... (enemy death logic unchanged) ... */ }

	float notSeenEnemyTime = gpGlobals->curtime - me->GetLastSawEnemyTimestamp();
	if (!me->IsEnemyVisible())
	{
		if (notSeenEnemyTime > 0.5f && me->CanHearNearbyEnemyGunfire()) { /* ... */ }
		if (notSeenEnemyTime > 0.25f) m_isEnemyHidden = true;
		if (notSeenEnemyTime > 0.1f)
		{
			if (me->GetDisposition() == CFFBot::ENGAGE_AND_INVESTIGATE) { /* ... (ambush check) ... */ }
			else { StopAttacking( me ); return; }
		}
	}
	else { m_didAmbushCheck = false; if (m_isEnemyHidden) { m_reacquireTimestamp = gpGlobals->curtime + me->GetProfile()->GetReactionTime(); m_isEnemyHidden = false; } }

	float chaseTime = 2.0f + 2.0f * (1.0f - me->GetProfile()->GetAggression());
	if (me->IsUsingSniperRifle()) chaseTime += 3.0f; else if (me->IsCrouching()) chaseTime += 1.0f;

	if (!me->IsEnemyVisible() && (notSeenEnemyTime > chaseTime || !m_haveSeenEnemy))
	{
        // If not assassinating VIP, or if VIP is the one lost, then do this.
        // If assassinating VIP and VIP is lost, the specific VIP logic at the start of OnUpdate should handle it.
        if (currentTask != BOT_TASK_ASSASSINATE_VIP_FF || enemy == gameState->GetVIP()) { // Use global BotTaskType
		    if (me->GetTask() == BOT_TASK_SNIPING) { StopAttacking( me ); return; } // Use global BotTaskType
		    else { me->SetTask( BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION, enemy ); me->MoveTo( me->GetLastKnownEnemyPosition() ); return; } // Use global BotTaskType
        }
	}

	if (!me->IsEnemyVisible() && me->GetTimeSinceAttacked() < 3.0f && me->GetAttacker() && me->GetAttacker() != me->GetBotEnemy())
	{
        // If assassinating VIP, generally stick to VIP unless direct self-preservation dictates otherwise.
        // For now, allow switching if attacked by someone else and VIP isn't visible.
		if (me->IsVisible( me->GetAttacker(), true )) { me->Attack( me->GetAttacker() ); me->PrintIfWatched("Switching targets to retaliate against new attacker!\n" ); }
		return;
	}

	if (gpGlobals->curtime > m_reacquireTimestamp) me->FireWeaponAtEnemy();
	Dodge( me );
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::OnExit( CFFBot *me ) { /* ... (implementation unchanged) ... */ }

[end of mp/src/game/server/ff/bot/states/ff_bot_state_attack.cpp]
