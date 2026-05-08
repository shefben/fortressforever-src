//========= Fortress Forever Bot =============================================//
//
// FFBotClass — per-tick driver for class-specific behaviors.
//
// Most class behavior in FF is *layered* over base movement and combat: a spy
// keeps cloaking and disguising while still chasing flag goals; an engineer
// builds and maintains buildables on top of staying near the defensive area;
// a sniper zooms when at range. None of these need a dedicated Action — they
// are tick-rate triggers that run alongside the bot's main objective.
//
// This module is invoked once per tick from CFFBotMainAction::Update, between
// the always-on aim/fire/stuck handlers and the threat dispatch.
//
//===========================================================================//

#ifndef FF_BOT_CLASS_H
#define FF_BOT_CLASS_H
#ifdef _WIN32
#pragma once
#endif

class CFFBot;

namespace FFBotClass
{
	// Per-tick driver. No-op for classes without specialty behavior implemented yet.
	void Update( CFFBot *me );
}

#endif // FF_BOT_CLASS_H
