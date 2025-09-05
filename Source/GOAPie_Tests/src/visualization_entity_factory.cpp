#include "visualization.h"

#include <string>
#include <vector>

// Helper to get archetype display name
static std::string ArchetypeDisplayName( const gie::Archetype& a )
{
    auto nh = a.nameHash();
    if( nh != gie::InvalidStringHash )
    {
        std::string s( gie::stringRegister().get( nh ) );
        if( !s.empty() ) return s;
    }
    // fallback to last 4 of guid
    unsigned long long id = static_cast<unsigned long long>( a.guid() );
    unsigned int last4 = static_cast<unsigned int>( id % 10000ULL );
    char buf[32];
    snprintf( buf, sizeof(buf), "A%04u", last4 );
    return std::string( buf );
}

bool CancelEntityFactory()
{
    if( g_SelectedArchetypeGuid != gie::NullGuid )
    {
        g_SelectedArchetypeGuid = gie::NullGuid;
        return true;
    }
    return false;
}

void drawEntityFactoryWindow( gie::World& world )
{
    if( !g_ShowEntityFactoryWindow ) return;

    if( ImGui::Begin( "Entity Factory", &g_ShowEntityFactoryWindow ) )
    {
        if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }

        // Track hover state to allow click-outside to clear selection
        g_EntityFactoryWindowHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_ChildWindows );

        // Cancel placement with Escape or Right Mouse Button when this window is hovered
        if( g_SelectedArchetypeGuid != gie::NullGuid && g_EntityFactoryWindowHovered )
        {
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18700
            bool esc = ImGui::IsKeyPressed( ImGuiKey_Escape );
#else
            bool esc = ImGui::IsKeyPressed( ImGui::GetKeyIndex( ImGuiKey_Escape ) );
#endif
            if( esc || ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
            {
                CancelEntityFactory();
            }
        }

        // Instruction
        ImGui::TextDisabled( "Select an archetype, then click in World View to place entities." );
        ImGui::Separator();

        // Draw archetype buttons flowing on the same line wrapping to next lines
        ImGuiStyle& style = ImGui::GetStyle();
        float spacing = style.ItemSpacing.x;
        float availX = ImGui::GetContentRegionAvail().x;
        float xUsed = 0.0f;
        bool firstOnLine = true;

        auto& arches = world.archetypes();
        for( const auto& [ id, arch ] : arches )
        {
            std::string label = ArchetypeDisplayName( arch );
            ImVec2 textSize = ImGui::CalcTextSize( label.c_str() );
            float btnW = textSize.x + style.FramePadding.x * 2.0f;
            float btnH = textSize.y + style.FramePadding.y * 2.0f;

            if( !firstOnLine && xUsed + spacing + btnW > availX )
            {
                xUsed = 0.0f;
                firstOnLine = true;
            }

            if( !firstOnLine ) ImGui::SameLine();

            bool selected = ( g_SelectedArchetypeGuid == id );
            if( selected )
            {
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.30f, 0.50f, 0.80f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.35f, 0.55f, 0.85f, 1.0f ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.25f, 0.45f, 0.75f, 1.0f ) );
            }
            bool pressed = ImGui::Button( label.c_str(), ImVec2( btnW, btnH ) );
            if( selected ) ImGui::PopStyleColor( 3 );

            if( pressed )
            {
                // Select this archetype and unselect others by simply storing its guid
                g_SelectedArchetypeGuid = id;
            }

            xUsed += ( firstOnLine ? 0.0f : spacing ) + btnW;
            firstOnLine = false;
        }

        // No archetypes message
        if( arches.empty() )
        {
            ImGui::TextDisabled( "No archetypes defined in this world." );
        }
    }
    ImGui::End();
}
