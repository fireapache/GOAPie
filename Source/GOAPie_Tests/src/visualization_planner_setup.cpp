// Source/GOAPie_Tests/src/visualization_planner_setup.cpp
// Planner Setup ImGui window - simplified editor for Lua-backed planner actions.
// Persists only Lua files; window no longer persists world-setup JSON.

#include "visualization.h"

#include "TextEditor.h"
#include "goapie_lua.h"
#include "persistency.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

#include <unordered_map>
#include <regex>
#include <map>

using namespace gie;

// Simplified UI model: single chunk + single source per action.
struct SimpleActionEntry
{
	std::string name;
	bool active = true;
	std::string chunk;   	// unified chunk/registry key
	std::string sourceLua; // single source buffer
};

static std::vector< SimpleActionEntry > s_actions;
static std::unordered_map< std::string, bool > s_actionEnabled;

// Planner state persistence
static bool s_plannerStateLoaded = false;
static bool s_plannerStateDirty = false;

// Modal editing state
static bool s_showLogicModal = false;
static bool s_canSaveLuaFile = false;
static int s_editPlannerActionIndex = -1; // index into planner.actionSet() when modal opened
static SimpleActionEntry s_modalEditEntry;
static std::string s_modalCompileError;
static std::map<int, std::string> markers;

// Transient modal message (mutually exclusive) - shown for a limited time
static std::string s_modalTransientMessage;
static ImVec4 s_modalTransientColor = ImVec4( 0, 0, 0, 0 );
static double s_modalTransientExpire = 0.0; // ImGui::GetTime() + seconds
static ImVec2 s_bottomGroupSize = ImVec2( 0, 0 );

// New action creation state
static char s_newActionName[64] = "";
static std::string s_createStatusMsg;
static ImVec4 s_createStatusColor = ImVec4( 0, 0, 0, 0 );
static double s_createStatusExpire = 0.0;

bool extractLastNumberAndSuffix( const std::string& s, std::string& lastNumber, std::string& suffix )
{
static const std::regex re( R"((?:.*\D)?(\d+)\D*(.*)$)" );
std::smatch m;
if( std::regex_search( s, m, re ) && m.size() >= 3 )
{
lastNumber = m[ 1 ].str();
std::string afterNumber = m[ 0 ].str().substr( m.position( 1 ) + m.length( 1 ) + 2 );
suffix = afterNumber.replace( afterNumber.size() - 17, 17, "" ); // trim end
return true;
}
return false;
}

// Planner state persistence helper functions
static std::string plannerStateFilePath()
{
    extern std::string g_exampleName;
    std::string exeDir = gie::persistency::executableDirectory();
    const std::string folderName = ( g_exampleName.empty() ? std::string( "example" ) : g_exampleName ) + "_lua";
    std::string scriptsDir = gie::persistency::joinPath( exeDir, "scripts" );
    std::string actionDir = gie::persistency::joinPath( scriptsDir, folderName );
    return gie::persistency::joinPath( actionDir, "planner.json" );
}

static void loadPlannerSetupState()
{
    if( s_plannerStateLoaded )
        return;
    
    s_plannerStateLoaded = true;
    
    const std::string filePath = plannerStateFilePath();
    std::ifstream in( filePath, std::ios::binary );
    if( !in.is_open() )
        return; // File doesn't exist, use defaults
    
    std::ostringstream oss;
    oss << in.rdbuf();
    const std::string data = oss.str();
    if( data.empty() )
        return;
    
    try
    {
        gie::persistency::json::Value root = gie::persistency::json::Value::parse( data );
        if( !root.isObject() )
            return;
        
        const auto& rootObj = root.asObject();
        
        // Check format version
        auto itFmt = rootObj.find( "formatVersion" );
        if( itFmt == rootObj.end() || !itFmt->second.isNumber() )
            return;
        const int formatVersion = static_cast<int>( itFmt->second.asNumber() );
        if( formatVersion != 1 )
            return;
        
        // Load actions
        auto itActions = rootObj.find( "actions" );
        if( itActions != rootObj.end() && itActions->second.isObject() )
        {
            const auto& actionsObj = itActions->second.asObject();
            for( const auto& kv : actionsObj )
            {
                const std::string& actionName = kv.first;
                if( kv.second.isObject() )
                {
                    const auto& actionData = kv.second.asObject();
                    auto itActive = actionData.find( "active" );
                    if( itActive != actionData.end() && itActive->second.isBoolean() )
                    {
                        s_actionEnabled[ actionName ] = itActive->second.asBoolean();
                    }
                }
            }
        }
    }
    catch( ... )
    {
        // Ignore parsing errors, use defaults
    }
}

