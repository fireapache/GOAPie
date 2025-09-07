// Source/GOAPie_Tests/src/visualization_world_setup.cpp
// Minimal, compilation-safe World Setup ImGui window skeleton.
// This file intentionally contains a small, robust UI stub and TODOs for wiring
// to goapie_lua.h and worldsetup_persistency.h in subsequent steps.

#include "visualization.h"
#include "worldsetup_persistency.h"
#include "goapie_lua.h"

#include <imgui.h>
#include <vector>
#include <string>
#include <cstring>


struct SimpleActionEntry
{
    std::string name;
    bool active = true;
    std::string evalLua;
    std::string simulateLua;
};

struct SimpleTargetEntry
{
    int entityId = -1;
    std::string propertyName;
    std::string valueStr;
};

struct SimpleGoalEntry
{
    std::string name;
    bool active = false;
    std::vector<SimpleTargetEntry> targets;
};

static std::vector<SimpleActionEntry> s_actions;
static std::vector<SimpleGoalEntry>   s_goals;
static bool s_targetsModalOpen = false;
static int  s_editGoalIndex = -1;
static bool s_initializedForExample = false;
static std::string s_loadedExampleName;

// Forward declaration
void drawWorldSetupWindow( ExampleParameters& params );

// No-op persistence/loading placeholders (to be replaced with worldsetup_persistency.h calls)
static void tryLoadWorldSetupForExample(const std::string& exampleName, ExampleParameters& params)
{
    using namespace gie;

    if (exampleName.empty()) return;
    if (s_initializedForExample && s_loadedExampleName == exampleName) return;

    s_loadedExampleName = exampleName;
    s_actions.clear();
    s_goals.clear();

    // Attempt to load persisted data for this example.
    const std::string fileName = exampleName + "_worldsetup.json";
    WorldSetupData data;
    if ( LoadWorldSetupFromJson( data, fileName ) )
    {
        // Convert persistent data -> UI model
        for (const auto& a : data.actions)
        {
            SimpleActionEntry sa;
            sa.name = a.name;
            sa.active = a.active;
            sa.evalLua = a.evaluateSource;
            sa.simulateLua = a.simulateSource;
            s_actions.push_back( std::move(sa) );
        }

        for (const auto& g : data.goals)
        {
            SimpleGoalEntry sg;
            sg.name = g.name;
            sg.active = g.active;
            for (const auto& t : g.targets)
            {
                SimpleTargetEntry st;
                st.propertyName = t.propertyName;
                // Best-effort entity mapping: store numeric representation (may be empty)
                if ( !t.entityGuidDec.empty() )
                {
                    try
                    {
                        st.entityId = static_cast<int>( std::stoll( t.entityGuidDec ) );
                    }
                    catch( ... )
                    {
                        st.entityId = -1;
                    }
                }
                else
                {
                    st.entityId = -1;
                }

                // Serialize json value to a string for the simple editor.
                try
                {
                    st.valueStr = t.value.dump();
                }
                catch( ... )
                {
                    // Fallback: empty
                    st.valueStr.clear();
                }

                sg.targets.push_back( std::move( st ) );
            }
            s_goals.push_back( std::move( sg ) );
        }

        s_initializedForExample = true;
        return;
    }

    // Fallback default when no persisted file exists
    SimpleActionEntry a;
    a.name = "ExampleAction";
    a.evalLua = "-- evaluate(params)\nreturn true";
    a.simulateLua = "-- simulate(params)\n";
    s_actions.push_back(a);

    SimpleGoalEntry g;
    g.name = "ExampleGoal";
    g.active = true;
    s_goals.push_back(g);

    s_initializedForExample = true;
}

static void trySaveWorldSetupForExample(const std::string& exampleName, ExampleParameters& params)
{
    using namespace gie;
    (void)params;

    if (exampleName.empty()) return;

    WorldSetupData data;
    data.actions.reserve( s_actions.size() );
    for (const auto& a : s_actions)
    {
        WorldSetupAction wa;
        wa.name = a.name;
        wa.active = a.active;
        wa.evaluateSource = a.evalLua;
        wa.simulateSource = a.simulateLua;
        // heuristic left empty for now
        data.actions.emplace_back( std::move( wa ) );
    }

    data.goals.reserve( s_goals.size() );
    for (const auto& g : s_goals)
    {
        WorldSetupGoal wg;
        wg.name = g.name;
        wg.active = g.active;
        for (const auto& t : g.targets)
        {
            WorldSetupTarget wt;
            // Best-effort: store entityId as decimal string when present
            if ( t.entityId >= 0 )
            {
                wt.entityGuidDec = std::to_string( static_cast<long long>( t.entityId ) );
            }
            else
            {
                wt.entityGuidDec.clear();
            }

            wt.propertyName = t.propertyName;

            // Store value as a JSON string for now (simple editor)
            try
            {
                using namespace gie::persistency;
                wg.targets.emplace_back( WorldSetupTarget{ wt.entityGuidDec, wt.propertyName, json::Value( t.valueStr ) } );
            }
            catch( ... )
            {
                // fallback to empty value
                wg.targets.emplace_back( WorldSetupTarget{ wt.entityGuidDec, wt.propertyName, gie::persistency::json::Value() } );
            }
        }
        data.goals.emplace_back( std::move( wg ) );
    }

    const std::string fileName = exampleName + "_worldsetup.json";
    SaveWorldSetupToJson( data, fileName );
}

