#pragma once

// Lightweight header-only scaffolding for Lua-backed actions.
// NOTE:
// - This header provides a safe, compileable placeholder implementation so the
//   codebase can be extended incrementally. The actual Lua binding is guarded
//   and can be enabled in selected targets (e.g. Tests).
// - It intentionally keeps dependencies minimal to avoid forcing a Lua dependency
//   into every consumer of the GOAPie headers.
//
// Primary types:
// - LuaSandbox        : runtime wrapper that hosts lua_State when enabled.
// - LuaActionSimulator: ActionSimulator that delegates evaluate/simulate/heuristic
//                       to user-provided Lua snippets (stored as strings).
// - LuaActionSetEntry : ActionSetEntry factory producing LuaActionSimulator instances.
// - helpers           : small utility to append Lua entries into a Planner.

#include <string>
#include <vector>
#include <memory>

#include "action.h"
#include "arguments.h"
#include "simulation.h"
#include "goal.h"
#include "planner.h"

#ifndef GIE_WITH_LUA
#define GIE_WITH_LUA 0
#endif

#if GIE_WITH_LUA
 // Only include heavy STL and Lua headers when the real Lua path is enabled.
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <iostream>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#endif // GIE_WITH_LUA

namespace gie
{

#if GIE_WITH_LUA

// Suppress Lua's abort/message-box on a panic by installing a custom panic
// handler that logs the error and returns instead of calling abort.
// Use 'static' for internal linkage so the header remains safe across TUs.
static int goapie_lua_panic_handler(lua_State* L)
{
    const char* msg = lua_tostring(L, -1);
    if (msg)
    {
        std::cerr << "[LuaSandbox] PANIC: " << msg << "\n";
    }
    else
    {
        std::cerr << "[LuaSandbox] PANIC: (no message on stack)\n";
    }
    /* Do not call abort or show a message box; return to C.
       Returning from a panic handler is allowed (Lua treats it as last-resort)
       and avoids platform dialogs. */
    return 0;
}

// Helper C functions exposed to Lua (minimal placeholders).
static int goapie_lua_debug_func(lua_State* L)
{
    const char* msg = lua_tostring(L, 1);
    if (msg) std::cout << "[Lua] " << msg << "\n";
    return 0;
}

static int goapie_lua_get_property_func(lua_State* L)
{
    // Args: (guid, propName) -> returns property value or nil.
    // Retrieve current Simulation pointer stored in registry.
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud)
    {
        lua_pushnil(L);
        return 1;
    }
    Simulation* sim = static_cast< Simulation* >( ud );

    if( !lua_isnumber( L, 1 ) || !lua_isstring( L, 2 ) )
    {
        lua_pushnil( L );
        return 1;
    }

    Guid guid = static_cast< Guid >( lua_tointeger( L, 1 ) );
    const char* propName = lua_tostring( L, 2 );
    if( !propName ) { lua_pushnil( L ); return 1; }

    const Entity* ent = sim->context().entity( guid );
    if( !ent ) ent = sim->world()->entity( guid );
    if( !ent ) { lua_pushnil( L ); return 1; }

    const Property* ppt = ent->property( std::string_view( propName ) );
    if( !ppt ) { lua_pushnil( L ); return 1; }

