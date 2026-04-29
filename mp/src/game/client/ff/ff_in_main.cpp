//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: FF specific input handling
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "kbutton.h"
#include "input.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: TF Input interface
//-----------------------------------------------------------------------------
class CFFInput : public CInput
{
public:
};

static CFFInput g_Input;

// Expose this interface
IInput *input = ( IInput * )&g_Input;
