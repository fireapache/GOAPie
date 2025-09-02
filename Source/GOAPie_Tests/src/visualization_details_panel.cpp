#include "visualization.h"

#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <cstring>

// Details Panel: shows properties for the currently selected entity and allows adding new properties
// The add flow requires choosing the type first and then entering a name (similar to Entity Outliner add flow)

namespace {
    enum class AddPropMode : uint8_t { None = 0, ChoosingType, EnteringName };
    static AddPropMode s_AddPropMode = AddPropMode::None;
    static gie::Property::Type s_AddPropChosenType = gie::Property::Unknow;
    static char s_AddPropNameBuf[256] = { 0 };
    static bool s_AddPropNameFocus = false;

    inline const char* typeToLabel( gie::Property::Type t )
    {
        using T = gie::Property::Type;
        switch( t )
        {
        case T::Boolean:      return "Boolean";
        case T::BooleanArray: return "BooleanArray";
        case T::Float:        return "Float";
        case T::FloatArray:   return "FloatArray";
        case T::Integer:      return "Integer";
        case T::IntegerArray: return "IntegerArray";
        case T::GUID:         return "GUID / StringHash";
        case T::GUIDArray:    return "GUIDArray / StringHashArray";
        case T::Vec3:         return "Vec3";
        case T::Vec3Array:    return "Vec3Array";
        default:              return "Unknown";
        }
    }

    inline void resetAddPropDialog()
    {
        s_AddPropMode = AddPropMode::None;
        s_AddPropChosenType = gie::Property::Unknow;
        s_AddPropNameBuf[0] = '\0';
        s_AddPropNameFocus = false;
    }
}

static void drawEntityHeader( const gie::Entity* e, gie::Guid guid )
{
    if( !e ) return;
    std::string name = e->nameHash() != gie::InvalidStringHash ? std::string( gie::stringRegister().get( e->nameHash() ) ) : std::string{};
    if( name.empty() ) name = std::string("<unnamed>");

    ImGui::Text( "%s", name.c_str() );
    ImGui::SameLine();
    ImGui::TextDisabled( "(%llu)", static_cast< unsigned long long >( guid ) );
}

void drawDetailsPanelWindow( gie::World& world )
{
    if( !g_ShowDetailsPanelWindow ) return;

    if( ImGui::Begin( "Details", &g_ShowDetailsPanelWindow ) )
    {
        const bool hasSelection = ( g_SelectedEntityGuid != gie::NullGuid );
        gie::Entity* entity = hasSelection ? world.entity( g_SelectedEntityGuid ) : nullptr;
        if( !entity )
        {
            ImGui::TextDisabled( "No entity selected." );
            resetAddPropDialog();
            ImGui::End();
            return;
        }

        drawEntityHeader( entity, g_SelectedEntityGuid );
        ImGui::Separator();

        // List existing properties
        if( ImGui::BeginTable( "##entity_props_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "Property" );
            ImGui::TableSetupColumn( "Value" );
            ImGui::TableHeadersRow();

            const auto& stringregister = gie::stringRegister();
            for( const auto& [ propNameHash, propGuid ] : entity->properties() )
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                auto propName = stringregister.get( propNameHash );
                ImGui::Text( "%s", propName.data() );

                ImGui::TableSetColumnIndex( 1 );
                if( auto* prop = world.property( propGuid ) )
                {
                    ImGui::Text( "%s", prop->toString().c_str() );
                }
                else
                {
                    ImGui::TextDisabled( "<missing>" );
                }
            }

            ImGui::EndTable();
        }

        ImGui::Separator();

        // Add property controls
        if( s_AddPropMode == AddPropMode::None )
        {
            if( ImGui::Button( "+ Add Property" ) )
            {
                s_AddPropMode = AddPropMode::ChoosingType;
            }
        }
        else if( s_AddPropMode == AddPropMode::ChoosingType )
        {
            ImGui::TextUnformatted( "Choose type:" );
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) ) { resetAddPropDialog(); }

            // Type selection grid
            const gie::Property::Type types[] = {
                gie::Property::Boolean,
                gie::Property::BooleanArray,
                gie::Property::Float,
                gie::Property::FloatArray,
                gie::Property::Integer,
                gie::Property::IntegerArray,
                gie::Property::GUID,
                gie::Property::GUIDArray,
                gie::Property::Vec3,
                gie::Property::Vec3Array
            };

            for( size_t i = 0; i < std::size( types ); ++i )
            {
                if( i > 0 ) ImGui::SameLine();
                if( ImGui::Button( typeToLabel( types[i] ) ) )
                {
                    s_AddPropChosenType = types[i];
                    s_AddPropMode = AddPropMode::EnteringName;
                    s_AddPropNameBuf[0] = '\0';
                    s_AddPropNameFocus = true;
                }
            }
        }
        else if( s_AddPropMode == AddPropMode::EnteringName )
        {
            ImGui::AlignTextToFramePadding();
            ImGui::Text( "Type: %s", typeToLabel( s_AddPropChosenType ) );
            ImGui::SameLine();
            if( ImGui::Button( "Change" ) )
            {
                s_AddPropMode = AddPropMode::ChoosingType;
            }

            ImGui::Separator();
            ImGui::TextUnformatted( "Property Name:" );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 240.0f );
            if( s_AddPropNameFocus ) { ImGui::SetKeyboardFocusHere(); s_AddPropNameFocus = false; }
            bool apply = ImGui::InputText( "##prop_name_dialog", s_AddPropNameBuf, IM_ARRAYSIZE( s_AddPropNameBuf ), ImGuiInputTextFlags_EnterReturnsTrue );
            ImGui::SameLine();
            if( ImGui::Button( "Create" ) ) apply = true;
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) ) { resetAddPropDialog(); }

            if( apply )
            {
                const char* propName = s_AddPropNameBuf;
                if( propName && propName[0] )
                {
                    // Create property with chosen type and sensible default
                    using T = gie::Property::Type;
                    switch( s_AddPropChosenType )
                    {
                    case T::Boolean:      entity->createProperty( propName, false ); break;
                    case T::BooleanArray: entity->createProperty( propName, gie::Property::BooleanVector{} ); break;
                    case T::Float:        entity->createProperty( propName, 0.0f ); break;
                    case T::FloatArray:   entity->createProperty( propName, gie::Property::FloatVector{} ); break;
                    case T::Integer:      entity->createProperty( propName, static_cast<int32_t>(0) ); break;
                    case T::IntegerArray: entity->createProperty( propName, gie::Property::IntegerVector{} ); break;
                    case T::GUID:         entity->createProperty( propName, gie::NullGuid ); break;
                    case T::GUIDArray:    entity->createProperty( propName, gie::Property::GuidVector{} ); break;
                    case T::Vec3:         entity->createProperty( propName, glm::vec3{ 0, 0, 0 } ); break;
                    case T::Vec3Array:    entity->createProperty( propName, gie::Property::Vec3Vector{} ); break;
                    default: break;
                    }
                }
                resetAddPropDialog();
            }
        }
    }
    ImGui::End();
}