    switch( ppt->type() )
    {
    case Property::Boolean:
        lua_pushboolean( L, ppt->getBool() ? *ppt->getBool() : 0 );
        return 1;
    case Property::Float:
        lua_pushnumber( L, ppt->getFloat() ? static_cast< lua_Number >( *ppt->getFloat() ) : 0.0 );
        return 1;
    case Property::Integer:
        lua_pushinteger( L, ppt->getInteger() ? static_cast< lua_Integer >( *ppt->getInteger() ) : 0 );
        return 1;
    case Property::GUID:
        lua_pushinteger( L, ppt->getGuid() ? static_cast< lua_Integer >( *ppt->getGuid() ) : 0 );
        return 1;
    case Property::Vec3:
        if( const glm::vec3* v = ppt->getVec3() )
        {
            lua_newtable( L );
            lua_pushnumber( L, v->x ); lua_rawseti( L, -2, 1 );
            lua_pushnumber( L, v->y ); lua_rawseti( L, -2, 2 );
            lua_pushnumber( L, v->z ); lua_rawseti( L, -2, 3 );
            return 1;
        }
        lua_pushnil( L );
        return 1;
    case Property::GUIDArray:
        if( const Property::GuidVector* vec = ppt->getGuidArray() )
        {
            lua_newtable( L );
            for( size_t i = 0; i < vec->size(); ++i )
            {
                lua_pushinteger( L, static_cast< lua_Integer >( ( *vec )[ i ] ) );
                lua_rawseti( L, -2, static_cast< int >( i + 1 ) );
            }
            return 1;
        }
        lua_pushnil( L );
        return 1;
    case Property::FloatArray:
        if( const Property::FloatVector* vec = ppt->getFloatArray() )
        {
            lua_newtable( L );
            for( size_t i = 0; i < vec->size(); ++i )
            {
                lua_pushnumber( L, static_cast< lua_Number >( ( *vec )[ i ] ) );
                lua_rawseti( L, -2, static_cast< int >( i + 1 ) );
            }
            return 1;
        }
        lua_pushnil( L );
        return 1;
    default:
        lua_pushnil( L );
        return 1;
    }
}

static int goapie_lua_set_property_func(lua_State* L)
{
    // Args: (guid, propName, value) -> returns boolean success.
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushboolean(L, 0); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    if( !lua_isnumber( L, 1 ) || !lua_isstring( L, 2 ) )
    {
        lua_pushboolean( L, 0 );
        return 1;
    }

    Guid guid = static_cast< Guid >( lua_tointeger( L, 1 ) );
    const char* propName = lua_tostring( L, 2 );

    Entity* ent = sim->context().entity( guid );
    if( !ent ) ent = sim->world()->entity( guid );
    if( !ent )
    {
        lua_pushboolean( L, 0 );
        return 1;
    }

    // Determine Lua value type and set property accordingly.
    if( lua_isboolean( L, 3 ) )
    {
        bool v = lua_toboolean( L, 3 ) != 0;
        Property* ppt = ent->createProperty( std::string_view( propName ), v );
        if( !ppt ) { lua_pushboolean( L, 0 ); return 1; }
        lua_pushboolean( L, 1 );
        return 1;
    }
    else if( lua_isinteger( L, 3 ) )
    {
        int32_t v = static_cast< int32_t >( lua_tointeger( L, 3 ) );
        Property* ppt = ent->createProperty( std::string_view( propName ), v );
        lua_pushboolean( L, ppt ? 1 : 0 );
        return 1;
    }
    else if( lua_isnumber( L, 3 ) )
    {
        float v = static_cast< float >( lua_tonumber( L, 3 ) );
        Property* ppt = ent->createProperty( std::string_view( propName ), v );
        lua_pushboolean( L, ppt ? 1 : 0 );
        return 1;
    }
    else if( lua_istable( L, 3 ) )
    {
        // try vec3 (three numeric entries) or array of guids/numbers
        // check length
        lua_len( L, 3 );
        int len = static_cast<int>( lua_tointeger( L, -1 ) );
        lua_pop( L, 1 );
        if( len == 3 )
        {
            // attempt vec3
            double x, y, z;
            lua_rawgeti( L, 3, 1 ); x = lua_tonumber( L, -1 ); lua_pop( L, 1 );
            lua_rawgeti( L, 3, 2 ); y = lua_tonumber( L, -1 ); lua_pop( L, 1 );
            lua_rawgeti( L, 3, 3 ); z = lua_tonumber( L, -1 ); lua_pop( L, 1 );
            Property* ppt = ent->createProperty( std::string_view( propName ), glm::vec3( static_cast<float>( x ), static_cast<float>( y ), static_cast<float>( z ) ) );
            lua_pushboolean( L, ppt ? 1 : 0 );
            return 1;
        }
        else
        {
            // assume array of guids/numbers -> GUIDArray
            Property::GuidVector vec;
            for( int i = 1; i <= len; ++i )
            {
                lua_rawgeti( L, 3, i );
                if( lua_isnumber( L, -1 ) )
                {
                    vec.push_back( static_cast< Guid >( lua_tointeger( L, -1 ) ) );
                }
                lua_pop( L, 1 );
            }
            Property* ppt = ent->createProperty( std::string_view( propName ), vec );
            lua_pushboolean( L, ppt ? 1 : 0 );
            return 1;
        }
    }

    lua_pushboolean( L, 0 );
    return 1;
}

