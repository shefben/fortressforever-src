//========= Fortress Forever Bot =============================================//
//
// Client-side mirror for DT_NextBot (server-side NextBotCombatCharacter).
//
// FF doesn't actually instantiate any standalone NextBotCombatCharacter
// entities — CFFBot inherits NextBotPlayer<CFFPlayer> and networks as a
// player. But the imported NextBot framework still calls
// IMPLEMENT_SERVERCLASS_ST(NextBotCombatCharacter, DT_NextBot) at link time,
// which registers a SendTable. Without a matching client RecvTable, the
// engine's DataTable validation drops the client on connect:
//
//   "DataTable warning: No matching RecvTable for SendTable 'DT_NextBot'."
//   "Host_EndGame: CL_ParseClassInfo_EndClasses: CreateDecoders failed."
//
// This file provides the empty mirror. The class itself doesn't need to do
// anything — it just has to exist so the engine can match the network class.
//
//===========================================================================//

#include "cbase.h"
#include "c_basecombatcharacter.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


class C_NextBotCombatCharacter : public C_BaseCombatCharacter
{
public:
	DECLARE_CLASS( C_NextBotCombatCharacter, C_BaseCombatCharacter );
	DECLARE_CLIENTCLASS();
};

IMPLEMENT_CLIENTCLASS_DT( C_NextBotCombatCharacter, DT_NextBot, NextBotCombatCharacter )
END_RECV_TABLE()
