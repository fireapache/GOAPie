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

    inline void CancelNameDialog()
    {
        s_NameDialogMode = NameDialogMode::None;
        s_NameDialogBuf[0] = '\0';
        s_NameDialogFocus = false;
    }
}

void drawEntityOutlinerWindow( gie::World& world )
{
    if( !g_ShowEntityOutlinerWindow ) return;

    if( ImGui::Begin( "Entity Outliner", &g_ShowEntityOutlinerWindow ) )
    {
        // If name dialog is open and user interacts/focuses elsewhere, cancel it
        if( s_NameDialogMode != NameDialogMode::None )
        {
            if( !ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
            {
                ImGuiIO& io = ImGui::GetIO();
                if( io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2] || io.MouseWheel != 0.0f )
                {
                    CancelNameDialog();
                }
            }
        }

        // Sync selection from other tools -> outliner
        if( g_WaypointEditSelectedGuid != gie::NullGuid && g_SelectedEntityGuid != g_WaypointEditSelectedGuid )
        {
            g_SelectedEntityGuid = g_WaypointEditSelectedGuid;
        }

        // Controls
        bool hasSelection = ( g_SelectedEntityGuid != gie::NullGuid && world.entity( g_SelectedEntityGuid ) != nullptr );

        ImGui::BeginDisabled( s_NameDialogMode != NameDialogMode::None );
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
                world.removeEntity( g_SelectedEntityGuid );
                if( g_WaypointEditSelectedGuid == g_SelectedEntityGuid )
                {
                    g_WaypointEditSelectedGuid = gie::NullGuid;
                }
                g_SelectedEntityGuid = gie::NullGuid;
            }
        }
        ImGui::SameLine();
        if( ImGui::Button( "Rename" ) )
        {
            if( hasSelection )
            {
                s_NameDialogMode = NameDialogMode::Rename;
                auto* e = world.entity( g_SelectedEntityGuid );
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
                    auto* e = world.entity( g_SelectedEntityGuid );
                    if( e )
                    {
                        e->setName( s_NameDialogBuf );
                    }
                }
                else if( s_NameDialogMode == NameDialogMode::Add )
                {
                    if( auto* e = world.createEntity( s_NameDialogBuf[0] ? s_NameDialogBuf : "entity" ) )
                    {
                        g_SelectedEntityGuid = e->guid();
                        g_WaypointEditSelectedGuid = g_SelectedEntityGuid; // sync
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
                std::string displayName = nameOf( ent );
                if( displayName.empty() ) displayName = "<unnamed>";
                bool selected = ( g_SelectedEntityGuid == guid );
                std::string selLabel = displayName + "##" + std::to_string( static_cast<unsigned long long>( guid ) );
                if( ImGui::Selectable( selLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns ) )
                {
                    g_SelectedEntityGuid = guid;
                    g_WaypointEditSelectedGuid = guid; // sync to other tools
                }

                ImGui::TableSetColumnIndex( 1 );
                ImGui::Text( "%llu", static_cast<unsigned long long>( guid ) );
            }
            ImGui::EndTable();
        }

        // Hint
        ImGui::Separator();
        ImGui::TextUnformatted( "Tip: Click a row to select. 'Add' opens a name dialog." );
    }
    ImGui::End();
}

void ResetEntityOutlinerState()
{
    CancelNameDialog();
}