static int goapie_lua_entity_by_name_func(lua_State* L)
{
    // Args: (name) -> guid or nil
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushnil(L); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    if( !lua_isstring( L, 1 ) )
    {
        lua_pushnil( L );
        return 1;
    }

    const char* name = lua_tostring( L, 1 );
    StringHash targetHash = stringHasher( name );

    // Search simulation context first, then world context.
    for( const auto& kv : sim->context().entities() )
    {
        const Entity& e = kv.second;
        if( e.nameHash() == targetHash )
        {
            lua_pushinteger( L, static_cast< lua_Integer >( e.guid() ) );
            return 1;
        }
    }

    for( const auto& kv : sim->world()->context().entities() )
    {
        const Entity& e = kv.second;
        if( e.nameHash() == targetHash )
        {
            lua_pushinteger( L, static_cast< lua_Integer >( e.guid() ) );
            return 1;
        }
    }

    lua_pushnil( L );
    return 1;
}

static int goapie_lua_tag_set_func(lua_State* L)
{
    // Args: (tagName) -> array table of GUIDs or nil
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushnil(L); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    if( !lua_isstring( L, 1 ) )
    {
        lua_pushnil( L );
        return 1;
    }

    const char* tagName = lua_tostring( L, 1 );
    const std::set< Guid >* set = sim->tagSet( std::string_view( tagName ) );
    if( !set ) { lua_pushnil( L ); return 1; }

    lua_newtable( L );
    int idx = 1;
    for( Guid g : *set )
    {
        lua_pushinteger( L, static_cast< lua_Integer >( g ) );
        lua_rawseti( L, -2, idx++ );
    }
    return 1;
}

static int goapie_lua_move_agent_to_entity_func(lua_State* L)
{
    // Args: (targetEntityGuid) -> return path length (number) or nil on failure.
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushnil(L); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    if( !lua_isnumber( L, 1 ) )
    {
        lua_pushnil( L );
        return 1;
    }

    Guid target = static_cast< Guid >( lua_tointeger( L, 1 ) );
    Guid agentGuid = sim->agent().guid();
    Entity* agentEnt = sim->context().entity( agentGuid );
    if( !agentEnt ) agentEnt = sim->world()->entity( agentGuid );
    const Entity* targetEntC = sim->context().entity( target );
    if( !targetEntC ) targetEntC = sim->world()->entity( target );
    if( !agentEnt || !targetEntC )
    {
        lua_pushnil( L );
        return 1;
    }

    const Property* agentLocP = agentEnt->property( "Location" );
    const Property* targetLocP = targetEntC->property( "Location" );
    if( !targetLocP || !targetLocP->getVec3() )
    {
        lua_pushnil( L );
        return 1;
    }

    glm::vec3 agentPos{ 0.0f, 0.0f, 0.0f };
    if( agentLocP && agentLocP->getVec3() ) agentPos = *agentLocP->getVec3();
    glm::vec3 targetPos = *targetLocP->getVec3();
    const float dx = agentPos.x - targetPos.x;
    const float dy = agentPos.y - targetPos.y;
    const float dz = agentPos.z - targetPos.z;
    const float dist = std::sqrt( dx*dx + dy*dy + dz*dz );

    // Move agent location inside simulation context (simulate movement)
    agentEnt->createProperty( "Location", targetPos );

    lua_pushnumber( L, static_cast< lua_Number >( dist ) );
    return 1;
}

