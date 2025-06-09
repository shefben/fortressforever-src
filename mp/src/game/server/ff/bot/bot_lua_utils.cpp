#include "cbase.h"
#include "bot_lua_utils.h"
#include "script_object.h" // For CScriptObject
#include "ff_scriptman.h"   // For _scriptman to get lua_State*
#include "lua.hpp"          // For Lua API (includes lua.h, lauxlib.h, lualib.h)

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

bool GetScriptIntProperty(CBaseEntity* pEntity, const char* propertyName, int& outValue)
{
    if (!pEntity || !propertyName || !propertyName[0]) return false;

    // Attempt to cast to CScriptObject to access Lua table
    CScriptObject* pScriptObject = dynamic_cast<CScriptObject*>(pEntity);
    if (!pScriptObject)
    {
        // Fallback: try using entity name if not a CScriptObject (less reliable)
        if (!pEntity->GetEntityName() || pEntity->GetEntityName() == NULL_STRING) return false;
        const char* luaTableName = STRING(pEntity->GetEntityName());
        if (!luaTableName || !luaTableName[0]) return false;

        lua_State* L = _scriptman.GetLuaState();
        if (!L) return false;

        lua_getglobal(L, luaTableName);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return false;
        }

        lua_getfield(L, -1, propertyName);
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 2);
            return false;
        }

        outValue = lua_tointeger(L, -1);
        lua_pop(L, 2);
        return true;
    }

    // Preferred method: Use CScriptObject's Lua table reference
    if (!pScriptObject->HasLuaTable())
    {
        return false;
    }

    lua_State* L = _scriptman.GetLuaState();
    if (!L) return false;

    pScriptObject->PushLuaTable(L); // Pushes the entity's Lua table onto the stack
    if (!lua_istable(L, -1)) { // Should always be a table if HasLuaTable returned true
        lua_pop(L, 1);
        return false;
    }

    lua_getfield(L, -1, propertyName);
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 2); // Pop non-number and table
        return false;
    }

    outValue = lua_tointeger(L, -1);
    lua_pop(L, 2); // Pop number and table
    return true;
}
