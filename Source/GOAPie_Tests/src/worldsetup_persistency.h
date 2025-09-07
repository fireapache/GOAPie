#pragma once

// World Setup persistency for test-app (per-example).
// Saves/loads user-defined Lua Actions and Goals to a JSON file next to the executable.
// File name convention: <exampleName>_worldsetup.json
//
// This header is intentionally small and header-only. It uses the JSON utilities
// embedded in gie::persistency (persistency.h) and the Lua scaffolding types
// defined in goapie_lua.h for creating in-memory entries after loading.

#include <string>
#include <vector>
#include <memory>

#include "persistency.h"
#include "goapie_lua.h"

namespace gie
{
struct WorldSetupAction
{
    std::string name;
    bool active{ true };
    std::string evaluateSource;   // Lua source for Evaluate
    std::string simulateSource;   // Lua source for Simulate
    std::string heuristicSource;  // optional Lua source for heuristic
};

struct WorldSetupTarget
{
    // Original entity guid as decimal string (stored for remapping best-effort).
    // When applying to a live world we will interpret this as a Guid and attempt to
    // find the matching entity; if not found the target is skipped.
    std::string entityGuidDec;

    // Property name on the entity (string)
    std::string propertyName;

    // Typed value for the target stored as JSON value (persistency::json::Value).
    persistency::json::Value value;
};

struct WorldSetupGoal
{
    std::string name;
    bool active{ false }; // only one goal should be active at a time
    std::vector< WorldSetupTarget > targets;
};

struct WorldSetupData
{
    std::vector< WorldSetupAction > actions;
    std::vector< WorldSetupGoal > goals;
};

// Save WorldSetupData to a JSON file next to the executable.
// Uses the same executableDirectory() helper as other persistency helpers.
inline bool SaveWorldSetupToJson( const WorldSetupData& data, const std::string& fileName )
{
    using namespace gie::persistency;
    json::Object root;
    root[ "formatVersion" ] = 1.0;

    // Actions
    json::Array actionsArray;
    for( const auto& a : data.actions )
    {
        json::Object obj;
        obj[ "name" ] = a.name;
        obj[ "active" ] = a.active ? true : false;
        obj[ "evaluate" ] = a.evaluateSource;
        obj[ "simulate" ] = a.simulateSource;
        obj[ "heuristic" ] = a.heuristicSource;
        actionsArray.emplace_back( std::move( obj ) );
    }
    root[ "actions" ] = json::Value{ std::move( actionsArray ) };

    // Goals
    json::Array goalsArray;
    for( const auto& g : data.goals )
    {
        json::Object obj;
        obj[ "name" ] = g.name;
        obj[ "active" ] = g.active ? true : false;
        json::Array targetsArr;
        for( const auto& t : g.targets )
        {
            json::Object to;
            to[ "entityGuid" ] = t.entityGuidDec;
            to[ "property" ] = t.propertyName;
            to[ "value" ] = t.value;
            targetsArr.emplace_back( std::move( to ) );
        }
        obj[ "targets" ] = json::Value{ std::move( targetsArr ) };
        goalsArray.emplace_back( std::move( obj ) );
    }
    root[ "goals" ] = json::Value{ std::move( goalsArray ) };

    json::Value vroot( std::move( root ) );
    std::string jsonText = vroot.dump( 2 );

    const std::string path = persistency::joinPath( persistency::executableDirectory(), fileName );
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    if( !out.is_open() ) return false;
    out.write( jsonText.data(), static_cast<std::streamsize>( jsonText.size() ) );
    return out.good();
}

// Load WorldSetupData from a JSON file. Values are deserialized into the in-memory
// WorldSetupData structure but entity guid remapping is left to the caller when applying.
inline bool LoadWorldSetupFromJson( WorldSetupData& outData, const std::string& fileName )
{
    using namespace gie::persistency;
    const std::string path = joinPath( executableDirectory(), fileName );
    std::ifstream in( path, std::ios::binary );
    if( !in.is_open() ) return false;
    std::ostringstream oss; oss << in.rdbuf();
    const std::string data = oss.str();
    if( data.empty() ) return false;

    json::Value root;
    try
    {
        root = json::Value::parse( data );
    }
    catch( ... )
    {
        return false;
    }
    if( !root.isObject() ) return false;
    const auto& rootObj = root.asObject();

    // version (forward compatible)
    auto itFmt = rootObj.find( "formatVersion" );
    if( itFmt == rootObj.end() || !itFmt->second.isNumber() ) return false;
    const int formatVersion = static_cast<int>( itFmt->second.asNumber() );
    if( formatVersion != 1 ) return false;

    outData.actions.clear();
    outData.goals.clear();

    // Actions
    auto itActions = rootObj.find( "actions" );
    if( itActions != rootObj.end() && itActions->second.isArray() )
    {
        for( const auto& an : itActions->second.asArray() )
        {
            if( !an.isObject() ) continue;
            const auto& aobj = an.asObject();
            WorldSetupAction a;
            auto itName = aobj.find( "name" ); if( itName != aobj.end() && itName->second.isString() ) a.name = itName->second.asString();
            auto itActive = aobj.find( "active" ); if( itActive != aobj.end() && itActive->second.isBoolean() ) a.active = itActive->second.asBoolean();
            auto itEval = aobj.find( "evaluate" ); if( itEval != aobj.end() && itEval->second.isString() ) a.evaluateSource = itEval->second.asString();
            auto itSim = aobj.find( "simulate" ); if( itSim != aobj.end() && itSim->second.isString() ) a.simulateSource = itSim->second.asString();
            auto itHeu = aobj.find( "heuristic" ); if( itHeu != aobj.end() && itHeu->second.isString() ) a.heuristicSource = itHeu->second.asString();
            outData.actions.emplace_back( std::move( a ) );
        }
    }

    // Goals
    auto itGoals = rootObj.find( "goals" );
    if( itGoals != rootObj.end() && itGoals->second.isArray() )
    {
        for( const auto& gn : itGoals->second.asArray() )
        {
            if( !gn.isObject() ) continue;
            const auto& gobj = gn.asObject();
            WorldSetupGoal g;
            auto itName = gobj.find( "name" ); if( itName != gobj.end() && itName->second.isString() ) g.name = itName->second.asString();
            auto itActive = gobj.find( "active" ); if( itActive != gobj.end() && itActive->second.isBoolean() ) g.active = itActive->second.asBoolean();
            auto itTargets = gobj.find( "targets" );
            if( itTargets != gobj.end() && itTargets->second.isArray() )
            {
                for( const auto& tn : itTargets->second.asArray() )
                {
                    if( !tn.isObject() ) continue;
                    const auto& tobj = tn.asObject();
                    WorldSetupTarget t;
                    auto itEnt = tobj.find( "entityGuid" ); if( itEnt != tobj.end() && itEnt->second.isString() ) t.entityGuidDec = itEnt->second.asString();
                    auto itProp = tobj.find( "property" ); if( itProp != tobj.end() && itProp->second.isString() ) t.propertyName = itProp->second.asString();
                    auto itVal = tobj.find( "value" ); if( itVal != tobj.end() ) t.value = itVal->second;
                    g.targets.emplace_back( std::move( t ) );
                }
            }
            outData.goals.emplace_back( std::move( g ) );
        }
    }

    return true;
}

// Helper: Build in-memory Lua ActionSetEntry vector from WorldSetupData actions
inline std::vector< std::shared_ptr< ActionSetEntry > > BuildLuaActionEntriesFromSetup( const WorldSetupData& data )
{
    std::vector< std::shared_ptr< ActionSetEntry > > out;
    out.reserve( data.actions.size() );
    for( const auto& a : data.actions )
    {
        // Only include active actions
        if( !a.active ) continue;
        out.emplace_back( std::make_shared< LuaActionSetEntry >( std::make_shared< LuaSandbox >(), a.name, a.evaluateSource, a.simulateSource, a.heuristicSource ) );
    }
    return out;
}

} // namespace gie