static int goapie_lua_estimate_heuristic_func(lua_State* L)
{
    // Retrieve sim pointer from registry; if unavailable return 0.
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushnumber(L, 0.0); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    // Try to approximate the example6 heuristic by returning straight-line distance
    // from agent to the Safe entity's Location property (if available).
    Guid agentGuid = sim->agent().guid();
    const Entity* agentEnt = sim->context().entity( agentGuid );
    if( !agentEnt ) agentEnt = sim->world()->entity( agentGuid );
    if( !agentEnt ) { lua_pushnumber( L, 0.0 ); return 1; }

    // Find Safe entity by name hash "Safe"
    StringHash safeHash = stringHasher( "Safe" );
    const Entity* safeEnt = nullptr;
    for( const auto& kv : sim->context().entities() )
    {
        const Entity& e = kv.second;
        if( e.nameHash() == safeHash ) { safeEnt = &e; break; }
    }
    if( !safeEnt )
    {
        for( const auto& kv : sim->world()->context().entities() )
        {
            const Entity& e = kv.second;
            if( e.nameHash() == safeHash ) { safeEnt = &e; break; }
        }
    }

    if( !safeEnt ) { lua_pushnumber( L, 0.0 ); return 1; }

    const Property* agentLocP = agentEnt->property( "Location" );
    const Property* safeLocP = safeEnt->property( "Location" );
    if( !agentLocP || !safeLocP || !agentLocP->getVec3() || !safeLocP->getVec3() )
    {
        lua_pushnumber( L, 0.0 );
        return 1;
    }

    const glm::vec3& a = *agentLocP->getVec3();
    const glm::vec3& s = *safeLocP->getVec3();
    const float dx = a.x - s.x;
    const float dy = a.y - s.y;
    const float dz = a.z - s.z;
    const float dist = std::sqrt( dx*dx + dy*dy + dz*dz );

    lua_pushnumber( L, static_cast< lua_Number >( dist ) );
    return 1;
}

// Real Lua-backed sandbox (requires linking Lua).
class LuaSandbox
{
public:
    LuaSandbox()
    : L( nullptr )
    {
        L = luaL_newstate();
        if( L )
        {
            // Install custom panic handler immediately to avoid platform message boxes / abort
            // if any code in luaL_openlibs (or later initialization) triggers a panic.
            lua_atpanic( L, goapie_lua_panic_handler );
            luaL_openlibs( L );
            std::cout << "[LuaSandbox] luaL_newstate succeeded (panic handler installed)\n";
        }
        else
        {
            std::cout << "[LuaSandbox] luaL_newstate returned nullptr\n";
        }
    }

    ~LuaSandbox()
    {
        if( L ) lua_close( L );
        L = nullptr;
    }

