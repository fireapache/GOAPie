// Source/GOAPie_Tests/src/visualization_planner_setup.cpp
// Planner Setup ImGui window - simplified editor for Lua-backed planner actions.
// Persists only Lua files; window no longer persists world-setup JSON.

#include "visualization.h"
#include "goapie_lua.h"
#include "persistency.h"

#include <imgui.h>
#include "thirdparty/ImGuiColorTextEdit/TextEditor.h"
#include <vector>
#include <string>
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <filesystem>

using namespace gie;

// Simplified UI model: single chunk + single source per action.
struct SimpleActionEntry
{
    std::string name;
    bool active = true;
    std::string chunk;     // unified chunk/registry key
    std::string sourceLua; // single source buffer
};

static std::vector<SimpleActionEntry> s_actions;
static std::unordered_map<std::string, bool> s_actionEnabled;

// Modal editing state
static bool s_showLogicModal = false;
static int  s_editPlannerActionIndex = -1; // index into planner.actionSet() when modal opened
static SimpleActionEntry s_modalEditEntry;
static std::string s_modalCompileError;

// Forward
void drawPlannerSetupWindow( ExampleParameters& params );

// Main window UI (renamed to Planner Setup)
void drawPlannerSetupWindow( ExampleParameters& params )
{
    extern bool g_ShowWorldSetupWindow;
    extern std::string g_exampleName;
    if (!g_ShowWorldSetupWindow) return;

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Planner Setup", &g_ShowWorldSetupWindow))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Registered Actions (planner)");
        ImGui::Separator();

        // Columns: Name | Use | Logic
        ImGui::Columns(3, "registered_actions_cols", true);
        ImGui::TextUnformatted("Name"); ImGui::NextColumn();
        ImGui::TextUnformatted("Use"); ImGui::NextColumn();
        ImGui::TextUnformatted("Logic"); ImGui::NextColumn();
        ImGui::Separator();

        // Seed UI model from current planner actionSet (only for Lua entries)
        auto& actionSet = params.planner.actionSet();
        for (size_t ai = 0; ai < actionSet.size(); ++ai)
        {
            auto& entry = actionSet[ai];
            ImGui::PushID((int)(1000 + ai));

            // Name
            std::string nm = entry ? std::string(entry->name()) : std::string("<null>");
            ImGui::TextUnformatted(nm.c_str());
            ImGui::NextColumn();

            // Use checkbox (UI-only)
            if (s_actionEnabled.find(nm) == s_actionEnabled.end())
                s_actionEnabled[nm] = true;
            ImGui::Checkbox(("##use" + nm).c_str(), &s_actionEnabled[nm]);
            ImGui::NextColumn();

            // Logic button: only for LuaActionSetEntry
            if (entry)
            {
                auto luaEntry = std::dynamic_pointer_cast< gie::LuaActionSetEntry >( entry );
                if (luaEntry)
                {
                    // Ensure UI model has an entry for this planner action so edits persist in-memory.
                    bool foundInUi = false;
                    const std::string luaChunk = luaEntry->chunkName();
                    const std::string luaSrc = luaEntry->source();
                    for (const auto& sa : s_actions)
                    {
                        if (sa.name == nm) { foundInUi = true; break; }
                    }
                    if (!foundInUi)
                    {
                        SimpleActionEntry newSa;
                        newSa.name = nm;
                        newSa.chunk = luaChunk;
                        newSa.sourceLua = luaSrc;
                        newSa.active = true;
                        s_actions.push_back(std::move(newSa));
                    }

                    if (ImGui::Button("Edit Logic"))
                    {
                        s_modalEditEntry.name = nm;
                        s_modalEditEntry.chunk = luaChunk;
                        s_modalEditEntry.sourceLua = luaSrc;
                        s_modalEditEntry.active = true;
                        s_editPlannerActionIndex = static_cast<int>(ai);
                        s_modalCompileError.clear();
                        s_showLogicModal = true;
                    }
                }
                else
                {
                    ImGui::TextDisabled("Native");
                }
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);

        // Logic editor modal for planner action entries (Lua)
        if (s_showLogicModal)
        {
            ImGui::OpenPopup("Edit Action Logic");
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Edit Action Logic", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
            {
                static TextEditor s_logicEditor;
                static bool s_logicEditorInit = false;

                if (ImGui::IsWindowAppearing() || !s_logicEditorInit)
                {
                    s_logicEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
                    s_logicEditor.SetPalette(TextEditor::GetDarkPalette());
                    s_logicEditor.SetText(s_modalEditEntry.sourceLua);
                    s_logicEditorInit = true;
                }

                ImGui::Text("Editing: %s", s_modalEditEntry.name.c_str());
                ImGui::Separator();

                ImVec2 editorSize = ImGui::GetContentRegionAvail() - ImVec2{ 0, 64 };
                s_logicEditor.Render("##lua_editor", editorSize, true);
                if (s_logicEditor.IsTextChanged())
                {
                    s_modalEditEntry.sourceLua = s_logicEditor.GetText();
                }

                ImGui::Separator();

                if (!s_modalCompileError.empty())
                {
                    ImGui::TextColored(ImVec4(1.0f,0.2f,0.2f,1.0f), "Compile error:");
                    ImGui::TextWrapped("%s", s_modalCompileError.c_str());
                    ImGui::Separator();
                }

                // Compile button: validate Lua syntax via compileAndLoad (sets both evaluate & simulate sources)
                if (ImGui::Button("Compile"))
                {
                    if (s_editPlannerActionIndex >= 0 && s_editPlannerActionIndex < (int)params.planner.actionSet().size())
                    {
                        auto targetEntry = std::dynamic_pointer_cast< gie::LuaActionSetEntry >( params.planner.actionSet()[ s_editPlannerActionIndex ] );
                        if (targetEntry)
                        {
                            targetEntry->setSource( s_modalEditEntry.sourceLua );
                            bool ok = targetEntry->compileAndLoad();
                            if (ok)
                            {
                                // Update UI model
                                bool found = false;
                                for (auto& sa : s_actions)
                                {
                                    if (sa.name == s_modalEditEntry.name)
                                    {
                                        sa.chunk = targetEntry->chunkName();
                                        sa.sourceLua = s_modalEditEntry.sourceLua;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                {
                                    SimpleActionEntry newSa;
                                    newSa.name = s_modalEditEntry.name;
                                    newSa.chunk = targetEntry->chunkName();
                                    newSa.sourceLua = s_modalEditEntry.sourceLua;
                                    newSa.active = true;
                                    s_actions.push_back(std::move(newSa));
                                }

                                s_modalCompileError.clear();
                                // keep modal open
                            }
                            else
                            {
                                s_modalCompileError = targetEntry->lastCompileError();
                            }
                        }
                    }
                }

                ImGui::SameLine();
                // Save button: write Lua file only, do not compile/close
                if (ImGui::Button("Save"))
                {
                    // Determine path: <exeDir>/scripts/<exampleName>_lua/<ActionName>.lua
                    std::string exeDir = gie::persistency::executableDirectory();
                    const std::string folderName = (g_exampleName.empty() ? std::string("example") : g_exampleName) + "_lua";
                    std::string scriptsDir = gie::persistency::joinPath( exeDir, "scripts" );
                    std::string actionDir = gie::persistency::joinPath( scriptsDir, folderName );
                    try
                    {
                        std::filesystem::create_directories( actionDir );
                        const std::string filePath = gie::persistency::joinPath( actionDir, s_modalEditEntry.name + ".lua" );
                        std::ofstream out( filePath, std::ios::binary | std::ios::trunc );
                        if (out.is_open())
                        {
                            out.write( s_modalEditEntry.sourceLua.data(), static_cast<std::streamsize>( s_modalEditEntry.sourceLua.size() ) );
                            out.close();
                        }
                    }
                    catch( ... )
                    {
                        // ignore filesystem errors for now
                    }

                    // Update UI model in-memory
                    bool found = false;
                    for (auto& sa : s_actions)
                    {
                        if (sa.name == s_modalEditEntry.name)
                        {
                            sa.chunk = s_modalEditEntry.chunk;
                            sa.sourceLua = s_modalEditEntry.sourceLua;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        SimpleActionEntry newSa;
                        newSa.name = s_modalEditEntry.name;
                        newSa.chunk = s_modalEditEntry.chunk;
                        newSa.sourceLua = s_modalEditEntry.sourceLua;
                        newSa.active = true;
                        s_actions.push_back(std::move(newSa));
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Close"))
                {
                    s_showLogicModal = false;
                    s_editPlannerActionIndex = -1;
                    s_modalCompileError.clear();
                    // reset editor init so it re-initializes next time modal opens
                    /* if TextEditor is present it will be a static in this scope; reset its init flag */
                    // Note: s_logicEditorInit is static inside this modal scope; reset via a second static variable trick
                    // (we declared s_logicEditorInit above in this scope; set it to false to force re-init)
                    s_logicEditorInit = false;
                    ImGui::CloseCurrentPopup();
                }

                // Short help text about available GOAPie Lua helpers
                ImGui::Separator();
                ImGui::TextWrapped("Available GOAPie helpers in Lua: debug(), get_property(guid, prop), set_property(guid, prop, val), entity_by_name(name), tag_set(name), move_agent_to_entity(guid), estimate_heuristic(params)");
                ImGui::EndPopup();
            }
        }
    }

    ImGui::End();
}
