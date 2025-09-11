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
        lua_len( L, 3 );
        int len = static_cast<int>( lua_tointeger( L, -1 ) );
        lua_pop( L, 1 );
        if( len == 3 )
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

static int goapie_lua_estimate_heuristic_real_func(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushnumber(L, 0.0); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    Guid agentGuid = sim->agent().guid();
    const Entity* agentEnt = sim->context().entity( agentGuid );
    if( !agentEnt ) agentEnt = sim->world()->entity( agentGuid );
    if( !agentEnt ) { lua_pushnumber( L, 0.0 ); return 1; }

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

static int goapie_lua_estimate_heuristic_func(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "__GIE_CURRENT_SIMULATION");
    void* ud = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if(!ud) { lua_pushnumber(L, 0.0); return 1; }
    Simulation* sim = static_cast< Simulation* >( ud );

    Guid agentGuid = sim->agent().guid();
    const Entity* agentEnt = sim->context().entity( agentGuid );
    if( !agentEnt ) agentEnt = sim->world()->entity( agentGuid );
    if( !agentEnt ) { lua_pushnumber( L, 0.0 ); return 1; }

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

        lua_pushcfunction( L, goapie_lua_estimate_heuristic_func );
        lua_setfield( L, -2, "estimate_heuristic" );

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

    bool evaluate( EvaluateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        // Prefer unified lua chunk if present (m_luaChunk on the associated entry).
        // The simulator receives chunk names from the ActionSetEntry factory; keep legacy behavior here.
        return m_sandbox->executeEvaluate( m_evaluateChunk, params );
#else
        (void)params;
        return true;
#endif
    }

    bool simulate( SimulateSimulationParams params ) const override
    {
#if GIE_WITH_LUA
        return m_sandbox->executeSimulate( m_simulateChunk, params );
#else
        (void)params;
        return true;
#endif
    }

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
// This class adds a lightweight single-chunk ergonomics layer while preserving
// the legacy evaluate/simulate/heuristic fields for compatibility.
// New usage can provide a single luaChunk + single source; legacy constructors remain supported.
class LuaActionSetEntry : public ActionSetEntry
{
public:
    // Legacy ctor (kept for compatibility)
    LuaActionSetEntry( const std::shared_ptr<LuaSandbox>& sandbox,
                       const std::string& name,
                       const std::string& evaluateChunk,
                       const std::string& simulateChunk,
                       const std::string& heuristicChunk = std::string(),
                       const NamedArguments& args = {} )
    : m_name( name )
    , m_evaluateChunk( evaluateChunk )
    , m_simulateChunk( simulateChunk )
    , m_heuristicChunk( heuristicChunk )
    , m_arguments( args )
    , m_sandbox( sandbox )
    , m_evaluateSource()
    , m_simulateSource()
    , m_heuristicSource()
    , m_luaChunk() // empty by default
    , m_source()
    {
    }

    // New single-chunk ctor (preferred)
    LuaActionSetEntry( const std::shared_ptr<LuaSandbox>& sandbox,
                       const std::string& name,
                       const std::string& luaChunk,
                       const NamedArguments& args = {} )
    : m_name( name )
    , m_evaluateChunk( luaChunk )
    , m_simulateChunk( luaChunk )
    , m_heuristicChunk()
    , m_arguments( args )
    , m_sandbox( sandbox )
    , m_evaluateSource()
    , m_simulateSource()
    , m_heuristicSource()
    , m_luaChunk( luaChunk )
    , m_source()
    {
    }

    virtual ~LuaActionSetEntry() = default;

    std::string_view name() const override { return m_name; }
    StringHash hash() const override { return stringHasher( m_name ); }

    std::shared_ptr< ActionSimulator > simulator( const NamedArguments& /*arguments*/ ) const override
    {
        // Create LuaActionSimulator using stored evaluate/simulate chunk names.
        return std::make_shared< LuaActionSimulator >( m_sandbox, m_name, m_evaluateChunk, m_simulateChunk, m_heuristicChunk, m_arguments );
    }

    std::shared_ptr< Action > action( const NamedArguments& arguments ) const override
    {
        (void)arguments;
        return nullptr;
    }

    const NamedArguments& entryArguments() const { return m_arguments; }

    // Legacy chunk accessors
    std::string evaluateChunkName() const { return !m_luaChunk.empty() ? m_luaChunk : m_evaluateChunk; }
    std::string simulateChunkName() const { return !m_luaChunk.empty() ? m_luaChunk : m_simulateChunk; }
    std::string heuristicChunkName() const { return !m_luaChunk.empty() ? m_luaChunk : m_heuristicChunk; }

    // New single-chunk accessor
    std::string chunkName() const { return !m_luaChunk.empty() ? m_luaChunk : m_evaluateChunk; }

    // Source text accessors for editor integration
    // Legacy methods still behave by mirroring to the unified source when possible.
    void setEvaluateSource( const std::string& src )
    {
        m_evaluateSource = src;
        m_simulateSource = src;
        m_source = src;
    }
    void setSimulateSource( const std::string& src )
    {
        m_simulateSource = src;
        m_evaluateSource = src;
        m_source = src;
    }
    void setHeuristicSource( const std::string& src )
    {
        m_heuristicSource = src;
        // heuristic kept separate in legacy model; also set m_source for convenience.
        if (m_source.empty()) m_source = src;
    }

    // New single-source API
    void setSource( const std::string& src )
    {
        m_source = src;
        m_evaluateSource = src;
        m_simulateSource = src;
        // do not override heuristicSource
    }

    const std::string& evaluateSource() const { return m_evaluateSource.empty() ? m_source : m_evaluateSource; }
    const std::string& simulateSource() const { return m_simulateSource.empty() ? m_source : m_simulateSource; }
    const std::string& heuristicSource() const { return m_heuristicSource.empty() ? m_source : m_heuristicSource; }

    // New single-source accessor
    const std::string& source() const { return m_source; }

    // Compile/validate and load stored sources into the sandbox registry under the configured chunk names.
    // Returns true if all non-empty sources compiled and loaded successfully.
    bool compileAndLoad() const
    {
#if GIE_WITH_LUA
        m_lastCompileError.clear();

        // Prefer unified chunk if present, otherwise fall back to evaluate/simulate/heuristic.
        const std::string chunkToLoad = !m_luaChunk.empty() ? m_luaChunk : m_evaluateChunk;

        // If we have a unified source, load it under chunkToLoad.
        if( !m_source.empty() )
        {
            if( !m_sandbox->loadChunk( chunkToLoad, m_source ) )
            {
                m_lastCompileError = m_sandbox->lastError();
                return false;
            }
        }
        else
        {
            // Fallback: try individual sources as before.
            if( !m_evaluateSource.empty() )
            {
                if( !m_sandbox->loadChunk( m_evaluateChunk, m_evaluateSource ) )
                {
                    m_lastCompileError = m_sandbox->lastError();
                    return false;
                }
            }
            if( !m_simulateSource.empty() )
            {
                if( !m_sandbox->loadChunk( m_simulateChunk, m_simulateSource ) )
                {
                    m_lastCompileError = m_sandbox->lastError();
                    return false;
                }
            }
            if( !m_heuristicSource.empty() )
            {
                if( !m_sandbox->loadChunk( m_heuristicChunk, m_heuristicSource ) )
                {
                    m_lastCompileError = m_sandbox->lastError();
                    return false;
                }
            }
        }

        m_lastCompileError.clear();
        return true;
#else
        (void)m_evaluateSource; (void)m_simulateSource; (void)m_heuristicSource; (void)m_source;
        return true;
#endif
    }

    const std::string& lastCompileError() const { return m_lastCompileError; }

    void setSandbox( const std::shared_ptr< LuaSandbox >& sandbox ) { m_sandbox = sandbox; }

private:
    std::string m_name;

    // Legacy per-function chunk identifiers (kept for compatibility)
    std::string m_evaluateChunk;
    std::string m_simulateChunk;
    std::string m_heuristicChunk;

    NamedArguments m_arguments;
    std::shared_ptr< LuaSandbox > m_sandbox;

    // Legacy per-function source buffers
    std::string m_evaluateSource;
    std::string m_simulateSource;
    std::string m_heuristicSource;

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
