//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../ff_bot.h" // Changed from cs_bot.h

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Plant the bomb. - FF: This state is CS-specific and needs to be re-evaluated or removed.
 * Could be adapted for FF objectives like capturing a point or planting a device if one exists.
 */
void PlantBombState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO: PlantBombState::OnEnter - CS-specific logic removed/stubbed.\n" );
	// me->Crouch();
	// me->SetDisposition( CFFBot::SELF_DEFENSE ); // Changed CCSBot to CFFBot

	// float yaw = me->EyeAngles().y;
	// Vector2D dir( BotCOS(yaw), BotSIN(yaw) );
	// Vector myOrigin = GetCentroid( me );
	// Vector down( myOrigin.x + 10.0f * dir.x, myOrigin.y + 10.0f * dir.y, me->GetFeetZ() );
	// me->SetLookAt( "Plant bomb on floor", down, PRIORITY_HIGH ); // "Plant bomb" is CS-specific
	me->Idle(); // Default to idle as planting bomb is not applicable
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Plant the bomb. - FF: This state is CS-specific.
 */
void PlantBombState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO: PlantBombState::OnUpdate - CS-specific logic removed/stubbed.\n" );
	// CBasePlayerWeapon *gun = me->GetActiveWeapon(); // Use CBasePlayerWeapon
	// bool holdingC4 = false; // C4 is CS-specific
	// if (gun)
	// {
	// 	// FF_TODO: Replace "weapon_c4" with an FF equivalent if a bot needs to hold a specific item for an objective
	// 	// if (FStrEq( gun->GetClassname(), "weapon_c4" ))
	// 	//	holdingC4 = true;
	// }

	// if (holdingC4)
	// 	me->PrimaryAttack();
	// else
	// 	me->SelectItem( "weapon_c4" ); // CS-specific item

	// if (!me->HasC4()) // HasC4 is CS-specific
	// {
	// 	// FF_TODO: Adapt task for FF objective, e.g., GUARD_CAPTURED_POINT
	// 	// me->SetTask( CFFBot::GUARD_TICKING_BOMB ); // GUARD_TICKING_BOMB is CS-specific
	// 	me->Hide();
	// }

	// const float timeout = 5.0f;
	// if (gpGlobals->curtime - me->GetStateTimestamp() > timeout)
	// 	me->Idle();
	me->Idle(); // Default to idle
}

//--------------------------------------------------------------------------------------------------------------
void PlantBombState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO: PlantBombState::OnExit - CS-specific logic reviewed.\n" );
	// me->EquipBestWeapon(); // EquipBestWeapon needs FF logic
	// me->StandUp();
	// me->ResetStuckMonitor();
	// me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE ); // ENGAGE_AND_INVESTIGATE is generic
	// me->ClearLookAt();
}
