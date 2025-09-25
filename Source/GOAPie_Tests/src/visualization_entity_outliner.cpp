#include "visualization.h"

#include <algorithm>
#include <string>
#include <vector>
#include <cstring>

// Local state for Entity Outliner name dialog (Add/Rename)
namespace {
    enum class NameDialogMode : uint8_t { None = 0, Add, Rename };
    static NameDialogMode s_NameDialogMode = NameDialogMode::None;
    static char s_NameDialogBuf[256] = { 0 };
    static bool s_NameDialogFocus = false; // request keyboard focus on next frame

    // New: inline rename state
    static gie::Guid s_InlineRenameGuid = gie::NullGuid;
    static char s_InlineRenameBuf[256] = { 0 };
    static bool s_InlineRenameFocus = false; // request keyboard focus on next frame

    inline void CancelNameDialog()
    {
        s_NameDialogMode = NameDialogMode::None;
        s_NameDialogBuf[0] = '\0';
        s_NameDialogFocus = false;
    }

    inline void CancelInlineRename()
    {
        s_InlineRenameGuid = gie::NullGuid;
        s_InlineRenameBuf[0] = '\0';
        s_InlineRenameFocus = false;
    }
}

void drawEntityOutlinerWindow( gie::World& world )
{
    if( !g_ShowEntityOutlinerWindow ) return;

    if( ImGui::Begin( "Entity Outliner", &g_ShowEntityOutlinerWindow ) )
    {
        if( g_IsLoading )
        {
            DrawWindowLoadingOverlay();
            ImGui::End();
            return;
        }
        // If name dialog or inline rename is open and user interacts/focuses elsewhere, cancel it
        if( s_NameDialogMode != NameDialogMode::None || s_InlineRenameGuid != gie::NullGuid )
        {
            if( !ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
            {
                ImGuiIO& io = ImGui::GetIO();
                if( io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2] || io.MouseWheel != 0.0f )
                {
                    CancelNameDialog();
                    CancelInlineRename();
                }
            }
        }

        // Sync selection from other tools -> outliner
        if( g_WaypointEditSelectedGuid != gie::NullGuid )
        {
            // Sync waypoint edit selection into unified set as single selection
            g_selectedEntityGuids.clear();
            g_selectedEntityGuids.insert( g_WaypointEditSelectedGuid );
        }

        // Controls
        bool hasSelection = false;
        if( g_selectedEntityGuids.size() == 1 )
        {
            gie::Guid g = *g_selectedEntityGuids.begin();
            hasSelection = ( g != gie::NullGuid && world.entity( g ) != nullptr );
        }

        ImGui::BeginDisabled( s_NameDialogMode != NameDialogMode::None || s_InlineRenameGuid != gie::NullGuid );
        if( ImGui::Button( "+ Add" ) )
        {
            // Start name dialog for Add
            s_NameDialogMode = NameDialogMode::Add;
            s_NameDialogBuf[0] = '\0';
            s_NameDialogFocus = true;
        }
        ImGui::SameLine();

        ImGui::BeginDisabled( !hasSelection );
        if( ImGui::Button( "Delete" ) )
        {
            if( hasSelection )
            {
                gie::Guid g = *g_selectedEntityGuids.begin();
                world.removeEntity( g );
                if( g_WaypointEditSelectedGuid == g )
                {
                    g_WaypointEditSelectedGuid = gie::NullGuid;
                }
                g_selectedEntityGuids.clear();
            }
        }
        ImGui::SameLine();
        if( ImGui::Button( "Rename" ) )
        {
                if( hasSelection )
                {
                    s_NameDialogMode = NameDialogMode::Rename;
                    gie::Guid g = *g_selectedEntityGuids.begin();
                    auto* e = world.entity( g );
                    std::string curName = ( e && e->nameHash() != gie::InvalidStringHash ) ? std::string( gie::stringRegister().get( e->nameHash() ) ) : std::string{};
                    std::strncpy( s_NameDialogBuf, curName.c_str(), sizeof( s_NameDialogBuf ) - 1 );
                    s_NameDialogBuf[ sizeof( s_NameDialogBuf ) - 1 ] = '\0';
                    s_NameDialogFocus = true;
                }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        // Name dialog row (shared for Add/Rename)
        if( s_NameDialogMode != NameDialogMode::None )
        {
            ImGui::Separator();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted( "Name:" );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 240.0f );
            if( s_NameDialogFocus ) { ImGui::SetKeyboardFocusHere(); s_NameDialogFocus = false; }
            bool apply = ImGui::InputText( "##entity_name_dialog", s_NameDialogBuf, IM_ARRAYSIZE( s_NameDialogBuf ), ImGuiInputTextFlags_EnterReturnsTrue );
            ImGui::SameLine();
            if( ImGui::Button( "Apply" ) ) apply = true;
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) )
            {
                CancelNameDialog();
            }
            if( apply )
            {
                if( s_NameDialogMode == NameDialogMode::Rename )
                {
                    if( g_selectedEntityGuids.size() == 1 )
                    {
                        gie::Guid g = *g_selectedEntityGuids.begin();
                        auto* e = world.entity( g );
                        if( e )
                        {
                            e->setName( s_NameDialogBuf );
                        }
                    }
                }
                else if( s_NameDialogMode == NameDialogMode::Add )
                {
                    if( auto* e = world.createEntity( s_NameDialogBuf[0] ? s_NameDialogBuf : "entity" ) )
                    {
                        g_selectedEntityGuids.clear();
                        g_selectedEntityGuids.insert( e->guid() );
                        g_WaypointEditSelectedGuid = e->guid(); // sync
                    }
                }
                CancelNameDialog();
            }
            ImGui::Separator();
        }

        // Build sorted list of entities
        std::vector<std::pair<gie::Guid, const gie::Entity*>> entries;
        entries.reserve( world.context().entities().size() );
        for( const auto& kv : world.context().entities() )
        {
            entries.emplace_back( kv.first, &kv.second );
        }
        auto nameOf = []( const gie::Entity* e ) -> std::string
        {
            if( !e ) return std::string{};
            auto nh = e->nameHash();
            if( nh != gie::InvalidStringHash ) return std::string( gie::stringRegister().get( nh ) );
            return std::string{};
        };
        std::sort( entries.begin(), entries.end(), [&]( const auto& a, const auto& b )
        {
            std::string an = nameOf( a.second );
            std::string bn = nameOf( b.second );
            // Empty names go last
            if( an.empty() && bn.empty() ) return a.first < b.first;
            if( an.empty() ) return false;
            if( bn.empty() ) return true;
            if( an == bn ) return a.first < b.first;
            return an < bn;
        } );

        // List header
        if( ImGui::BeginTable( "##entity_outliner_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "Entity" );
            ImGui::TableSetupColumn( "Guid" );
            ImGui::TableHeadersRow();

            for( const auto& [ guid, ent ] : entries )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );

                if( guid == s_InlineRenameGuid )
                {
                    // Inline rename input
                    ImGui::SetNextItemWidth( -1.0f );
                    if( s_InlineRenameFocus ) { ImGui::SetKeyboardFocusHere(); s_InlineRenameFocus = false; }
                    bool enterPressed = ImGui::InputText( "##inline_rename", s_InlineRenameBuf, IM_ARRAYSIZE( s_InlineRenameBuf ), ImGuiInputTextFlags_EnterReturnsTrue );
                    if( enterPressed )
                    {
                        // Apply rename
                        auto* e = world.entity( guid );
                        if( e ) e->setName( s_InlineRenameBuf );
                        CancelInlineRename();
                    }
                    else if( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
                    {
                        // Cancel rename
                        CancelInlineRename();
                    }
                    else if( !ImGui::IsItemActive() && ImGui::IsMouseClicked( 0 ) )
                    {
                        // Clicked outside input, cancel
                        CancelInlineRename();
                    }
                }
                else
                {
                    // Normal selectable row
                    std::string displayName = nameOf( ent );
                    if( displayName.empty() ) displayName = "<unnamed>";
                    bool selected = false;
                    if( g_selectedEntityGuids.size() == 1 && *g_selectedEntityGuids.begin() == guid ) selected = true;
                    std::string selLabel = displayName + "##" + std::to_string( static_cast<unsigned long long>( guid ) );
                    if( ImGui::Selectable( selLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns ) )
                    {
                        g_selectedEntityGuids.clear();
                        g_selectedEntityGuids.insert( guid );
                        g_WaypointEditSelectedGuid = guid; // sync to other tools
                    }
                    // Check for double-click to start inline rename
                    if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( 0 ) )
                    {
                        s_InlineRenameGuid = guid;
                        std::string curName = nameOf( ent );
                        std::strncpy( s_InlineRenameBuf, curName.c_str(), sizeof( s_InlineRenameBuf ) - 1 );
                        s_InlineRenameBuf[ sizeof( s_InlineRenameBuf ) - 1 ] = '\0';
                        s_InlineRenameFocus = true;
                        CancelNameDialog(); // cancel any existing dialog
                    }
                }

                ImGui::TableSetColumnIndex( 1 );
                ImGui::Text( "%llu", static_cast<unsigned long long>( guid ) );
            }
            ImGui::EndTable();
        }

        // Hint
        ImGui::Separator();
        ImGui::TextUnformatted( "Tip: Click a row to select. Double-click to rename inline. 'Add' opens a name dialog." );
    }
    ImGui::End();
}