    // Load a chunk (source) into a named table environment. Functions defined in
    // the source (e.g. evaluate, simulate, heuristic) will be stored under a
    // private environment table that falls back to the global environment.
    bool loadChunk( const std::string& name, const std::string& source )
    {
        if( !L )
        {
            std::cout << "[LuaSandbox] loadChunk aborted: lua_State is null\n";
            return false;
        }
        std::cout << "[LuaSandbox] loadChunk: " << name << " (source size=" << source.size() << ")\n";
        // Runtime capability probe: query Lua version and whether 'load' and 'loadstring' are available.
        if( luaL_loadstring( L, "return _VERSION, type(load), type(loadstring)" ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] probe luaL_loadstring error: " << msg << "\n";
            lua_pop( L, 1 );
        }
        else
        {
            if( lua_pcall( L, 0, 3, 0 ) != LUA_OK )
            {
                const char* msg = lua_tostring( L, -1 );
                if( msg ) std::cout << "[LuaSandbox] probe lua_pcall error: " << msg << "\n";
                // attempt traceback if available
#ifdef LUA_VERSION_NUM
                std::cout << "[LuaSandbox] compiled LUA_VERSION_NUM=" << LUA_VERSION_NUM << "\n";
#endif
                lua_pop( L, 1 );
            }
            else
            {
                const char* version = lua_isstring( L, -3 ) ? lua_tostring( L, -3 ) : "(nil)";
                const char* loadType = lua_isstring( L, -2 ) ? lua_tostring( L, -2 ) : "(nil)";
                const char* loadstringType = lua_isstring( L, -1 ) ? lua_tostring( L, -1 ) : "(nil)";
                std::cout << "[LuaSandbox] runtime probe: _VERSION=" << (version ? version : "(null)")
                          << ", type(load)=" << (loadType ? loadType : "(null)")
                          << ", type(loadstring)=" << (loadstringType ? loadstringType : "(null)") << "\n";
                lua_pop( L, 3 );
            }
        }

        // Use a safe wrapper that leverages Lua's load() with an explicit environment.
        // The wrapper creates an env table with fallback to _G, loads the provided source into that env,
        // executes it and returns the env table to C++ where we'll store it in the registry.
        //
        // This avoids fiddly upvalue manipulation and is compatible with Lua 5.2+.
        std::string wrapper;
        wrapper.reserve( source.size() + name.size() + 128 );
        wrapper += "local env = {} setmetatable(env, { __index = _G })\n";
        wrapper += "local f, err = load([=[\n";
        wrapper += source;
        wrapper += "\n]=], '";
        wrapper += name;
        wrapper += "', 't', env)\n";
        wrapper += "if not f then error(err) end\n";
        wrapper += "f()\n";
        wrapper += "return env\n";

        if( luaL_loadstring( L, wrapper.c_str() ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] loadChunk wrapper luaL_loadstring error: " << msg << "\n";
            lua_pop( L, 1 );
            return false;
        }

        // Execute wrapper; it returns env on success.
        if( lua_pcall( L, 0, 1, 0 ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] loadChunk wrapper lua_pcall error: " << msg << "\n";
            // Capture a full traceback for improved diagnostics (lauxlib provides luaL_traceback).
            // Push a traceback string onto the stack and print it.
            luaL_traceback( L, L, msg ? msg : "nil", 1 );
            const char* trace = lua_tostring( L, -1 );
            if( trace ) std::cout << "[LuaSandbox] loadChunk wrapper traceback: " << trace << "\n";
            // Pop traceback + original error
            lua_pop( L, 2 );
            return false;
        }

        // Expecting env table at top
        if( lua_type( L, -1 ) != LUA_TTABLE )
        {
            std::cout << "[LuaSandbox] loadChunk error: wrapper did not return env table\n";
            lua_pop( L, 1 );
            return false;
        }

        // Register helper functions into the returned env table so scripts can call debug(), get_property(), etc.
        // (env is at top of stack)
        lua_pushcfunction( L, goapie_lua_debug_func );
        lua_setfield( L, -2, "debug" );

        lua_pushcfunction( L, goapie_lua_get_property_func );
        lua_setfield( L, -2, "get_property" );

        lua_pushcfunction( L, goapie_lua_set_property_func );
        lua_setfield( L, -2, "set_property" );

        lua_pushcfunction( L, goapie_lua_entity_by_name_func );
        lua_setfield( L, -2, "entity_by_name" );

        lua_pushcfunction( L, goapie_lua_tag_set_func );
        lua_setfield( L, -2, "tag_set" );

        lua_pushcfunction( L, goapie_lua_move_agent_to_entity_func );
        lua_setfield( L, -2, "move_agent_to_entity" );

        lua_pushcfunction( L, goapie_lua_estimate_heuristic_func );
        lua_setfield( L, -2, "estimate_heuristic" );

        // Store env in registry under the chunk name
        lua_pushstring( L, name.c_str() ); // key
        lua_pushvalue( L, -2 );            // env
        lua_settable( L, LUA_REGISTRYINDEX ); // registry[name] = env

        // Pop env from stack
        lua_pop( L, 1 );

        // Record that this chunk was loaded.
        m_loaded.insert( name );
        return true;
    }