// Minimal apply stub
static void applyWorldSetupToPlanner(gie::Planner& planner, ExampleParameters& params)
{
    using namespace gie;

    // Build transient WorldSetupData from current UI model
    WorldSetupData data;
    data.actions.reserve( s_actions.size() );
    for (const auto& a : s_actions)
    {
        WorldSetupAction wa;
        wa.name = a.name;
        wa.active = a.active;
        wa.evaluateSource = a.evalLua;
        wa.simulateSource = a.simulateLua;
        data.actions.emplace_back( std::move( wa ) );
    }

    data.goals.reserve( s_goals.size() );
    for (const auto& g : s_goals)
    {
        WorldSetupGoal wg;
        wg.name = g.name;
        wg.active = g.active;
        for (const auto& t : g.targets)
        {
            WorldSetupTarget wt;
            wt.entityGuidDec = ( t.entityId >= 0 ) ? std::to_string( static_cast<long long>( t.entityId ) ) : std::string();
            wt.propertyName = t.propertyName;
            try
            {
                wt.value = gie::persistency::json::Value( t.valueStr );
            }
            catch( ... )
            {
                wt.value = gie::persistency::json::Value();
            }
            wg.targets.emplace_back( std::move( wt ) );
        }
        data.goals.emplace_back( std::move( wg ) );
    }

    // Create Lua-backed ActionSetEntry instances and append to planner
    auto entries = BuildLuaActionEntriesFromSetup( data );
    ApplyLuaActionEntriesToPlanner( planner, entries );

    // Append active goal targets into the running Goal (append-only)
    for (const auto& g : data.goals)
    {
        if ( !g.active ) continue;

        for (const auto& t : g.targets)
        {
            // Try to interpret stored entityGuidDec as numeric Guid
            gie::Guid entityGuid{ 0 };
            bool haveGuid = false;
            if ( !t.entityGuidDec.empty() )
            {
                try
                {
                    entityGuid = static_cast< gie::Guid >( std::stoull( t.entityGuidDec ) );
                    if ( params.world.entity( entityGuid ) != nullptr )
                    {
                        haveGuid = true;
                    }
                }
                catch( ... )
                {
                    haveGuid = false;
                }
            }

            if ( !haveGuid ) continue;

            // Convert json::Value -> Property::Variant (best-effort)
            Property::Variant var = false;
            try
            {
                const auto& jv = t.value;
                using namespace gie::persistency;
                if ( jv.isBoolean() )
                {
                    var = jv.asBoolean();
                }
                else if ( jv.isNumber() )
                {
                    double n = jv.asNumber();
                    // prefer integer when exact
                    if ( std::floor( n ) == n && n >= std::numeric_limits<int32_t>::min() && n <= std::numeric_limits<int32_t>::max() )
                        var = static_cast<int32_t>( n );
                    else
                        var = static_cast<float>( n );
                }
                else if ( jv.isString() )
                {
                    const std::string s = jv.asString();
                    // try boolean
                    if ( s == "true" ) var = true;
                    else if ( s == "false" ) var = false;
                    else
                    {
                        // try integer
                        try { var = static_cast<int32_t>( std::stoll( s ) ); }
                        catch( ... )
                        {
                            // try as Guid
                            try { var = static_cast<gie::Guid>( std::stoull( s ) ); }
                            catch( ... ) { var = static_cast<int32_t>( 0 ); }
                        }
                    }
                }
                else
                {
                    // fallback: stringify
                    var = static_cast<int32_t>( 0 );
                }
            }
            catch( ... )
            {
                var = static_cast<int32_t>( 0 );
            }

            // Convert property name -> StringHash
            const auto propHash = stringHasher( t.propertyName );

            // Append NamedDefinition or Definition depending on available mapping.
            // We prefer Definition (Guid + Variant) so use entityGuid as first element.
            params.goal.targets.emplace_back( std::make_pair( entityGuid, var ) );
        }
    }
}

