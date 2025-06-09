#ifndef BOT_LUA_UTILS_H
#define BOT_LUA_UTILS_H
#pragma once

class CBaseEntity;

// Returns true if property found and is a number, outValue is set.
// Returns false otherwise.
bool GetScriptIntProperty(CBaseEntity* pEntity, const char* propertyName, int& outValue);

// Add other types like GetScriptStringProperty if needed later.

#endif // BOT_LUA_UTILS_H