    // Execute an evaluation chunk: calls _G[chunkName].evaluate(params)
    bool executeEvaluate( const std::string& chunkName, const EvaluateSimulationParams& params ) const
    {
        if( !L ) return true; // permissive fallback

        // get stored env for chunkName from registry: registry[chunkName]
        lua_pushstring( L, chunkName.c_str() ); // key
        lua_gettable( L, LUA_REGISTRYINDEX );   // +1 -> env or nil
        if( lua_type( L, -1 ) != LUA_TTABLE )
        {
            lua_pop( L, 1 );
            return true; // no chunk -> permissive
        }

        lua_getfield( L, -1, "evaluate" ); // +1 (function or nil)
        if( lua_type( L, -1 ) != LUA_TFUNCTION )
        {
            lua_pop( L, 2 );
            return true;
        }

        // push params
        pushEvaluateParams( L, params );
        // call func( params )
        if( lua_pcall( L, 1, 1, 0 ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] executeEvaluate lua_pcall error: " << msg << "\n";
            // attempt full traceback
            luaL_traceback( L, L, msg ? msg : "nil", 1 );
            const char* trace = lua_tostring( L, -1 );
            if( trace ) std::cout << "[LuaSandbox] executeEvaluate traceback: " << trace << "\n";
            lua_pop( L, 2 ); // error + env table
            return false;
        }

        bool result = true;
        if( lua_isboolean( L, -1 ) )
        {
            result = (lua_toboolean( L, -1 ) != 0);
        }
        lua_pop( L, 2 ); // return + env table
        return result;
    }

    // Execute simulate: calls _G[chunkName].simulate(params)
    bool executeSimulate( const std::string& chunkName, SimulateSimulationParams& params ) const
    {
        if( !L ) return true;

        // get stored env for chunkName from registry: registry[chunkName]
        lua_pushstring( L, chunkName.c_str() ); // key
        lua_gettable( L, LUA_REGISTRYINDEX );   // +1 -> env or nil
        if( lua_type( L, -1 ) != LUA_TTABLE )
        {
            lua_pop( L, 1 );
            return true;
        }

        lua_getfield( L, -1, "simulate" ); // +1
        if( lua_type( L, -1 ) != LUA_TFUNCTION )
        {
            lua_pop( L, 2 );
            return true;
        }

        pushSimulateParams( L, params );
        if( lua_pcall( L, 1, 1, 0 ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] executeSimulate lua_pcall error: " << msg << "\n";
            luaL_traceback( L, L, msg ? msg : "nil", 1 );
            const char* trace = lua_tostring( L, -1 );
            if( trace ) std::cout << "[LuaSandbox] executeSimulate traceback: " << trace << "\n";
            lua_pop( L, 2 );
            return false;
        }

        bool result = true;
        if( lua_isboolean( L, -1 ) )
        {
            result = (lua_toboolean( L, -1 ) != 0);
        }
        lua_pop( L, 2 );
        return result;
    }

