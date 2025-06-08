//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements the bot state for reloading their active weapon.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_reload_state.h"
#include "../ff_bot.h" // For CFFBot
#include "../ff_weapon_base.h" // For CFFWeaponBase

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const float DEFAULT_RELOAD_TIME = 2.0f; // Generic reload time, actual time is weapon-specific

//--------------------------------------------------------------------------------------------------------------
ReloadState::ReloadState(void)
{
	m_weaponToReload = NULL;
}

//--------------------------------------------------------------------------------------------------------------
const char *ReloadState::GetName( void ) const
{
	return "Reload"; // Short name for display
}

//--------------------------------------------------------------------------------------------------------------
void ReloadState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched("ReloadState: Entering state.\n");
	m_weaponToReload = me->GetActiveFFWeapon();

	if (!m_weaponToReload.Get())
	{
		me->PrintIfWatched("ReloadState: No active weapon to reload. Idling.\n");
		me->Idle();
		return;
	}

	// Conceptual checks - actual game logic handles reload possibility and duration.
	// These are for the bot's understanding and decision-making.
	bool needsReload = m_weaponToReload->Clip1() < m_weaponToReload->GetMaxClip1() && me->GetAmmoCount(m_weaponToReload->GetPrimaryAmmoType()) > 0;
	bool canReload = m_weaponToReload->CanReload(); // Assumes CFFWeaponBase::CanReload exists (likely true for most)

	if (!needsReload)
	{
		me->PrintIfWatched("ReloadState: Weapon %s does not need reload (Clip: %d/%d, Reserve: %d). Idling.\n",
			m_weaponToReload->GetClassname(), m_weaponToReload->Clip1(), m_weaponToReload->GetMaxClip1(), me->GetAmmoCount(m_weaponToReload->GetPrimaryAmmoType()));
		me->Idle();
		return;
	}

	if (!canReload)
	{
		// This case should be rare for primary/secondary weapons.
		me->PrintIfWatched("ReloadState: Weapon %s cannot be reloaded. Idling.\n", m_weaponToReload->GetClassname());
		me->Idle();
		return;
	}

	// Simulate pressing the reload button. The actual reload is handled by the game.
	me->PressButton(IN_RELOAD);
	// Some games might need a short hold, others just a press. For now, immediate release.
	// me->ReleaseButton(IN_RELOAD); // Releasing immediately might be too fast for some games.
	// Let's assume the bot holds it for a tiny duration or game handles it on press.
	// The CBasePlayer::Reload() is typically called by ItemPostFrame of the weapon if IN_RELOAD is set.

	// Estimate reload time. Actual reload completion is driven by weapon's animation and game logic.
	// CFFWeaponBase should ideally provide this.
	float reloadTime = m_weaponToReload->GetReloadTime(); // Conceptual: CFFWeaponBase::GetReloadTime()
	if (reloadTime <= 0) {
		reloadTime = DEFAULT_RELOAD_TIME; // Fallback
	}

	m_reloadDurationTimer.Start(reloadTime);
	me->PrintIfWatched("ReloadState: Reloading %s (estimated %.2f sec). Clip: %d, Reserve: %d\n",
		m_weaponToReload->GetClassname(), reloadTime, m_weaponToReload->Clip1(), me->GetAmmoCount(m_weaponToReload->GetPrimaryAmmoType()));

	me->SetTask(CFFBot::TaskType::RELOADING); // Set a task if you have one for reloading
}

//--------------------------------------------------------------------------------------------------------------
void ReloadState::OnUpdate( CFFBot *me )
{
	if (!m_weaponToReload.Get() || me->GetActiveFFWeapon() != m_weaponToReload.Get())
	{
		me->PrintIfWatched("ReloadState: Weapon changed or lost during reload. Idling.\n");
		me->Idle();
		return;
	}

	// The actual reload finishes based on game events/weapon state, not just this timer.
	// This timer is a bot-side estimation. A better check is if the weapon clip is full or ammo type is 0.
	bool isClipFull = (m_weaponToReload->Clip1() == m_weaponToReload->GetMaxClip1());
	bool noReserveAmmo = (me->GetAmmoCount(m_weaponToReload->GetPrimaryAmmoType()) == 0 && m_weaponToReload->Clip1() > 0); // Has some ammo in clip but no more reserve
	bool isEmptyAndNoReserve = (m_weaponToReload->Clip1() == 0 && me->GetAmmoCount(m_weaponToReload->GetPrimaryAmmoType()) == 0);


	if (isClipFull || noReserveAmmo || isEmptyAndNoReserve || m_reloadDurationTimer.IsElapsed())
	{
		if (isClipFull)
			me->PrintIfWatched("ReloadState: Reload complete for %s (Clip full: %d/%d).\n", m_weaponToReload->GetClassname(), m_weaponToReload->Clip1(), m_weaponToReload->GetMaxClip1());
		else if (noReserveAmmo)
			me->PrintIfWatched("ReloadState: Reload complete for %s (No reserve ammo left. Clip: %d/%d).\n", m_weaponToReload->GetClassname(), m_weaponToReload->Clip1(), m_weaponToReload->GetMaxClip1());
		else if (isEmptyAndNoReserve)
			me->PrintIfWatched("ReloadState: Reload attempted for %s but no ammo left at all.\n", m_weaponToReload->GetClassname());
		else // Timer elapsed, assume finished.
			me->PrintIfWatched("ReloadState: Reload timer elapsed for %s. Assuming complete.\n", m_weaponToReload->GetClassname());

		me->ReleaseButton(IN_RELOAD); // Ensure reload button is released if held.
		me->Idle();
		return;
	}

	// FF_TODO_AI_BEHAVIOR: If attacked while reloading, should try to cancel reload (if possible)
	// and switch to secondary/melee or flee. This is advanced. For now, bot is committed.
	// Example: if (me->GetAttacker() != NULL && me->GetTimeSinceAttacked() < 0.5f) { me->Idle(); return; }

	// Keep looking around or at last known enemy position
	me->UpdateLookAround();
	if (me->GetBotEnemy())
	{
		me->SetLookAt("Enemy during reload", me->GetBotEnemy()->EyePosition(), PRIORITY_MEDIUM);
	}
}

//--------------------------------------------------------------------------------------------------------------
void ReloadState::OnExit( CFFBot *me )
{
	me->PrintIfWatched("ReloadState: Exiting state. Was reloading %s.\n",
		m_weaponToReload.Get() ? m_weaponToReload->GetClassname() : "nothing");

	// Ensure the reload button is released if the state is exited prematurely.
	// CUserCmd already clears buttons each frame, but this is an explicit clear for bot's internal state.
	if (me->GetButtons() & IN_RELOAD)
	{
		me->ReleaseButton(IN_RELOAD);
	}

	m_weaponToReload = NULL;
	if (me->GetTask() == CFFBot::TaskType::RELOADING)
	{
		me->SetTask(CFFBot::TaskType::SEEK_AND_DESTROY); // Clear specific reload task
	}
}
