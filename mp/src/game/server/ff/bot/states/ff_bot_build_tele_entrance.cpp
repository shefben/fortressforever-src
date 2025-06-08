//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ff_bot_build_tele_entrance.h"
#include "bot/ff_bot.h" // For CFFBot
#include "ff_buildableobject.h" // For CFFBuildableObject and CFFTeleporterEntrance

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
BuildTeleEntranceState::BuildTeleEntranceState( void )
{
	m_teleEntrance = NULL;
	m_isBlueprintPlaced = false;
	// m_buildLocation = vec3_invalid;
}

//--------------------------------------------------------------------------------------------------------------
void BuildTeleEntranceState::OnEnter( CFFBot *me )
{
	me->SelectSpecificBuildable( CFFBot::BUILDABLE_TELE_ENTRANCE ); // Bot attempts to select/switch to teleporter entrance blueprint
	m_isBlueprintPlaced = false;
	m_teleEntrance = NULL;
	m_actionTimer.Start( RandomFloat( 0.5f, 1.0f ) ); // Time to place blueprint

	PrintIfWatched( "%s: Entering BuildTeleporterEntranceState.\n", me->GetPlayerName() );
	// FF_TODO_CLASS_ENGINEER: Add logic to choose a good build location if m_buildLocation is not set.
	// For now, assumes bot is already roughly where it wants to build or CFFPlayer handles placement.
}

//--------------------------------------------------------------------------------------------------------------
void BuildTeleEntranceState::OnUpdate( CFFBot *me )
{
	if ( !me->IsEngineer() )
	{
		me->Idle();
		return;
	}

	if ( !m_isBlueprintPlaced )
	{
		if ( m_actionTimer.IsElapsed() )
		{
			// Simulate placing the blueprint (IN_ATTACK)
			// FF_TODO_CLASS_ENGINEER: Verify if IN_ATTACK is the correct button.
			// CFFPlayer::SelectItem("item_teleporter_entrance_blueprint"); // Conceptual if blueprints are items
			// me->HandleCommand("buildteleporterentrance"); // This is already called by SelectSpecificBuildable.
			// Here, we assume SelectSpecificBuildable has switched to the blueprint, now we "place" it.
			me->PressButton( IN_ATTACK );
			me->ReleaseButton( IN_ATTACK );
			m_isBlueprintPlaced = true;
			m_actionTimer.Start( RandomFloat( 0.5f, 1.0f ) ); // Time to find the placed blueprint entity
			PrintIfWatched( "%s: Placed Teleporter Entrance blueprint (simulated).\n", me->GetPlayerName() );
		}
		return;
	}

	if ( m_teleEntrance == NULL )
	{
		if ( m_actionTimer.IsElapsed() ) // Give some time for blueprint to appear
		{
			// FF_TODO_CLASS_ENGINEER: Find the actual teleporter entrance entity.
			// This might involve searching for entities of class "obj_teleporter_entrance" owned by the bot
			// that are not yet fully built.
			// For now, placeholder:
			CBaseEntity* pBuilding = NULL; // Find my tele entrance blueprint
			float searchRadius = 150.0f;
			CBaseEntity *pEnt = NULL;
			while ((pEnt = gEntList.FindEntityInSphere(pEnt, me->GetAbsOrigin(), searchRadius)) != NULL)
			{
				if (FClassnameIs(pEnt, "obj_teleporter_entrance") && pEnt->GetOwnerEntity() == me)
				{
					CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject*>(pEnt);
					if (pBuildable && !pBuildable->IsBuilt()) // Found our unbuilt entrance
					{
						pBuilding = pBuildable;
						break;
					}
				}
			}

			if ( pBuilding )
			{
				m_teleEntrance = pBuilding;
				PrintIfWatched( "%s: Found Teleporter Entrance entity to build: %s\n", me->GetPlayerName(), m_teleEntrance->GetClassname() );
			}
			else
			{
				PrintIfWatched( "%s: Could not find Teleporter Entrance blueprint after placement. Giving up.\n", me->GetPlayerName() );
				me->Idle(); // Failed to find it
				return;
			}
		}
		return; // Still waiting to find the blueprint
	}

	// We have the teleporter entrance entity, now build it
	CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject*>(m_teleEntrance.Get());
	if ( !pBuildable )
	{
		PrintIfWatched( "%s: Teleporter Entrance entity %s is not a CFFBuildableObject! Aborting build.\n", me->GetPlayerName(), m_teleEntrance->GetClassname() );
		me->Idle();
		return;
	}

	if ( pBuildable->IsBuilt() )
	{
		PrintIfWatched( "%s: Teleporter Entrance %s is now built!\n", me->GetPlayerName(), m_teleEntrance->GetClassname() );
		// FF_TODO_CLASS_ENGINEER: If an exit already exists, link them? Or does game do this automatically?
		// FF_TODO_CLASS_ENGINEER: Consider building an exit next if one doesn't exist.
		me->Idle(); // Done
		return;
	}

	// Hit it with the wrench
	// FF_TODO_CLASS_ENGINEER: Ensure spanner is equipped and IN_ATTACK hits buildables.
	CFFWeaponBase* pSpanner = me->GetWeaponByID(FF_WEAPON_SPANNER); // Assuming spanner ID
	if (me->GetActiveFFWeapon() != pSpanner && pSpanner)
	{
		me->EquipWeapon(pSpanner);
	}

	if (me->GetActiveFFWeapon() == pSpanner)
	{
		if (m_actionTimer.IsElapsed()) // Time between wrench hits
		{
			me->SetLookAt("Wrenching Teleporter Entrance", pBuildable->WorldSpaceCenter(), PRIORITY_HIGH, 0.3f);
			me->PressButton(IN_ATTACK);
			me->ReleaseButton(IN_ATTACK);
			m_actionTimer.Start(RandomFloat(0.6f, 0.8f)); // Wrench hit interval
			// PrintIfWatched( "%s: Wrenching Teleporter Entrance %s.\n", me->GetPlayerName(), m_teleEntrance->GetClassname() );
		}
	}
	else
	{
		PrintIfWatched( "%s: Needs spanner to build Teleporter Entrance.\n", me->GetPlayerName() );
		// Bot might be stuck without a spanner, or weapon switching is slow.
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuildTeleEntranceState::OnExit( CFFBot *me )
{
	m_teleEntrance = NULL;
	m_isBlueprintPlaced = false;
	// m_buildLocation = vec3_invalid;
	PrintIfWatched( "%s: Exiting BuildTeleporterEntranceState.\n", me->GetPlayerName() );
}
