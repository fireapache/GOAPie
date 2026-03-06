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

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <cctype>

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
    return 0;
}

static int goapie_lua_debug_func(lua_State* L)
{
    const char* msg = lua_tostring(L, 1);
    if (msg) std::cout << "[Lua] " << msg << "\n";
    return 0;
}

static int goapie_lua_get_property_func(lua_State* L)
{
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
        lua_Integer raw = lua_tointeger( L, 3 );
        // If the property already exists as a GUID type, preserve that type.
        // GUIDs are 64-bit and would be truncated by int32_t.
        Property* existing = ent->property( std::string_view( propName ) );
        if( existing && existing->type() == Property::GUID )
        {
            Guid v = static_cast< Guid >( raw );
            Property* ppt = ent->createProperty( std::string_view( propName ), v );
            lua_pushboolean( L, ppt ? 1 : 0 );
            return 1;
        }
        int32_t v = static_cast< int32_t >( raw );
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
        lua_len( L, 3 );
        int len = static_cast<int>( lua_tointeger( L, -1 ) );
        lua_pop( L, 1 );
        // Disambiguate Vec3 vs GuidArray: check existing property type first.
        // A 3-element table could be either, so we check what the property was before.
        Property* existing = ent->property( std::string_view( propName ) );
        bool isGuidArray = existing && existing->type() == Property::GUIDArray;
        if( len == 3 && !isGuidArray )
        {
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

// set_cost(value) — sets the current simulation node's cost (g-cost component for A*)
static int goapie_lua_set_cost_func(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushboolean(L, 0); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    if( !lua_isnumber( L, 1 ) )
    {
        lua_pushboolean( L, 0 );
        return 1;
    }

    sim->cost = static_cast< float >( lua_tonumber( L, 1 ) );
    lua_pushboolean( L, 1 );
    return 1;
}

static int goapie_lua_move_agent_to_entity_func(lua_State* L)
{
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

    agentEnt->createProperty( "Location", targetPos );

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

    bool loadChunk( const std::string& name, const std::string& source )
    {
        if( !L )
        {
            std::cout << "[LuaSandbox] loadChunk aborted: lua_State is null\n";
            return false;
        }
        std::cout << "[LuaSandbox] loadChunk: " << name << " (source size=" << source.size() << ")\n";

        // Strip optional BOM
        std::string src = source;
        if( src.size() >= 3 &&
            static_cast<unsigned char>( src[0] ) == 0xEF &&
            static_cast<unsigned char>( src[1] ) == 0xBB &&
            static_cast<unsigned char>( src[2] ) == 0xBF )
        {
            src.erase( 0, 3 );
        }

        // Wrapper now returns (f, env) instead of executing f() in Lua.
        std::string wrapper;
        wrapper.reserve( src.size() + name.size() + 128 );
        wrapper += "local env = {} setmetatable(env, { __index = _G })\n";
        wrapper += "local f, err = load([=[\n";
        wrapper += src;
        wrapper += "\n]=], '";
        wrapper += name;
        wrapper += "', 't', env)\n";
        wrapper += "if not f then return nil, err end\n";
        wrapper += "return f, env\n";

        // Load the wrapper chunk and run it to obtain (f, env) on the stack.
        if( luaL_loadstring( L, wrapper.c_str() ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] loadChunk wrapper luaL_loadstring error: " << msg << "\n";
            m_lastError = msg ? msg : std::string("luaL_loadstring error");
            lua_pop( L, 1 );
            return false;
        }

        // Execute wrapper: expects 2 returns: function (or nil) and env (or error string)
        if( lua_pcall( L, 0, 2, 0 ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] loadChunk wrapper lua_pcall error: " << msg << "\n";
            luaL_traceback( L, L, msg ? msg : "nil", 1 );
            const char* trace = lua_tostring( L, -1 );
            if( trace ) std::cout << "[LuaSandbox] loadChunk wrapper traceback: " << trace << "\n";
            if( trace ) m_lastError = trace;
            else if( msg ) m_lastError = msg;
            else m_lastError = std::string("unknown lua_pcall error");
            lua_pop( L, 2 ); // pop error + traceback
            return false;
        }

        // At this point stack: -2 = f (function or nil), -1 = env (table or error string)
        if( lua_type( L, -2 ) != LUA_TFUNCTION || lua_type( L, -1 ) != LUA_TTABLE )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] loadChunk wrapper returned unexpected types: " << msg << "\n";
            m_lastError = msg ? msg : std::string("wrapper did not return (function, env)");
            lua_pop( L, 2 );
            return false;
        }

        // Set helper C functions into env BEFORE executing the compiled chunk.
        // Stack currently: f (-2), env (-1)
        lua_pushcfunction( L, goapie_lua_debug_func );
        lua_setfield( L, -2, "debug" ); // set env.debug

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

        lua_pushcfunction( L, goapie_lua_set_cost_func );
        lua_setfield( L, -2, "set_cost" );



        // Store env in registry under name so later lookups work.
        lua_pushstring( L, name.c_str() ); // key
        lua_pushvalue( L, -2 );            // push env (env is at -2 after pushes)
        lua_settable( L, LUA_REGISTRYINDEX ); // registry[name] = env

        // Execute the compiled chunk (call the function). Duplicate f to top and call.
        lua_pushvalue( L, -2 ); // duplicate function f
        if( lua_pcall( L, 0, 0, 0 ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] executing compiled chunk error: " << msg << "\n";
            luaL_traceback( L, L, msg ? msg : "nil", 1 );
            const char* trace = lua_tostring( L, -1 );
            if( trace ) std::cout << "[LuaSandbox] compiled chunk traceback: " << trace << "\n";
            if( trace ) m_lastError = trace;
            else if( msg ) m_lastError = msg;
            else m_lastError = std::string("unknown lua_pcall error");
            lua_pop( L, 2 ); // pop error + original error/trace
            // pop env (the env table remains on stack)
            if( lua_type( L, -1 ) == LUA_TTABLE ) lua_pop( L, 1 );
            return false;
        }

        // Pop env table and the original f value if still present.
        if( lua_type( L, -1 ) == LUA_TTABLE ) lua_pop( L, 1 ); // pop env
        if( lua_type( L, -1 ) == LUA_TFUNCTION ) lua_pop( L, 1 ); // pop original f (if any)

        m_loaded.insert( name );
        m_lastError.clear();
        return true;
    }

    const std::string& lastError() const { return m_lastError; }

    bool executeEvaluate( const std::string& chunkName, const EvaluateSimulationParams& params ) const
    {
        if( !L ) return true;

        lua_pushstring( L, chunkName.c_str() ); // key
        lua_gettable( L, LUA_REGISTRYINDEX );   // +1 -> env or nil
        if( lua_type( L, -1 ) != LUA_TTABLE )
        {
            lua_pop( L, 1 );
            return true;
        }

        lua_getfield( L, -1, "evaluate" ); // +1 (function or nil)
        if( lua_type( L, -1 ) != LUA_TFUNCTION )
        {
            lua_pop( L, 2 );
            return true;
        }

        pushEvaluateParams( L, params );
        if( lua_pcall( L, 1, 1, 0 ) != LUA_OK )
        {
            const char* msg = lua_tostring( L, -1 );
            if( msg ) std::cout << "[LuaSandbox] executeEvaluate lua_pcall error: " << msg << "\n";
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

    bool executeSimulate( const std::string& chunkName, SimulateSimulationParams& params ) const
    {
        if( !L ) return true;

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

    void executeHeuristic( const std::string& chunkName, CalculateHeuristicParams& params ) const
    {
        if( !L ) return;

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
            params.simulation.heuristic.value = static_cast< float >( v );
        }

        lua_pushnil( L );
        lua_setfield( L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION" );
        lua_pop( L, 2 );
    }

    void pushEvaluateParams( lua_State* L, const EvaluateSimulationParams& params ) const
    {
        // Store simulation pointer so bridge functions (get_property, etc.) can access it
        lua_pushlightuserdata( L, const_cast< Simulation* >( &params.simulation ) );
        lua_setfield( L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION" );

        lua_newtable( L ); // params

        lua_pushstring( L, "simulation" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.simulation.guid() ) );
        lua_settable( L, -3 );
        lua_settable( L, -3 );

        lua_pushstring( L, "agent" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.agent.guid() ) );
        lua_settable( L, -3 );
        lua_settable( L, -3 );

        lua_pushstring( L, "goal" );
        lua_newtable( L );
        lua_settable( L, -3 ); // params.goal = {}
    }

    void pushSimulateParams( lua_State* L, const SimulateSimulationParams& params ) const
    {
        // Store simulation pointer so bridge functions (get_property, etc.) can access it
        lua_pushlightuserdata( L, const_cast< Simulation* >( &params.simulation ) );
        lua_setfield( L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION" );

        lua_newtable( L ); // params

        lua_pushstring( L, "simulation" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.simulation.guid() ) );
        lua_settable( L, -3 );
        lua_settable( L, -3 );

        lua_pushstring( L, "agent" );
        lua_newtable( L );
        lua_pushstring( L, "guid" );
        lua_pushinteger( L, static_cast< lua_Integer >( params.agent.guid() ) );
        lua_settable( L, -3 );
        lua_settable( L, -3 );

        lua_pushstring( L, "goal" );
        lua_newtable( L );
        lua_settable( L, -3 );
    }

    void pushCalculateHeuristicParams( lua_State* L, const CalculateHeuristicParams& params ) const
    {
        pushSimulateParams( L, static_cast< const SimulateSimulationParams& >( params ) );
    }

private:
    lua_State* L;
    mutable std::unordered_set< std::string > m_loaded;
    mutable std::string m_lastError;
};

#else // GIE_WITH_LUA

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

// Lightweight Action produced by Lua-backed simulators so that backtrack()
// can identify which action was performed at each simulation node.
class LuaAction : public Action
{
	std::string m_name;
public:
	LuaAction( const std::string& name ) : m_name( name ) {}
	std::string_view name() const override { return m_name; }
	StringHash hash() const override { return stringHasher( m_name ); }
};

// Lua-backed ActionSimulator. Stores the source (or chunk names) and calls
// into LuaSandbox when available. Header-only stub returns permissive defaults.
class LuaActionSimulator : public ActionSimulator
{
public:
    LuaActionSimulator( const std::shared_ptr<LuaSandbox>& sandbox,
                        const std::string& name,
                        const std::string& chunkName,
                        const NamedArguments& args = {} )
    : ActionSimulator( args )
    , m_name( name )
    , m_chunkName( chunkName )
    , m_sandbox( sandbox ? sandbox : std::make_shared< LuaSandbox >() )
    {
    }

    virtual ~LuaActionSimulator() = default;

    std::string_view name() const { return m_name; }
    StringHash hash() const override { return stringHasher( m_name ); }

    bool evaluate( EvaluateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        // Use the unified chunk name for all Lua-backed callbacks.
        return m_sandbox->executeEvaluate( m_chunkName, params );
#else
        (void)params;
        return true;
#endif
    }

    bool simulate( SimulateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        bool ok = m_sandbox->executeSimulate( m_chunkName, params );
        if( ok )
        {
            params.simulation.actions.emplace_back( std::make_shared< LuaAction >( m_name ) );
        }
        return ok;
#else
        (void)params;
        return true;
#endif
    }

    void calculateHeuristic( CalculateHeuristicParams params ) const override
    {
#if GIE_WITH_LUA
        m_sandbox->executeHeuristic( m_chunkName, params );
#else
        (void)params;
#endif
    }

private:
    std::string m_name;
    std::string m_chunkName;
    std::shared_ptr< LuaSandbox > m_sandbox;
};

// ActionSetEntry producing LuaActionSimulator instances.
// This class adds a lightweight single-chunk ergonomics layer while preserving
// the legacy evaluate/simulate/heuristic fields for compatibility.
// New usage can provide a single luaChunk + single source; legacy constructors remain supported.
class LuaActionSetEntry : public ActionSetEntry
{
public:
    LuaActionSetEntry( const std::shared_ptr<LuaSandbox>& sandbox,
                       const std::string& name,
                       const std::string& luaChunk,
                       const NamedArguments& args = {} )
    : m_name( name )
    , m_luaChunk( luaChunk )
    , m_arguments( args )
    , m_sandbox( sandbox )
    , m_source()
    {
    }

    virtual ~LuaActionSetEntry() = default;

    std::string_view name() const override { return m_name; }
    StringHash hash() const override { return stringHasher( m_name ); }

    std::shared_ptr< ActionSimulator > simulator( const NamedArguments& /*arguments*/ ) const override
    {
        // Create LuaActionSimulator using the unified chunk name.
        return std::make_shared< LuaActionSimulator >( m_sandbox, m_name, m_luaChunk, m_arguments );
    }

    std::shared_ptr< Action > action( const NamedArguments& arguments ) const override
    {
        (void)arguments;
        return nullptr;
    }

    const NamedArguments& entryArguments() const { return m_arguments; }

    // New single-chunk accessor
    std::string chunkName() const { return m_luaChunk; }

    // New single-source API
    void setSource( const std::string& src )
    {
        m_source = src;
    }



    // New single-source accessor
    const std::string& source() const { return m_source; }

    // Validate that Lua action script defines the required functions
    bool validateActionScript(const std::string& source) const
    {
        // Simple check for function definitions
        bool hasEvaluate = source.find("function evaluate(") != std::string::npos;
        bool hasSimulate = source.find("function simulate(") != std::string::npos;
        bool hasHeuristic = source.find("function heuristic(") != std::string::npos;

        if (!hasEvaluate) {
            m_lastCompileError = "Missing required 'evaluate' function definition. Expected: function evaluate(params)";
            return false;
        }

        if (!hasSimulate) {
            m_lastCompileError = "Missing required 'simulate' function definition. Expected: function simulate(params)";
            return false;
        }

        if (!hasHeuristic) {
            m_lastCompileError = "Missing required 'heuristic' function definition. Expected: function heuristic(params)";
            return false;
        }

        return true;
    }

    // Compile/validate and load stored sources into the sandbox registry under the configured chunk names.
    // Returns true if all non-empty sources compiled and loaded successfully.
    bool compile() const
    {
#if GIE_WITH_LUA
        m_lastCompileError.clear();

        std::string sourceToCompile;

        // If we have a unified source, use it
        if( !m_source.empty() )
        {
            sourceToCompile = m_source;
        }
        // Legacy per-function sources are no longer supported - only unified source is used
        else
        {
            // Nothing to load; treat as success.
            return true;
        }

        // Validate action script before compiling
        if (!validateActionScript(sourceToCompile)) {
            return false;
        }

        // Now compile the validated source
        if( !m_sandbox->loadChunk( m_luaChunk, sourceToCompile ) )
        {
            m_lastCompileError = m_sandbox->lastError();
            return false;
        }

        m_lastCompileError.clear();
        return true;
#else
        (void)m_source;
        return true;
#endif
    }

    const std::string& lastCompileError() const { return m_lastCompileError; }

    void setSandbox( const std::shared_ptr< LuaSandbox >& sandbox ) { m_sandbox = sandbox; }

private:
    std::string m_name;

    NamedArguments m_arguments;
    std::shared_ptr< LuaSandbox > m_sandbox;



    // New unified chunk + source (preferred)
    std::string m_luaChunk;
    std::string m_source;

    mutable std::string m_lastCompileError;
};

// Helper: append Lua action entries to a planner's actionSet.
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