static void savePlannerSetupState()
{
    const std::string filePath = plannerStateFilePath();
    
    try
    {
        // Build JSON
        gie::persistency::json::Object rootObj;
        rootObj[ "formatVersion" ] = 1.0;
        
        gie::persistency::json::Object actionsObj;
        for( const auto& kv : s_actionEnabled )
        {
            gie::persistency::json::Object actionData;
            actionData[ "active" ] = kv.second;
            actionsObj[ kv.first ] = gie::persistency::json::Value{ std::move( actionData ) };
        }
        rootObj[ "actions" ] = gie::persistency::json::Value{ std::move( actionsObj ) };
        
        gie::persistency::json::Value root{ std::move( rootObj ) };
        std::string jsonText = root.dump( 2 );
        
        // Ensure directory exists
        std::filesystem::create_directories( std::filesystem::path( filePath ).parent_path() );
        
        // Write to file
        std::ofstream out( filePath, std::ios::binary | std::ios::trunc );
        if( out.is_open() )
        {
            out.write( jsonText.data(), static_cast< std::streamsize >( jsonText.size() ) );
        }
        
        s_plannerStateDirty = false;
    }
    catch( ... )
    {
        // Ignore file write errors
    }
}

// Forward
void drawPlannerSetupWindow( ExampleParameters& params );

// Helper: parse s_modalCompileError and set TextEditor error markers
static void applyCompileErrorMarkers( TextEditor & editor, const std::string & error )
{
    if( !error.empty() )
    {
		markers.clear();
        
        // get last number in error string and following text
		std::string lastNumStr, suffix;
        if( extractLastNumberAndSuffix( error, lastNumStr, suffix ) )
        {
            try
            {
                int ln = std::stoi( lastNumStr );
				markers[ ln ] = suffix;
            }
            catch( ... ) {}
		}
    }
    else
    {
		markers.clear();
    }

    editor.SetErrorMarkers( markers );
}

