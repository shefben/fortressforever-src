#include "cbase.h"
#include "bot_util.h"
#include "script_object.h" // For CScriptObject, if used to get Lua object
#include "ff_scriptman.h"   // For _scriptman (assuming it's GScriptMan())
#include "entityoutput.h"   // For variant_t and GetEntityName() if needed indirectly
#include "string_t.h"       // For STRING()

// Lua C API includes
extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
}
// LuaBridge include - ensure this path is correct for your project structure
// #include "LuaBridge/LuaBridge.h"

// Assuming _scriptman is an extern global or accessible via a singleton pattern like GScriptMan()
// If GScriptMan() is the correct way:
#ifndef _scriptman
#define _scriptman (*GScriptMan())
#endif


int GetIntPropertyFromLua(CBaseEntity *pEntity, const char *propertyName) {
    if (!pEntity || !propertyName) {
        // DevMsg("GetIntPropertyFromLua: Null pEntity or propertyName.\n");
        return -1;
    }

    // TODO_FF: Determine the correct way to access GScriptMan() or _scriptman
    // For now, directly using _scriptman as per the plan.
    if (!_scriptman.GetLuaState()) {
        DevWarning("GetIntPropertyFromLua: Lua state is NULL via _scriptman.\n");
        return -1;
    }
    lua_State* L = _scriptman.GetLuaState();

    int result = -1; // Default to -1 (property not found or not a number)
    int originalStackTop = lua_gettop(L);

    // Try to get the Lua object associated with the entity.
    // This often involves using the entity's targetname as a global variable in Lua,
    // or if the entity is a CScriptObject, it might have a direct Lua binding.

    CScriptObject* pScriptObject = dynamic_cast<CScriptObject*>(pEntity);

    if (pScriptObject && pScriptObject->GetLuaObjectRef() != LUA_NOREF)
    {
        // Option 1: Entity has a direct Lua reference (e.g., via CScriptObject)
        lua_rawgeti(L, LUA_REGISTRYINDEX, pScriptObject->GetLuaObjectRef());
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, propertyName);
            if (lua_isnumber(L, -1)) {
                result = lua_tointeger(L, -1);
                // DevMsg("GetIntPropertyFromLua: Found property '%s' = %d via CScriptObject for ent %d (%s)\n", propertyName, result, pEntity->entindex(), STRING(pEntity->GetEntityName()));
            } else {
                 //DevMsg("GetIntPropertyFromLua: Property '%s' not found or not a number for CScriptObject ent %d (%s)\n", propertyName, pEntity->entindex(), STRING(pEntity->GetEntityName()));
            }
            lua_pop(L, 1); // Pop property value or nil
        } else {
            //DevMsg("GetIntPropertyFromLua: Lua object for CScriptObject ent %d (%s) is not a table.\n", pEntity->entindex(), STRING(pEntity->GetEntityName()));
        }
        lua_pop(L, 1); // Pop CScriptObject's Lua table or nil
    }
    else
    {
        // Option 2: Entity name is a global table (fallback, common for map entities like flags)
        const char* entityName = STRING(pEntity->GetEntityName());
        if (entityName && entityName[0]) {
            lua_getglobal(L, entityName); // Pushes global onto stack
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, propertyName); // Pushes field value onto stack
                if (lua_isnumber(L, -1)) {
                    result = lua_tointeger(L, -1);
                    // DevMsg("GetIntPropertyFromLua: Found property '%s' = %d on global Lua table '%s' for ent %d\n", propertyName, result, entityName, pEntity->entindex());
                } else {
                    // Property not found or not a number on the global entity table
                    // DevMsg("GetIntPropertyFromLua: Property '%s' not found or not a number on global Lua table '%s' for ent %d\n", propertyName, entityName, pEntity->entindex());
                }
                lua_pop(L, 1); // Pop property value or nil
            } else {
                // Global entity name not found or not a table
                // DevMsg("GetIntPropertyFromLua: Global Lua object '%s' not found or not a table for ent %d.\n", entityName, pEntity->entindex());
            }
            lua_pop(L, 1); // Pop global entity table or nil
        } else {
            // DevMsg("GetIntPropertyFromLua: Entity name is empty for ent %d. Cannot look up global Lua table.\n", pEntity->entindex());
        }
    }

    // Restore Lua stack to original state to prevent stack corruption
    lua_settop(L, originalStackTop);

    return result;
}