    // Execute heuristic: calls _G[chunkName].heuristic(params) and expects number.
    void executeHeuristic( const std::string& chunkName, CalculateHeuristicParams& params ) const
    {
        if( !L ) return;

        // get stored env for chunkName from registry: registry[chunkName]
        lua_pushstring( L, chunkName.c_str() ); // key
        lua_gettable( L, LUA_REGISTRYINDEX );   // +1 -> env or nil
        if( lua_type( L, -1 ) != LUA_TTABLE )
        {
            lua_pop( L, 1 );
            return;
        }

        lua_getfield( L, -1, "heuristic" ); // +1
        if( lua_type( L, -1 ) != LUA_TFUNCTION )
        {
            lua_pop( L, 2 );
            return;
        }

        pushCalculateHeuristicParams( L, params );
        if( lua_pcall( L, 1, 1, 0 ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] executeHeuristic lua_pcall error: " << msg << "\n";
            luaL_traceback( L, L, msg ? msg : "nil", 1 );
            const char* trace = lua_tostring( L, -1 );
            if( trace ) std::cout << "[LuaSandbox] executeHeuristic traceback: " << trace << "\n";
            lua_pop( L, 2 );
            return;
        }

        if( lua_isnumber( L, -1 ) )
        {
            double v = lua_tonumber( L, -1 );
            // Heuristic value is expected to be stored on params.simulation.heuristic.value
            params.simulation.heuristic.value = static_cast< float >( v );
        }

        // clear registry pointer
        lua_pushnil( L );
        lua_setfield( L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION" );
        lua_pop( L, 2 );
    }

    // Marshalling helpers: convert C++ params -> Lua tables.
    void pushEvaluateParams( lua_State* L, const EvaluateSimulationParams& params ) const
    {
        // params = { simulation = { guid = <num> }, agent = { guid = <num> }, goal = {} }
        lua_newtable( L ); // params

        // simulation
        lua_pushstring( L, "simulation" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.simulation.guid() ) );
        lua_settable( L, -3 ); // simulation.guid = <num>
        lua_settable( L, -3 ); // params.simulation = simulation table

        // agent
        lua_pushstring( L, "agent" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.agent.guid() ) );
        lua_settable( L, -3 ); // agent.guid = <num>
        lua_settable( L, -3 ); // params.agent = agent table

        // goal (placeholder table for now)
        lua_pushstring( L, "goal" );
        lua_newtable( L );
        lua_settable( L, -3 ); // params.goal = {}
    }

    void pushSimulateParams( lua_State* L, const SimulateSimulationParams& params ) const
    {
        // params = { simulation = { guid = <num> }, agent = { guid = <num> }, goal = {} }
        lua_newtable( L ); // params

        // simulation
        lua_pushstring( L, "simulation" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.simulation.guid() ) );
        lua_settable( L, -3 );
        lua_settable( L, -3 );

        // agent
        lua_pushstring( L, "agent" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.agent.guid() ) );
        lua_settable( L, -3 );
        lua_settable( L, -3 );

        // goal (placeholder)
        lua_pushstring( L, "goal" );
        lua_newtable( L );
        lua_settable( L, -3 );
    }

    void pushCalculateHeuristicParams( lua_State* L, const CalculateHeuristicParams& params ) const
    {
        // Reuse simulate marshalling for heuristic call.
        pushSimulateParams( L, static_cast< const SimulateSimulationParams& >( params ) );
    }

private:
    lua_State* L;
    // lightweight set of loaded chunk names
    mutable std::unordered_set< std::string > m_loaded;
};

#else // GIE_WITH_LUA

// Minimal runtime wrapper (no-op when GIE_WITH_LUA == 0).
class LuaSandbox
{
public:
    LuaSandbox() = default;
    ~LuaSandbox() = default;

    bool loadChunk( const std::string& /*name*/, const std::string& /*source*/ )
    {
        return true;
    }

    bool executeEvaluate( const std::string& /*chunkName*/, const EvaluateSimulationParams& params ) const
    {
        (void)params;
        return true;
    }

    bool executeSimulate( const std::string& /*chunkName*/, SimulateSimulationParams& params ) const
    {
        (void)params;
        return true;
    }

    void executeHeuristic( const std::string& /*chunkName*/, CalculateHeuristicParams& params ) const
    {
        (void)params;
    }
};

#endif // GIE_WITH_LUA

