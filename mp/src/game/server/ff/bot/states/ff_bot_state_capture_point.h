#ifndef FF_BOT_STATE_CAPTURE_POINT_H
#define FF_BOT_STATE_CAPTURE_POINT_H

#include "../ff_bot.h"
#include "../bot_constants.h"
#include "../ff_gamestate.h"

class CapturePointState : public BotState
{
public:
    CapturePointState();
    virtual void OnEnter( CFFBot *bot );
    virtual void OnUpdate( CFFBot *bot );
    virtual void OnExit( CFFBot *bot );
    virtual const char *GetName( void ) const { return "CapturePoint"; }

private:
    CHandle<CBaseEntity> m_hCurrentCP;
    int m_currentCP_ID; // 0-indexed ID corresponding to FFGameState::m_ControlPoints
    float m_nextActionTimestamp;
    float m_repathTimer; // Timer to occasionally re-path to CP center if moved
};

#endif // FF_BOT_STATE_CAPTURE_POINT_H