void ResetEntityOutlinerState()
{
    CancelNameDialog();
    CancelInlineRename();
}

bool CancelEntityOutlinerOngoingOperation()
{
    if( s_NameDialogMode != NameDialogMode::None )
    {
        CancelNameDialog();
        return true;
    }
    if( s_InlineRenameGuid != gie::NullGuid )
    {
        CancelInlineRename();
        return true;
    }
    return false;
}

void StartOutlinerInlineRename(gie::Guid guid)
{
    s_InlineRenameGuid = guid;
    std::string curName;
    if( guid != gie::NullGuid && g_WorldPtr )
    {
        if( auto* e = g_WorldPtr->entity( guid ) )
        {
            auto nh = e->nameHash();
            if( nh != gie::InvalidStringHash )
                curName = std::string( gie::stringRegister().get( nh ) );
        }
    }
    std::strncpy( s_InlineRenameBuf, curName.c_str(), sizeof( s_InlineRenameBuf ) - 1 );
    s_InlineRenameBuf[ sizeof( s_InlineRenameBuf ) - 1 ] = '\0';
    s_InlineRenameFocus = true;
    CancelNameDialog();
}

void CancelOutlinerInlineRename()
{
    CancelInlineRename();
}

bool GetOutlinerInlineRenameState(gie::Guid* outGuid, const char** outBuf)
{
    if( s_InlineRenameGuid == gie::NullGuid )
        return false;
    if( outGuid )
        *outGuid = s_InlineRenameGuid;
    if( outBuf )
        *outBuf = s_InlineRenameBuf;
    return true;
}
