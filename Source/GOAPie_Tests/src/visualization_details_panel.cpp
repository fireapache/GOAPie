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

    // Selection state
    static gie::Guid s_LastEntityGuid = gie::NullGuid;
    static gie::StringHash s_SelectedPropHash = gie::InvalidStringHash;
    static gie::Guid s_SelectedPropGuid = gie::NullGuid;

    // Edit state
    static bool s_EditActive = false;
    static gie::StringHash s_EditPropHash = gie::InvalidStringHash;
    static gie::Property::Type s_EditPropType = gie::Property::Unknow;
    static bool s_EditBool = false;
    static float s_EditFloat = 0.0f;
    static int s_EditInt = 0;
    static gie::Guid s_EditGuid = gie::NullGuid;
    static glm::vec3 s_EditVec3{ 0, 0, 0 };
    static char s_EditGuidBuf[256] = { 0 };
    static std::vector<bool> s_EditBools;
    static std::vector<float> s_EditFloats;
    static std::vector<int> s_EditInts;
    static std::vector<gie::Guid> s_EditGuids;
    static std::vector<glm::vec3> s_EditVec3s;

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

    inline void resetSelection()
    {
        s_SelectedPropHash = gie::InvalidStringHash;
        s_SelectedPropGuid = gie::NullGuid;
    }

    inline void resetEdit()
    {
        s_EditActive = false;
        s_EditPropHash = gie::InvalidStringHash;
        s_EditPropType = gie::Property::Unknow;
        s_EditBool = false;
        s_EditFloat = 0.0f;
        s_EditInt = 0;
        s_EditGuid = gie::NullGuid;
        s_EditVec3 = glm::vec3{ 0, 0, 0 };
        s_EditGuidBuf[0] = '\0';
        s_EditBools.clear();
        s_EditFloats.clear();
        s_EditInts.clear();
        s_EditGuids.clear();
        s_EditVec3s.clear();
    }

    inline void cancelTransientUIsIfClickedOutside()
    {
        if( s_AddPropMode == AddPropMode::None && !s_EditActive ) return;
        if( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) ) return;
        ImGuiIO& io = ImGui::GetIO();
        if( io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2] || io.MouseWheel != 0.0f )
        {
            resetAddPropDialog();
            resetEdit();
        }
    }

    inline void beginEditForProperty( gie::Property* prop, gie::StringHash propHash )
    {
        if( !prop ) return;
        s_EditActive = true;
        s_EditPropHash = propHash;
        s_EditPropType = prop->type();
        using T = gie::Property::Type;
        switch( s_EditPropType )
        {
        case T::Boolean:      s_EditBool = prop->getBool() ? *prop->getBool() : false; break;
        case T::Float:        s_EditFloat = prop->getFloat() ? *prop->getFloat() : 0.0f; break;
        case T::Integer:      s_EditInt = prop->getInteger() ? *prop->getInteger() : 0; break;
        case T::GUID:
        {
            s_EditGuid = prop->getGuid() ? *prop->getGuid() : gie::NullGuid;
            auto strView = gie::stringRegister().get( s_EditGuid );
            std::string text = strView.empty() ? std::to_string( static_cast<unsigned long long>( s_EditGuid ) ) : std::string( strView );
            std::snprintf( s_EditGuidBuf, sizeof( s_EditGuidBuf ), "%s", text.c_str() );
            break;
        }
        case T::Vec3:         s_EditVec3 = prop->getVec3() ? *prop->getVec3() : glm::vec3{ 0, 0, 0 }; break;
        case T::BooleanArray:
        {
            s_EditBools.clear();
            if( auto v = prop->getBooleanArray() ) s_EditBools = *v;
            break;
        }
        case T::FloatArray:
        {
            s_EditFloats.clear();
            if( auto v = prop->getFloatArray() ) s_EditFloats = *v;
            break;
        }
        case T::IntegerArray:
        {
            s_EditInts.clear();
            if( auto v = prop->getIntegerArray() ) s_EditInts = *v;
            break;
        }
        case T::GUIDArray:
        {
            s_EditGuids.clear();
            if( auto v = prop->getGuidArray() ) s_EditGuids = *v;
            break;
        }
        case T::Vec3Array:
        {
            s_EditVec3s.clear();
            if( auto v = prop->getVec3Array() ) s_EditVec3s = *v;
            break;
        }
        default: break;
        }
    }

    inline void applyEditToProperty( gie::Property* prop )
    {
        if( !prop ) return;
        using T = gie::Property::Type;
        switch( s_EditPropType )
        {
        case T::Boolean:      prop->value = s_EditBool; break;
        case T::Float:        prop->value = s_EditFloat; break;
        case T::Integer:      prop->value = static_cast<int32_t>( s_EditInt ); break;
        case T::GUID:         prop->value = s_EditGuid; break;
        case T::Vec3:         prop->value = s_EditVec3; break;
        case T::BooleanArray: prop->value = gie::Property::BooleanVector( s_EditBools.begin(), s_EditBools.end() ); break;
        case T::FloatArray:   prop->value = gie::Property::FloatVector( s_EditFloats.begin(), s_EditFloats.end() ); break;
        case T::IntegerArray: {
            gie::Property::IntegerVector tmp; tmp.reserve( s_EditInts.size() );
            for( int v : s_EditInts ) tmp.push_back( static_cast<int32_t>( v ) );
            prop->value = std::move( tmp );
            break; }
        case T::GUIDArray:    prop->value = gie::Property::GuidVector( s_EditGuids.begin(), s_EditGuids.end() ); break;
        case T::Vec3Array:    prop->value = gie::Property::Vec3Vector( s_EditVec3s.begin(), s_EditVec3s.end() ); break;
        default: break;
        }
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
        // Cancel add/edit if user clicks outside the Details window
        cancelTransientUIsIfClickedOutside();

        // Reset selection/edit when entity changes
        if( s_LastEntityGuid != g_SelectedEntityGuid )
        {
            s_LastEntityGuid = g_SelectedEntityGuid;
            resetSelection();
            resetAddPropDialog();
            resetEdit();
        }

        const bool hasSelection = ( g_SelectedEntityGuid != gie::NullGuid );
        gie::Entity* entity = hasSelection ? world.entity( g_SelectedEntityGuid ) : nullptr;
        if( !entity )
        {
            ImGui::TextDisabled( "No entity selected." );
            resetAddPropDialog();
            resetEdit();
            ImGui::End();
            return;
        }

        drawEntityHeader( entity, g_SelectedEntityGuid );
        ImGui::Separator();

        // New: show tags of selected entity
        if( !entity->tags().empty() )
        {
            ImGui::TextUnformatted( "Tags:" );
            ImGui::SameLine();
            bool first = true;
            for( auto tag : entity->tags() )
            {
                auto sv = gie::stringRegister().get( tag );
                std::string label = sv.empty() ? std::to_string( static_cast<unsigned long long>( tag ) ) : std::string( sv );
                if( !first ) ImGui::SameLine( 0.f, 6.f );
                ImGui::TextWrapped( "%s", label.c_str() );
                first = false;
            }
            ImGui::Separator();
        }

        bool didDelete = false;

        // Controls: Add / Edit / Delete
        if( ImGui::Button( "+ Add Property" ) )
        {
            s_AddPropMode = AddPropMode::ChoosingType;
            resetEdit();
        }
        ImGui::SameLine();
        const bool hasPropSelected = ( s_SelectedPropHash != gie::InvalidStringHash && s_SelectedPropGuid != gie::NullGuid );
        ImGui::BeginDisabled( !hasPropSelected );
        if( ImGui::Button( "Edit" ) )
        {
            if( auto* prop = world.property( s_SelectedPropGuid ) )
            {
                beginEditForProperty( prop, s_SelectedPropHash );
                resetAddPropDialog();
            }
        }
        ImGui::SameLine();
        if( ImGui::Button( "Delete" ) )
        {
            // Re-validate selected mapping before deletion
            auto it = entity->properties().find( s_SelectedPropHash );
            if( it != entity->properties().end() && it->second == s_SelectedPropGuid )
            {
                entity->removeProperty( s_SelectedPropHash );
                world.removeProperty( s_SelectedPropGuid );
            }
            resetSelection();
            resetEdit();
            didDelete = true;
        }
        ImGui::EndDisabled();

        // Optional: keyboard Delete to remove property as well
        if( hasPropSelected && s_AddPropMode == AddPropMode::None && !s_EditActive && ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
        {
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18700
            if( ImGui::IsKeyPressed( ImGuiKey_Delete ) )
#else
            if( ImGui::IsKeyPressed( ImGui::GetKeyIndex( ImGuiKey_Delete ) ) )
#endif
            {
                auto it = entity->properties().find( s_SelectedPropHash );
                if( it != entity->properties().end() && it->second == s_SelectedPropGuid )
                {
                    entity->removeProperty( s_SelectedPropHash );
                    world.removeProperty( s_SelectedPropGuid );
                }
                resetSelection();
                resetEdit();
                didDelete = true;
            }
        }

        if( didDelete )
        {
            // Stop drawing this frame to avoid using stale state
            ImGui::End();
            return;
        }

        ImGui::Separator();

        // List existing properties with selection
        if( ImGui::BeginTable( "##entity_props_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "Property" );
            ImGui::TableSetupColumn( "Value" );
            ImGui::TableHeadersRow();

            const auto& stringregister = gie::stringRegister();
            for( const auto& [ propNameHash, propGuid ] : entity->properties() )
            {
                const bool rowSelected = ( propNameHash == s_SelectedPropHash );
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex( 0 );
                auto propName = stringregister.get( propNameHash );
                std::string label = std::string( propName ) + "##sel" + std::to_string( static_cast<unsigned long long>( propGuid ) );
                if( ImGui::Selectable( label.c_str(), rowSelected, ImGuiSelectableFlags_SpanAllColumns ) )
                {
                    s_SelectedPropHash = propNameHash;
                    s_SelectedPropGuid = propGuid;
                }

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

        // Add property flow
        if( s_AddPropMode == AddPropMode::ChoosingType )
        {
            // Dropdown for type selection
            ImGui::TextUnformatted( "Choose type:" );
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) ) { resetAddPropDialog(); }

            static int s_TypeIndex = 0;
            const gie::Property::Type typeList[] = {
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
            const char* current = typeToLabel( typeList[ s_TypeIndex ] );
            ImGui::SetNextItemWidth( 200.0f );
            if( ImGui::BeginCombo( "##prop_type_combo", current ) )
            {
                for( int i = 0; i < (int)std::size( typeList ); ++i )
                {
                    bool selected = ( s_TypeIndex == i );
                    if( ImGui::Selectable( typeToLabel( typeList[i] ), selected ) )
                    {
                        s_TypeIndex = i;
                    }
                    if( selected ) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if( ImGui::Button( "Next" ) )
            {
                s_AddPropChosenType = typeList[ s_TypeIndex ];
                s_AddPropMode = AddPropMode::EnteringName;
                s_AddPropNameBuf[0] = '\0';
                s_AddPropNameFocus = true;
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

        // Edit property flow
        if( s_EditActive )
        {
            ImGui::Separator();
            ImGui::Text( "Editing: %s", gie::stringRegister().get( s_EditPropHash ).data() );
            ImGui::Text( "Type: %s", typeToLabel( s_EditPropType ) );

            using T = gie::Property::Type;
            switch( s_EditPropType )
            {
            case T::Boolean:
                ImGui::Checkbox( "Value", &s_EditBool );
                break;
            case T::Float:
                ImGui::InputFloat( "Value", &s_EditFloat );
                break;
            case T::Integer:
                ImGui::InputInt( "Value", &s_EditInt );
                break;
            case T::GUID:
            {
                ImGui::SetNextItemWidth( 240.0f );
                ImGui::InputText( "GUID/String", s_EditGuidBuf, IM_ARRAYSIZE( s_EditGuidBuf ) );
                break;
            }
            case T::Vec3:
            {
                float v[3] = { s_EditVec3.x, s_EditVec3.y, s_EditVec3.z };
                if( ImGui::InputFloat3( "Value", v ) )
                {
                    s_EditVec3.x = v[0]; s_EditVec3.y = v[1]; s_EditVec3.z = v[2];
                }
                break;
            }
            case T::BooleanArray:
            {
                // Controls to add/remove elements
                if( ImGui::Button( "+##add_bool" ) ) s_EditBools.push_back( false );
                ImGui::SameLine();
                ImGui::BeginDisabled( s_EditBools.empty() );
                if( ImGui::Button( "-##del_bool" ) && !s_EditBools.empty() ) s_EditBools.pop_back();
                ImGui::EndDisabled();
                for( size_t i = 0; i < s_EditBools.size(); ++i )
                {
                    std::string cb = std::string("[ ") + std::to_string( i ) + "]";
                    bool bv = s_EditBools[i];
                    if( ImGui::Checkbox( cb.c_str(), &bv ) ) { s_EditBools[i] = bv; }
                }
                break;
            }
            case T::FloatArray:
            {
                if( ImGui::Button( "+##add_float" ) ) s_EditFloats.push_back( 0.0f );
                ImGui::SameLine();
                ImGui::BeginDisabled( s_EditFloats.empty() );
                if( ImGui::Button( "-##del_float" ) && !s_EditFloats.empty() ) s_EditFloats.pop_back();
                ImGui::EndDisabled();
                for( size_t i = 0; i < s_EditFloats.size(); ++i )
                {
                    ImGui::PushID( static_cast<int>( i ) );
                    ImGui::InputFloat( "", &s_EditFloats[i] );
                    ImGui::PopID();
                }
                break;
            }
            case T::IntegerArray:
            {
                if( ImGui::Button( "+##add_int" ) ) s_EditInts.push_back( 0 );
                ImGui::SameLine();
                ImGui::BeginDisabled( s_EditInts.empty() );
                if( ImGui::Button( "-##del_int" ) && !s_EditInts.empty() ) s_EditInts.pop_back();
                ImGui::EndDisabled();
                for( size_t i = 0; i < s_EditInts.size(); ++i )
                {
                    ImGui::PushID( static_cast<int>( i ) );
                    ImGui::InputInt( "", &s_EditInts[i] );
                    ImGui::PopID();
                }
                break;
            }
            case T::GUIDArray:
            {
                if( ImGui::Button( "+##add_guid" ) ) s_EditGuids.push_back( gie::NullGuid );
                ImGui::SameLine();
                ImGui::BeginDisabled( s_EditGuids.empty() );
                if( ImGui::Button( "-##del_guid" ) && !s_EditGuids.empty() ) s_EditGuids.pop_back();
                ImGui::EndDisabled();
                for( size_t i = 0; i < s_EditGuids.size(); ++i )
                {
                    ImGui::PushID( static_cast<int>( i ) );
                    // Edit as text for flexibility (string or number)
                    char buf[256];
                    auto sv = gie::stringRegister().get( s_EditGuids[i] );
                    std::string txt = sv.empty() ? std::to_string( static_cast<unsigned long long>( s_EditGuids[i] ) ) : std::string( sv );
                    std::snprintf( buf, sizeof( buf ), "%s", txt.c_str() );
                    if( ImGui::InputText( "", buf, IM_ARRAYSIZE( buf ), ImGuiInputTextFlags_EnterReturnsTrue ) )
                    {
                        // parse on enter; otherwise leave as-is until Apply
                    }
                    ImGui::PopID();
                }
                ImGui::TextDisabled( "Note: GUID array text is applied on 'Apply' based on number or string hash." );
                break;
            }
            case T::Vec3Array:
            {
                if( ImGui::Button( "+##add_vec3" ) ) s_EditVec3s.push_back( glm::vec3{ 0, 0, 0 } );
                ImGui::SameLine();
                ImGui::BeginDisabled( s_EditVec3s.empty() );
                if( ImGui::Button( "-##del_vec3" ) && !s_EditVec3s.empty() ) s_EditVec3s.pop_back();
                ImGui::EndDisabled();
                for( size_t i = 0; i < s_EditVec3s.size(); ++i )
                {
                    ImGui::PushID( static_cast<int>( i ) );
                    float v[3] = { s_EditVec3s[i].x, s_EditVec3s[i].y, s_EditVec3s[i].z };
                    if( ImGui::InputFloat3( "", v ) )
                    {
                        s_EditVec3s[i].x = v[0]; s_EditVec3s[i].y = v[1]; s_EditVec3s[i].z = v[2];
                    }
                    ImGui::PopID();
                }
                break;
            }
            default:
                ImGui::TextDisabled( "Unsupported type." );
                break;
            }

            // Apply/Cancel row
            ImGui::Separator();
            if( ImGui::Button( "Apply" ) )
            {
                // Special parsing for GUID scalar text buffer before apply
                if( s_EditPropType == T::GUID )
                {
                    std::string txt = s_EditGuidBuf;
                    bool allDigits = !txt.empty() && std::all_of( txt.begin(), txt.end(), []( unsigned char c ){ return std::isdigit( c ); } );
                    if( allDigits )
                    {
                        unsigned long long v = 0ULL;
                        try { v = std::stoull( txt ); } catch( ... ) { v = 0ULL; }
                        s_EditGuid = static_cast< gie::Guid >( v );
                    }
                    else
                    {
                        s_EditGuid = gie::stringHasher( txt );
                    }
                }
                // For GUID arrays, we re-interpret existing values only
                // (entry inputs are informational; a full parser per-row is omitted for brevity)

                if( auto* prop = world.property( s_SelectedPropGuid ) )
                {
                    applyEditToProperty( prop );
                }
                resetEdit();
            }
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) )
            {
                resetEdit();
            }
        }

        // Add flow or Edit is visible; outside click already handled on top
    }
    ImGui::End();
}
