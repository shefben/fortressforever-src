#ifndef FF_BOT_UTIL_H
#define FF_BOT_UTIL_H
#pragma once

class CBaseEntity; // Forward declaration
struct lua_State;  // Forward declaration for Lua state

// Function to get an integer property from an entity's Lua object
// Returns -1 or a specific error code if property not found or not a number.
int GetIntPropertyFromLua(CBaseEntity *pEntity, const char *propertyName);

// Specific helpers using the generic one
inline int GetEntityCpNumberFromLua(CBaseEntity *pEntity) {
    // cp_number in Lua is typically 1-based. This function will return it as is.
    // Conversion to 0-based should happen at the call site if needed.
    return GetIntPropertyFromLua(pEntity, "cp_number");
}

inline int GetEntityBotGoalTypeFromLua(CBaseEntity *pEntity) {
    // botgoaltype in Lua should correspond to BOT_GOAL_TYPE constants.
    return GetIntPropertyFromLua(pEntity, "botgoaltype");
}

// Forward declare CFFBot for BotLog, if it's not already included via other headers
// class CFFBot;
// Example utility function declaration (implementation would go in a .cpp or be inline)
// For actual printing, it might need access to bot's player index or other means to filter messages.
// inline void BotLog(CFFBot *bot, const char *fmt, ...)
// {
//     // Basic stub for logging, replace with actual engine logging if available and appropriate
// #ifdef _DEBUG
//     va_list argptr;
//     char string[1024];
//     va_start (argptr,fmt);
//     Q_vsnprintf(string, sizeof(string), fmt,argptr);
//     va_end (argptr);
//     // Msg("[Bot %d] %s", bot ? bot->entindex() : 0, string);
// #endif
// }


#endif // FF_BOT_UTIL_H