// Lua-backed ActionSimulator. Stores the source (or chunk names) and calls
// into LuaSandbox when available. Header-only stub returns permissive defaults.
class LuaActionSimulator : public ActionSimulator
{
public:
    LuaActionSimulator( const std::shared_ptr<LuaSandbox>& sandbox,
                        const std::string& name,
                        const std::string& evaluateChunk,
                        const std::string& simulateChunk,
                        const std::string& heuristicChunk = std::string(),
                        const NamedArguments& args = {} )
    : ActionSimulator( args )
    , m_name( name )
    , m_evaluateChunk( evaluateChunk )
    , m_simulateChunk( simulateChunk )
    , m_heuristicChunk( heuristicChunk )
    , m_sandbox( sandbox ? sandbox : std::make_shared< LuaSandbox >() )
    {
    }

    virtual ~LuaActionSimulator() = default;

    std::string_view name() const { return m_name; }
    StringHash hash() const override { return stringHasher( m_name ); }

    // @Return True if context meets prerequisites.
    bool evaluate( EvaluateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        return m_sandbox->executeEvaluate( m_evaluateChunk, params );
#else
        (void)params;
        return true;
#endif
    }

    // @Return True if simulation setup was successful.
    bool simulate( SimulateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        return m_sandbox->executeSimulate( m_simulateChunk, params );
#else
        (void)params;
        return true;
#endif
    }

    // Calculates heuristic value for simulation.
    void calculateHeuristic( CalculateHeuristicParams params ) const override
    {
#if GIE_WITH_LUA
        m_sandbox->executeHeuristic( m_heuristicChunk, params );
#else
        (void)params;
#endif
    }

private:
    std::string m_name;
    std::string m_evaluateChunk;
    std::string m_simulateChunk;
    std::string m_heuristicChunk;
    std::shared_ptr< LuaSandbox > m_sandbox;
};

// ActionSetEntry producing LuaActionSimulator instances.
class LuaActionSetEntry : public ActionSetEntry
{
public:
    LuaActionSetEntry( const std::shared_ptr<LuaSandbox>& sandbox,
                       const std::string& name,
                       const std::string& evaluateChunk,
                       const std::string& simulateChunk,
                       const std::string& heuristicChunk = std::string(),
                       const NamedArguments& arguments = {} )
    : m_name( name )
    , m_evaluateChunk( evaluateChunk )
    , m_simulateChunk( simulateChunk )
    , m_heuristicChunk( heuristicChunk )
    , m_arguments( arguments )
    , m_sandbox( sandbox )
    {
    }

    virtual ~LuaActionSetEntry() = default;

    std::string_view name() const override { return m_name; }
    StringHash hash() const override { return stringHasher( m_name ); }

    std::shared_ptr< ActionSimulator > simulator( const NamedArguments& /*arguments*/ ) const override
    {
        // Merge stored arguments with provided ones if needed (simple replace semantics).
        return std::make_shared< LuaActionSimulator >( m_sandbox, m_name, m_evaluateChunk, m_simulateChunk, m_heuristicChunk, m_arguments );
    }

    std::shared_ptr< Action > action( const NamedArguments& /*arguments*/ ) const override
    {
        // No-op runtime Action - planner produces Actions via ActionSetEntry when backtracking.
        return nullptr;
    }

    const NamedArguments& entryArguments() const { return m_arguments; }

private:
    std::string m_name;
    std::string m_evaluateChunk;
    std::string m_simulateChunk;
    std::string m_heuristicChunk;
    NamedArguments m_arguments;
    std::shared_ptr< LuaSandbox > m_sandbox;
};

// Helper: append Lua action entries to a planner's actionSet.
// This keeps the existing C++ entries and adds Lua-defined ones.
inline void ApplyLuaActionEntriesToPlanner( Planner& planner, const std::vector< std::shared_ptr< ActionSetEntry > >& entries )
{
    auto& actionSet = planner.actionSet();
    actionSet.reserve( actionSet.size() + entries.size() );
    for( const auto& e : entries )
    {
        if( e ) actionSet.emplace_back( e );
    }
}

} // namespace gie
