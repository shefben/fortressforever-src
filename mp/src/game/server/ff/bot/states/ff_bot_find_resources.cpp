//========= Fortress Forever - Bot Engineer Find Resources State ============//
//
// Purpose: Implements the bot state for Engineers finding resources.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_find_resources.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "../ff_weapon_base.h"  // For AMMO_CELLS
#include "nav_area.h"
#include "ff_buildableobject.h" // For CFFDispenser (if checking type specifically)
#include "items.h" // For CItem (if ammo packs are CItem derivatives)

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern const float BUILD_REPATH_TIME; // Assuming this is accessible

//--------------------------------------------------------------------------------------------------------------
FindResourcesState::FindResourcesState(void)
{
	m_resourceTarget = NULL;
	m_searchTimer.Invalidate();
	m_repathTimer.Invalidate();
}

//--------------------------------------------------------------------------------------------------------------
const char *FindResourcesState::GetName( void ) const
{
	return "FindResources";
}

//--------------------------------------------------------------------------------------------------------------
void FindResourcesState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "FindResourcesState: Entering state.\n" );
	m_resourceTarget = NULL; // Clear any previous target

	// Try to find an initial target immediately
	m_resourceTarget = me->FindResourceSource();
	if (m_resourceTarget.Get())
	{
		me->PrintIfWatched("FindResourcesState: Initial resource target: %s\n", m_resourceTarget->GetClassname());
		if (!me->MoveTo(m_resourceTarget->GetAbsOrigin(), SAFEST_ROUTE))
		{
			me->PrintIfWatched("FindResourcesState: Unable to path to initial resource target. Will rescan.\n");
			m_resourceTarget = NULL; // Clear if unpathable
		}
		else
		{
			m_repathTimer.Start(BUILD_REPATH_TIME);
		}
	}
	else
	{
		me->PrintIfWatched("FindResourcesState: No initial resource target found.\n");
	}
	m_searchTimer.Start(RandomFloat(1.0f, 2.0f)); // Time before next global search if current target fails or none found
}

//--------------------------------------------------------------------------------------------------------------
void FindResourcesState::OnUpdate( CFFBot *me )
{
	// FF_TODO_RESOURCES: Define what "full" means. Could be GetMaxAmmoCount(AMMO_CELLS) or a gameplay defined max.
	const int MAX_CELLS_DESIRED = 200; // Example
	if (me->GetAmmoCount(AMMO_CELLS) >= MAX_CELLS_DESIRED)
	{
		me->PrintIfWatched("FindResourcesState: Resources full. Idling.\n");
		me->Idle();
		return;
	}

	// Handle enemy threats
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("FindResourcesState: Enemy detected! Assessing threat.\n");
		me->Attack(me->GetBotEnemy()); // Or flee, depending on Engineer's combat readiness
		return;
	}

	bool hadTarget = m_resourceTarget.Get() != NULL;

	// Validate current target or find new one if timer elapsed
	if (!m_resourceTarget.Get() || (m_resourceTarget.Get()->IsMarkedForDeletion()) )
	{
		m_resourceTarget = NULL; // Clear invalid target
	}

	if (!m_resourceTarget.Get() && m_searchTimer.IsElapsed())
	{
		m_resourceTarget = me->FindResourceSource();
		if (m_resourceTarget.Get())
		{
			me->PrintIfWatched("FindResourcesState: Found new resource target: %s\n", m_resourceTarget->GetClassname());
			if (!me->MoveTo(m_resourceTarget->GetAbsOrigin(), SAFEST_ROUTE))
			{
				me->PrintIfWatched("FindResourcesState: Unable to path to new resource target. Will rescan.\n");
				m_resourceTarget = NULL;
			}
			else
			{
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
		}
		else
		{
			me->PrintIfWatched("FindResourcesState: Still no resource target found after scan. Idling for now.\n");
			// Potentially wait a bit longer or engage in other tasks if resources are critically low but none found.
			me->Idle();
			return;
		}
		m_searchTimer.Start(RandomFloat(3.0f, 5.0f)); // Time before next global search
	}

	// If we have a target, move towards it / interact
	if (m_resourceTarget.Get())
	{
		// If it's an ammo pack and we're close, it should be picked up by Touch or auto-pickup logic.
		// If it's a dispenser, being nearby is usually enough.
		float distToTargetSq = me->GetAbsOrigin().DistToSqr(m_resourceTarget->GetAbsOrigin());
		float interactRadiusSq = Square(BUILD_PLACEMENT_RADIUS); // Reuse this for "close enough"

		// Special handling if target is a dispenser (just need to be near it)
		if (FClassnameIs(m_resourceTarget.Get(), "obj_dispenser"))
		{
			interactRadiusSq = Square(200.0f); // Stand a bit further from dispenser generally
		}


		if (distToTargetSq < interactRadiusSq)
		{
			me->Stop();
			me->PrintIfWatched("FindResourcesState: Near resource target %s. Waiting for resources.\n", m_resourceTarget->GetClassname());
			// FF_TODO_RESOURCES: Actual resource gain is passive (dispenser) or via Touch() (ammo pack).
			// If it's a pack that needs 'use', bot might need to press IN_USE.
			// For now, assume passive gain or Touch pickup.
			// If it was an ammo pack, it should be picked up and m_resourceTarget might become NULL via game logic.
			// If it's a dispenser, bot will just stay here until full or new orders.
			// Re-scan for a *better* or *different* source if this one seems slow or depleted (advanced)
			if(m_searchTimer.IsElapsed()) // Periodically check if there's a better source or if this one is gone
			{
			    if (hadTarget && m_resourceTarget.Get() && m_resourceTarget.Get()->IsMarkedForDeletion()) {
			        m_resourceTarget = NULL; // Force re-evaluation
			    }
				m_searchTimer.Start(RandomFloat(3.0f, 5.0f));
			}

		}
		else // Not close enough, keep moving
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				me->PrintIfWatched("FindResourcesState: Path failed or stuck. Retrying path to resource.\n");
				if (!me->MoveTo(m_resourceTarget->GetAbsOrigin(), SAFEST_ROUTE))
				{
					me->PrintIfWatched("FindResourcesState: Still unable to path to resource. Clearing target.\n");
					m_resourceTarget = NULL; // Give up on this target
				}
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
		}
	}
	else if (m_searchTimer.IsElapsed()) // No target and search timer elapsed (should have been caught above, but safeguard)
	{
	    me->PrintIfWatched("FindResourcesState: No target and search timer elapsed. Idling.\n");
		me->Idle();
	}
}

//--------------------------------------------------------------------------------------------------------------
void FindResourcesState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "FindResourcesState: Exiting state.\n" );
	m_resourceTarget = NULL;
	me->Stop(); // Stop any movement
	m_searchTimer.Invalidate();
	m_repathTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_find_resources.cpp]
