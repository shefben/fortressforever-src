#ifndef FF_BOT_STATE_CARRY_FLAG_H
#define FF_BOT_STATE_CARRY_FLAG_H

#include "../ff_bot.h"
#include "../ff_gamestate.h"

class CarryFlagState : public BotState
{
public:
    CarryFlagState();
    virtual void OnEnter( CFFBot *bot );
    virtual void OnUpdate( CFFBot *bot );
    virtual void OnExit( CFFBot *bot );
    virtual const char *GetName( void ) const { return "CarryFlag"; }

private:
    CHandle<CBaseEntity> m_hOurCaptureZone;
    // No other members strictly needed for this basic version,
    // pathing is handled by bot->MoveTo called in OnEnter.
};

#endif // FF_BOT_STATE_CARRY_FLAG_H