// Targets modal (simple, safe)
static void drawTargetsModal()
{
    if (!s_targetsModalOpen) return;

    ImGui::OpenPopup("WorldSetup - Edit Targets");
    if (!ImGui::BeginPopupModal("WorldSetup - Edit Targets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::EndPopup();
        return;
    }

    if (s_editGoalIndex < 0 || s_editGoalIndex >= (int)s_goals.size())
    {
        ImGui::Text("Invalid goal selected.");
        if (ImGui::Button("Close")) { s_targetsModalOpen = false; s_editGoalIndex = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
        return;
    }

    SimpleGoalEntry& goal = s_goals[s_editGoalIndex];

    ImGui::Text("Editing targets for: %s", goal.name.c_str());
    ImGui::Separator();

    for (size_t i = 0; i < goal.targets.size(); ++i)
    {
        auto& t = goal.targets[i];
        ImGui::PushID((int)i);

        ImGui::InputInt("Entity ID", &t.entityId);

        char propBuf[128] = {0};
        strncpy(propBuf, t.propertyName.c_str(), sizeof(propBuf)-1);
        if (ImGui::InputText("Property", propBuf, sizeof(propBuf)))
            t.propertyName = std::string(propBuf);

        char valBuf[256] = {0};
        strncpy(valBuf, t.valueStr.c_str(), sizeof(valBuf)-1);
        if (ImGui::InputText("Value", valBuf, sizeof(valBuf)))
            t.valueStr = std::string(valBuf);

        if (ImGui::Button("Remove"))
        {
            goal.targets.erase(goal.targets.begin() + i);
            ImGui::PopID();
            break;
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    if (ImGui::Button("Add Target"))
    {
        SimpleTargetEntry nt;
        nt.entityId = -1;
        nt.propertyName = "prop";
        nt.valueStr = "value";
        goal.targets.push_back(nt);
    }

    ImGui::SameLine();
    if (ImGui::Button("Close"))
    {
        s_targetsModalOpen = false;
        s_editGoalIndex = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Main window UI
void drawWorldSetupWindow( ExampleParameters& params )
{
    extern bool g_ShowWorldSetupWindow;
    if (!g_ShowWorldSetupWindow) return;

    // Load once per example
    tryLoadWorldSetupForExample(g_exampleName, params);

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("World Setup", &g_ShowWorldSetupWindow))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Add Action"))
        {
            SimpleActionEntry a;
            a.name = "NewAction";
            s_actions.push_back(a);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            trySaveWorldSetupForExample(g_exampleName, params);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            s_initializedForExample = false;
            tryLoadWorldSetupForExample(g_exampleName, params);
        }

        ImGui::Separator();

        for (size_t i = 0; i < s_actions.size(); ++i)
        {
            auto& a = s_actions[i];
            ImGui::PushID((int)i);

            char nameBuf[128] = {0};
            strncpy(nameBuf, a.name.c_str(), sizeof(nameBuf)-1);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                a.name = std::string(nameBuf);

            ImGui::Checkbox("Active", &a.active);

            if (ImGui::TreeNode("Evaluate Lua"))
            {
                char buf[512] = {0};
                strncpy(buf, a.evalLua.c_str(), sizeof(buf)-1);
                if (ImGui::InputTextMultiline("##eval", buf, sizeof(buf), ImVec2(-1, 100)))
                    a.evalLua = std::string(buf);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Simulate Lua"))
            {
                char buf2[512] = {0};
                strncpy(buf2, a.simulateLua.c_str(), sizeof(buf2)-1);
                if (ImGui::InputTextMultiline("##sim", buf2, sizeof(buf2), ImVec2(-1, 100)))
                    a.simulateLua = std::string(buf2);
                ImGui::TreePop();
            }

            if (ImGui::Button("Delete Action"))
            {
                s_actions.erase(s_actions.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Goals", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Add Goal"))
        {
            SimpleGoalEntry g;
            g.name = "NewGoal";
            s_goals.push_back(g);
        }
        ImGui::Separator();

        for (size_t i = 0; i < s_goals.size(); ++i)
        {
            auto& g = s_goals[i];
            ImGui::PushID((int)i);

            char nameBuf[128] = {0};
            strncpy(nameBuf, g.name.c_str(), sizeof(nameBuf)-1);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                g.name = std::string(nameBuf);

            bool wasActive = g.active;
            if (ImGui::RadioButton("Active", g.active))
            {
                for (auto& og : s_goals) og.active = false;
                g.active = true;
            }

            if (ImGui::Button("Edit Targets"))
            {
                s_editGoalIndex = (int)i;
                s_targetsModalOpen = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Goal"))
            {
                s_goals.erase(s_goals.begin() + i);
                ImGui::PopID();
                break;
            }

            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Apply to Planner"))
    {
        applyWorldSetupToPlanner(params.planner, params);
    }

    ImGui::End();

    drawTargetsModal();
}