// Main window UI (renamed to Planner Setup)
void drawPlannerSetupWindow( ExampleParameters& params )
{
    extern bool g_ShowWorldSetupWindow;
    extern std::string g_exampleName;
    if( !g_ShowWorldSetupWindow )
        return;

    ImGui::SetNextWindowSize( ImVec2( 600, 400 ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( "Planner Setup", &g_ShowWorldSetupWindow ) )
    {
        ImGui::End();
        return;
    }

    // Load planner state once before processing actions
    loadPlannerSetupState();

    if( ImGui::CollapsingHeader( "Actions", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        ImGui::Separator();
        ImGui::TextUnformatted( "Registered Actions (planner)" );
        ImGui::Separator();

        // Columns: Name | Use | Logic | Delete
        ImGui::Columns( 4, "registered_actions_cols", true );
        ImGui::TextUnformatted( "Name" );
        ImGui::NextColumn();
        ImGui::TextUnformatted( "Use" );
        ImGui::NextColumn();
        ImGui::TextUnformatted( "Logic" );
        ImGui::NextColumn();
        ImGui::TextUnformatted( "Delete" );
        ImGui::NextColumn();
        ImGui::Separator();

        // Collect indices to delete (will be processed after iteration)
        std::vector<int> indicesToDelete;

        // Seed UI model from current planner actionSet (only for Lua entries)
        auto& actionSet = params.planner.actionSet();
        for( size_t ai = 0; ai < actionSet.size(); ++ai )
        {
            auto& entry = actionSet[ ai ];
            ImGui::PushID( ( int )( 1000 + ai ) );

            // Name
            std::string nm = entry ? std::string( entry->name() ) : std::string( "<null>" );
            ImGui::TextUnformatted( nm.c_str() );
            ImGui::NextColumn();

            // Use checkbox (UI-only)
            if( s_actionEnabled.find( nm ) == s_actionEnabled.end() )
                s_actionEnabled[ nm ] = true;
            bool oldValue = s_actionEnabled[ nm ];
            if( ImGui::Checkbox( ( "##use" + nm ).c_str(), &s_actionEnabled[ nm ] ) )
            {
                // Checkbox was toggled, save state
                if( s_actionEnabled[ nm ] != oldValue )
                {
                    savePlannerSetupState();
                }
            }
            ImGui::NextColumn();

            // Logic button: only for LuaActionSetEntry
            if( entry )
            {
                auto luaEntry = std::dynamic_pointer_cast< gie::LuaActionSetEntry >( entry );
                if( luaEntry )
                {
                    // Ensure UI model has an entry for this planner action so edits persist in-memory.
                    bool foundInUi = false;
                    const std::string luaChunk = luaEntry->chunkName();
                    const std::string luaSrc = luaEntry->source();
                    for( const auto& sa : s_actions )
                    {
                        if( sa.name == nm )
                        {
                            foundInUi = true;
                            break;
                        }
                    }
                    if( !foundInUi )
                    {
                        SimpleActionEntry newSa;
                        newSa.name = nm;
                        newSa.chunk = luaChunk;
                        newSa.sourceLua = luaSrc;
                        newSa.active = true;
                        s_actions.push_back( std::move( newSa ) );
                    }

                    if( ImGui::Button( "Edit" ) )
                    {
                        s_modalEditEntry.name = nm;
                        s_modalEditEntry.chunk = luaChunk;
                        s_modalEditEntry.sourceLua = luaSrc;
                        s_modalEditEntry.active = true;
                        s_editPlannerActionIndex = static_cast< int >( ai );
                        s_modalCompileError.clear();
                        s_showLogicModal = true;
                    }
                }
                else
                {
                    ImGui::TextDisabled( "Native" );
                }
            }

            ImGui::NextColumn();

            // Delete button: only for LuaActionSetEntry
            if( entry )
            {
                auto luaEntry = std::dynamic_pointer_cast< gie::LuaActionSetEntry >( entry );
                if( luaEntry )
                {
                    if( ImGui::Button( "Delete" ) )
                    {
                        indicesToDelete.push_back( static_cast<int>( ai ) );
                    }
                }
                else
                {
                    ImGui::TextDisabled( "-" );
                }
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns( 1 );

        // Process deletions after iteration
        if( !indicesToDelete.empty() )
        {
            // Sort in descending order to delete from back to front
            std::sort( indicesToDelete.rbegin(), indicesToDelete.rend() );
            
            for( int idx : indicesToDelete )
            {
                if( idx >= 0 && idx < static_cast<int>( actionSet.size() ) )
                {
                    auto& entry = actionSet[ idx ];
                    if( entry )
                    {
                        auto luaEntry = std::dynamic_pointer_cast< gie::LuaActionSetEntry >( entry );
                        if( luaEntry )
                        {
            // Remove from planner action set
            actionSet.erase( actionSet.begin() + idx );
            
            // Remove from UI model
            std::string entryName = std::string( luaEntry->name() );
                            s_actions.erase( 
                                std::remove_if( s_actions.begin(), s_actions.end(),
                                    [&entryName]( const SimpleActionEntry& sa ) {
                                        return sa.name == entryName;
                                    }), 
                                s_actions.end() );
                            
                            // Remove from enabled map
                            s_actionEnabled.erase( entryName );
                            
                            // Delete the Lua file
                            std::string exeDir = gie::persistency::executableDirectory();
                            const std::string folderName = ( g_exampleName.empty() ? std::string( "example" ) : g_exampleName ) + "_lua";
                            std::string scriptsDir = gie::persistency::joinPath( exeDir, "scripts" );
                            std::string actionDir = gie::persistency::joinPath( scriptsDir, folderName );
                            std::string filePath = gie::persistency::joinPath( actionDir, entryName + ".lua" );
                            try
                            {
                                std::filesystem::remove( filePath );
                            }
                            catch( ... )
                            {
                                // Ignore file deletion errors
                            }
                        }
                    }
                }
            }
            
            // Re-run planner.plan() after deletions
            params.planner.plan();
            
            // Save planner state after deletions
            savePlannerSetupState();
        }

        ImGui::Separator();
        
        // Create new action section
        ImGui::Text( "Create New Action:" );
        ImGui::PushItemWidth( 300 );
        ImGui::InputText( "##new_action_name", s_newActionName, sizeof( s_newActionName ) );
        ImGui::PopItemWidth();
        ImGui::SameLine();
        
        if( ImGui::Button( "Create Action" ) )
        {
            std::string newName = s_newActionName;
            
            // Validate name: only letters A-Z, a-z
            std::regex namePattern( "^[A-Za-z]+$" );
            bool validName = std::regex_match( newName, namePattern );
            
            if( newName.empty() )
            {
                s_createStatusMsg = "Name cannot be empty";
                s_createStatusColor = ImVec4( 1.0f, 0.2f, 0.2f, 1.0f );
                s_createStatusExpire = ImGui::GetTime() + 3.0;
            }
            else if( !validName )
            {
                s_createStatusMsg = "Name can only contain letters (A-Z, a-z)";
                s_createStatusColor = ImVec4( 1.0f, 0.2f, 0.2f, 1.0f );
                s_createStatusExpire = ImGui::GetTime() + 3.0;
            }
            else
            {
                // Check for duplicate names
                bool duplicate = false;
                for( const auto& entry : actionSet )
                {
                    if( entry && entry->name() == newName )
                    {
                        duplicate = true;
                        break;
                    }
                }
                
                if( duplicate )
                {
                    s_createStatusMsg = "Action with this name already exists";
                    s_createStatusColor = ImVec4( 1.0f, 0.2f, 0.2f, 1.0f );
                    s_createStatusExpire = ImGui::GetTime() + 3.0;
                }
                else
                {
                    // Create template Lua script
                    std::string templateScript = 
                        "-- " + newName + " action\n"
                        "\n"
                        "function evaluate(params)\n"
                        "    -- Return true if this action can be executed with the given parameters\n"
                        "    return false\n"
                        "end\n"
                        "\n"
                        "function simulate(params)\n"
                        "    -- Apply the effects of this action to the world state\n"
                        "    -- Return true if simulation was successful\n"
                        "    return false\n"
                        "end\n"
                        "\n"
                        "function heuristic(params)\n"
                        "    -- Return estimated cost/priority for this action (lower is better)\n"
                        "    return 0\n"
                        "end\n";
                    
                    try
                    {
                        // Create the Lua action entry with a new sandbox
                        auto sandbox = std::make_shared< gie::LuaSandbox >();
                        std::string chunkName = newName + "_chunk";
                        auto newLuaEntry = std::make_shared< gie::LuaActionSetEntry >(
                            sandbox, newName, chunkName, gie::NamedArguments{} );
                        
                        newLuaEntry->setSource( templateScript );
                        bool compiled = newLuaEntry->compileAndLoad();
                        
                        if( compiled )
                        {
                            // Add to planner action set
                            params.planner.actionSet().push_back( newLuaEntry );
                            
                            // Add to UI model
                            SimpleActionEntry newSa;
                            newSa.name = newName;
                            newSa.chunk = chunkName;
                            newSa.sourceLua = templateScript;
                            newSa.active = true;
                            s_actions.push_back( std::move( newSa ) );
                            
                            // Enable by default
                            s_actionEnabled[ newName ] = true;
                            
                            // Save planner state
                            savePlannerSetupState();
                            
                            // Save the Lua file
                            std::string exeDir = gie::persistency::executableDirectory();
                            const std::string folderName = ( g_exampleName.empty() ? std::string( "example" ) : g_exampleName ) + "_lua";
                            std::string scriptsDir = gie::persistency::joinPath( exeDir, "scripts" );
                            std::string actionDir = gie::persistency::joinPath( scriptsDir, folderName );
                            std::string filePath = gie::persistency::joinPath( actionDir, newName + ".lua" );
                            
                            std::filesystem::create_directories( actionDir );
                            std::ofstream out( filePath, std::ios::binary | std::ios::trunc );
                            if( out.is_open() )
                            {
                                out.write( templateScript.data(), static_cast< std::streamsize >( templateScript.size() ) );
                                out.close();
                            }
                            
                            // Re-run planner.plan() after creation
                            params.planner.plan();
                            
                            // Clear the input and show success
                            std::memset( s_newActionName, 0, sizeof( s_newActionName ) );
                            s_createStatusMsg = "Action '" + newName + "' created successfully!";
                            s_createStatusColor = ImVec4( 0.2f, 1.0f, 0.2f, 1.0f );
                            s_createStatusExpire = ImGui::GetTime() + 3.0;
                        }
                        else
                        {
                            s_createStatusMsg = "Failed to compile template script: " + newLuaEntry->lastCompileError();
                            s_createStatusColor = ImVec4( 1.0f, 0.2f, 0.2f, 1.0f );
                            s_createStatusExpire = ImGui::GetTime() + 5.0;
                        }
                    }
                    catch( const std::exception& e )
                    {
                        s_createStatusMsg = "Error creating action: " + std::string( e.what() );
                        s_createStatusColor = ImVec4( 1.0f, 0.2f, 0.2f, 1.0f );
                        s_createStatusExpire = ImGui::GetTime() + 5.0;
                    }
                }
            }
        }
        
        // Display creation status message
        double now = ImGui::GetTime();
        if( s_createStatusExpire > now && !s_createStatusMsg.empty() )
        {
            ImGui::TextColored( s_createStatusColor, "%s", s_createStatusMsg.c_str() );
        }

        // Logic editor modal for planner action entries (Lua)
        if( s_showLogicModal )
        {
            ImGui::OpenPopup( "Edit Action Logic" );
            ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
            ImGui::SetNextWindowSize( ImVec2( 800, 600 ), ImGuiCond_Appearing );
            if( ImGui::BeginPopupModal(
                    "Edit Action Logic",
                    nullptr,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings ) )
            {
                static TextEditor s_logicEditor;
                static bool s_logicEditorInit = false;

                if( ImGui::IsWindowAppearing() || !s_logicEditorInit )
                {
                    s_logicEditor.SetLanguageDefinition( TextEditor::LanguageDefinition::Lua() );
                    s_logicEditor.SetPalette( TextEditor::GetDarkPalette() );
                    s_logicEditor.SetText( s_modalEditEntry.sourceLua );
                    s_logicEditor.SetShowWhitespaces( false );
                    s_logicEditorInit = true;
                }

                ImGui::Text( "Editing: %s", s_modalEditEntry.name.c_str() );
                ImGui::Separator();

                ImVec2 editorSize = ImGui::GetContentRegionAvail() - ImVec2{ 0, s_bottomGroupSize.y };
                s_logicEditor.Render( "##lua_editor", editorSize, true );
                if( s_logicEditor.IsTextChanged() )
                {
                    s_modalEditEntry.sourceLua = s_logicEditor.GetText();
                    s_canSaveLuaFile = false;
                }

                ImGui::BeginGroup();
                ImGui::Separator();

                if( !markers.empty() )
                {
					for( const auto& kv : markers )
					{
						ImGui::TextColored( /*red*/ ImVec4( 1.0f, 0.2f, 0.2f, 1.0f ), "Line %d: ", kv.first );
						ImGui::SameLine();
						ImGui::TextColored( /*yellow*/ ImVec4( 1.0f, 1.0f, 0.2f, 1.0f ), "%s", kv.second.c_str() );
						ImGui::SameLine();
                        if( ImGui::SmallButton( "Go" ) )
                        {
                            s_logicEditor.SetCursorPosition( TextEditor::Coordinates( kv.first - 1, 0 ) );
						}
					}
                    ImGui::Separator();
                }

                // Compile button: validate Lua syntax via compileAndLoad (sets both evaluate & simulate sources)
                if( ImGui::Button( "Compile" ) )
                {
                    if( s_editPlannerActionIndex >= 0 && s_editPlannerActionIndex < ( int )params.planner.actionSet().size() )
                    {
                        auto targetEntry = std::dynamic_pointer_cast< gie::LuaActionSetEntry >(
                            params.planner.actionSet()[ s_editPlannerActionIndex ] );
                        if( targetEntry )
                        {
                            // Create a temporary sandbox and entry for compilation testing without modifying the original
                            auto tempSandbox = std::make_shared< gie::LuaSandbox >();
                            auto tempEntry = std::make_shared< gie::LuaActionSetEntry >(
                                tempSandbox, 
                                std::string( targetEntry->name() ), 
                                targetEntry->chunkName(),
                                gie::NamedArguments{} );
                            tempEntry->setSource( s_modalEditEntry.sourceLua );
                            
                            bool ok = tempEntry->compileAndLoad();
                            if( ok )
                            {
                                // Update UI model
                                bool found = false;
                                for( auto& sa : s_actions )
                                {
                                    if( sa.name == s_modalEditEntry.name )
                                    {
                                        sa.chunk = targetEntry->chunkName();
                                        sa.sourceLua = s_modalEditEntry.sourceLua;
                                        found = true;
                                        break;
                                    }
                                }
                                if( !found )
                                {
                                    SimpleActionEntry newSa;
                                    newSa.name = s_modalEditEntry.name;
                                    newSa.chunk = targetEntry->chunkName();
                                    newSa.sourceLua = s_modalEditEntry.sourceLua;
                                    newSa.active = true;
                                    s_actions.push_back( std::move( newSa ) );
                                }

                                s_modalCompileError.clear();
                                s_canSaveLuaFile = true;

                                // Clear editor error markers on successful compile
                                applyCompileErrorMarkers( s_logicEditor, std::string() );

                                // Set transient success message (mutually exclusive)
                                s_modalTransientMessage = "Compiled successfully!";
                                s_modalTransientColor = ImVec4( 0.2f, 1.0f, 0.2f, 1.0f );
                                s_modalTransientExpire = ImGui::GetTime() + 5.0;
                            }
                            else
                            {
                                s_modalCompileError = tempEntry->lastCompileError();
                                s_canSaveLuaFile = false;

                                // Parse compile error and set editor markers
                                applyCompileErrorMarkers( s_logicEditor, s_modalCompileError );

                                // Set transient failure message (mutually exclusive)
                                s_modalTransientMessage = "Compilation failed!";
                                s_modalTransientColor = ImVec4( 1.0f, 0.2f, 0.2f, 1.0f );
                                s_modalTransientExpire = ImGui::GetTime() + 5.0;
                            }
                        }
                    }
                }

                ImGui::SameLine();

                // Save button: write Lua file if code compiles

                if( !s_canSaveLuaFile )
                    ImGui::BeginDisabled();

                if( ImGui::Button( "Save" ) && s_canSaveLuaFile )
                {
                    // Set the source on the actual entry and compile it
                    if( s_editPlannerActionIndex >= 0 && s_editPlannerActionIndex < ( int )params.planner.actionSet().size() )
                    {
                        auto targetEntry = std::dynamic_pointer_cast< gie::LuaActionSetEntry >(
                            params.planner.actionSet()[ s_editPlannerActionIndex ] );
                        if( targetEntry )
                        {
                            targetEntry->setSource( s_modalEditEntry.sourceLua );
                            bool ok = targetEntry->compileAndLoad();
                            if( !ok )
                            {
                                // Log compile error but continue with file save
                                std::cout << "[PlannerSetup] Warning: Failed to compile entry after save: " << targetEntry->lastCompileError() << std::endl;
                            }
                        }
                    }

                    // Determine path: <exeDir>/scripts/<exampleName>_lua/<ActionName>.lua
                    std::string exeDir = gie::persistency::executableDirectory();
                    const std::string folderName = ( g_exampleName.empty() ? std::string( "example" ) : g_exampleName ) + "_lua";
                    std::string scriptsDir = gie::persistency::joinPath( exeDir, "scripts" );
                    std::string actionDir = gie::persistency::joinPath( scriptsDir, folderName );
                    std::string filePath;
                    try
                    {
                        std::filesystem::create_directories( actionDir );
                        filePath = gie::persistency::joinPath( actionDir, s_modalEditEntry.name + ".lua" );
                        std::ofstream out( filePath, std::ios::binary | std::ios::trunc );
                        if( out.is_open() )
                        {
                            out.write(
                                s_modalEditEntry.sourceLua.data(),
                                static_cast< std::streamsize >( s_modalEditEntry.sourceLua.size() ) );
                            out.close();
                        }
                    }
                    catch( ... )
                    {
                        // ignore filesystem errors for now
                    }

                    // Update UI model in-memory
                    bool found = false;
                    for( auto& sa : s_actions )
                    {
                        if( sa.name == s_modalEditEntry.name )
                        {
                            sa.chunk = s_modalEditEntry.chunk;
                            sa.sourceLua = s_modalEditEntry.sourceLua;
                            found = true;
                            break;
                        }
                    }
                    if( !found )
                    {
                        SimpleActionEntry newSa;
                        newSa.name = s_modalEditEntry.name;
                        newSa.chunk = s_modalEditEntry.chunk;
                        newSa.sourceLua = s_modalEditEntry.sourceLua;
                        newSa.active = true;
                        s_actions.push_back( std::move( newSa ) );
                    }

                    // Transient file-saved message
                    s_modalTransientMessage = std::string( "File saved! " ) + filePath;
                    s_modalTransientColor = ImVec4( 0.2f, 1.0f, 0.2f, 1.0f );
                    s_modalTransientExpire = ImGui::GetTime() + 5.0;
                }

                if( !s_canSaveLuaFile )
                    ImGui::EndDisabled();

                ImGui::SameLine();
                if( ImGui::Button( "Close" ) )
                {
                    s_showLogicModal = false;
                    s_editPlannerActionIndex = -1;
                    s_modalCompileError.clear();
                    // reset editor init so it re-initializes next time modal opens
                    /* if TextEditor is present it will be a static in this scope; reset its init flag */
                    // Note: s_logicEditorInit is static inside this modal scope; reset via a second static variable trick
                    // (we declared s_logicEditorInit above in this scope; set it to false to force re-init)
                    s_logicEditorInit = false;
					markers.clear();
					s_logicEditor.SetErrorMarkers( markers );
                    ImGui::CloseCurrentPopup();
                }

                // Display transient message (mutually exclusive) to the right of buttons for up to 5 seconds
                double now = ImGui::GetTime();
                if( s_modalTransientExpire > now && !s_modalTransientMessage.empty() )
                {
                    ImGui::SameLine();
                    ImGui::TextColored( s_modalTransientColor, "%s", s_modalTransientMessage.c_str() );
                }

                // Short help text about available GOAPie Lua helpers
                ImGui::Separator();
                ImGui::TextWrapped(
                    "Available GOAPie helpers in Lua: debug(), get_property(guid, prop), set_property(guid, prop, val), "
                    "entity_by_name(name), tag_set(name), move_agent_to_entity(guid), estimate_heuristic(params)" );
                ImGui::EndGroup();

                s_bottomGroupSize = ImGui::GetItemRectSize();

                ImGui::EndPopup();
            }
        }
    }

    ImGui::End();
}
