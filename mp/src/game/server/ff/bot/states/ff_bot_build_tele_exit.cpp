//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ff_bot_build_tele_exit.h"
#include "bot/ff_bot.h" // For CFFBot
#include "ff_buildableobject.h" // For CFFBuildableObject and CFFTeleporterExit

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
BuildTeleExitState::BuildTeleExitState( void )
{
	m_teleExit = NULL;
	m_isBlueprintPlaced = false;
	// m_buildLocation = vec3_invalid;
}

//--------------------------------------------------------------------------------------------------------------
void BuildTeleExitState::OnEnter( CFFBot *me )
{
	me->SelectSpecificBuildable( CFFBot::BUILDABLE_TELE_EXIT );
	m_isBlueprintPlaced = false;
	m_teleExit = NULL;
	m_actionTimer.Start( RandomFloat( 0.5f, 1.0f ) ); // Time to place blueprint

	PrintIfWatched( "%s: Entering BuildTeleporterExitState.\n", me->GetPlayerName() );
	// FF_TODO_CLASS_ENGINEER: Add logic to choose a good build location if m_buildLocation is not set.
}

//--------------------------------------------------------------------------------------------------------------
void BuildTeleExitState::OnUpdate( CFFBot *me )
{
	if ( !me->IsEngineer() )
	{
		me->Idle();
		return;
	}

	if ( me->GetTeleporterEntranceLevel() == 0 )
	{
		PrintIfWatched( "%s: Teleporter Entrance doesn't exist or was destroyed. Aborting Exit build.\n", me->GetPlayerName() );
		me->Idle(); // Cannot build exit without an entrance
		return;
	}

	if ( !m_isBlueprintPlaced )
	{
		if ( m_actionTimer.IsElapsed() )
		{
			me->PressButton( IN_ATTACK );
			me->ReleaseButton( IN_ATTACK );
			m_isBlueprintPlaced = true;
			m_actionTimer.Start( RandomFloat( 0.5f, 1.0f ) );
			PrintIfWatched( "%s: Placed Teleporter Exit blueprint (simulated).\n", me->GetPlayerName() );
		}
		return;
	}

	if ( m_teleExit == NULL )
	{
		if ( m_actionTimer.IsElapsed() )
		{
			CBaseEntity* pBuilding = NULL;
			float searchRadius = 150.0f;
			CBaseEntity *pEnt = NULL;
			while ((pEnt = gEntList.FindEntityInSphere(pEnt, me->GetAbsOrigin(), searchRadius)) != NULL)
			{
				// FF_TODO_CLASS_ENGINEER: Verify classname "obj_teleporter_exit"
				if (FClassnameIs(pEnt, "obj_teleporter_exit") && pEnt->GetOwnerEntity() == me)
				{
					CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject*>(pEnt);
					if (pBuildable && !pBuildable->IsBuilt())
					{
						pBuilding = pBuildable;
						break;
					}
				}
			}

			if ( pBuilding )
			{
				m_teleExit = pBuilding;
				PrintIfWatched( "%s: Found Teleporter Exit entity to build: %s\n", me->GetPlayerName(), m_teleExit->GetClassname() );
			}
			else
			{
				PrintIfWatched( "%s: Could not find Teleporter Exit blueprint after placement. Giving up.\n", me->GetPlayerName() );
				me->Idle();
				return;
			}
		}
		return;
	}

	CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject*>(m_teleExit.Get());
	if ( !pBuildable )
	{
		PrintIfWatched( "%s: Teleporter Exit entity %s is not a CFFBuildableObject! Aborting build.\n", me->GetPlayerName(), m_teleExit->GetClassname() );
		me->Idle();
		return;
	}

	if ( pBuildable->IsBuilt() )
	{
		PrintIfWatched( "%s: Teleporter Exit %s is now built!\n", me->GetPlayerName(), m_teleExit->GetClassname() );
		// FF_TODO_CLASS_ENGINEER: Teleporters need linking. How is this done? Does the bot need to do anything special after building both?
		me->Idle();
		return;
	}

	CFFWeaponBase* pSpanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
	if (me->GetActiveFFWeapon() != pSpanner && pSpanner)
	{
		me->EquipWeapon(pSpanner);
	}

	if (me->GetActiveFFWeapon() == pSpanner)
	{
		if (m_actionTimer.IsElapsed())
		{
			me->SetLookAt("Wrenching Teleporter Exit", pBuildable->WorldSpaceCenter(), PRIORITY_HIGH, 0.3f);
			me->PressButton(IN_ATTACK);
			me->ReleaseButton(IN_ATTACK);
			m_actionTimer.Start(RandomFloat(0.6f, 0.8f));
			// PrintIfWatched( "%s: Wrenching Teleporter Exit %s.\n", me->GetPlayerName(), m_teleExit->GetClassname() );
		}
	}
	else
	{
		PrintIfWatched( "%s: Needs spanner to build Teleporter Exit.\n", me->GetPlayerName() );
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuildTeleExitState::OnExit( CFFBot *me )
{
	m_teleExit = NULL;
	m_isBlueprintPlaced = false;
	// m_buildLocation = vec3_invalid;
	PrintIfWatched( "%s: Exiting BuildTeleporterExitState.\n", me->GetPlayerName() );
}
