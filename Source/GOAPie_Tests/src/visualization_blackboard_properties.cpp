#include "visualization.h"

#include <algorithm>
#include <string>
#include <vector>
#include <set>

static void drawEntityNameText( const gie::Entity& entity, const gie::Guid entityGuid, const bool padding )
{
    auto nameHash = entity.nameHash();
    const bool isValidName = nameHash != gie::InvalidStringHash;
    const std::string_view entityName = isValidName ? gie::stringRegister().get( nameHash ) : "Unnamed Entity";

    if( padding )
    {
        ImGui::SetCursorPosX( ( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( entityName.data() ).x ) * 0.225f );
    }

    if( isValidName )
    {
        ImGui::Text( "%s", entityName.data() );
    }
    else
    {
        ImGui::Text( "%llu", static_cast< unsigned long long >( entityGuid ) );
    }
}

void drawBlackboardPropertiesWindow( const gie::Simulation* simulation )
{
    if( !g_ShowBlackboardPropertiesWindow ) return;
    if( !simulation ) return;

    const gie::Blackboard* currentContext = &simulation->context();
    std::set< gie::Guid > entityGuids;
    const auto& stringregister = gie::stringRegister();

    while( currentContext )
    {
        for( const auto& [ entityGuid, entity ] : currentContext->entities() )
        {
            entityGuids.insert( entityGuid );
        }

        currentContext = currentContext->parent();
    }

    static bool multiLevel = false;

    if( ImGui::Begin( "Blackboard Properties", &g_ShowBlackboardPropertiesWindow ) )
    {
        if( g_IsLoading ) { DrawWindowLoadingOverlay(); ImGui::End(); return; }
        currentContext = &simulation->context();
        ImGui::Checkbox( "Multi-Level", &multiLevel );

        if( multiLevel )
        {
            int level = 0;

            while( currentContext )
            {
                ImGui::Text( "Blackboard Level: %d", level );
                ImGui::Separator();

                for( const auto& [ entityGuid, entity ] : currentContext->entities() )
                {
                    drawEntityNameText( entity, entityGuid, true );
                    ImGui::Separator();

                    ImGui::Columns( 2, nullptr, false );
                    for( const auto& [ propertyNameHash, propertyGuid ] : entity.properties() )
                    {
                        auto propertyName = stringregister.get( propertyNameHash );
                        auto property = currentContext->property( propertyGuid );
                        ImGui::Text( "%s", propertyName.data() );
                        ImGui::NextColumn();
                        ImGui::Text( "%s", property->toString().c_str() );
                        ImGui::NextColumn();
                    }
                    ImGui::Columns( 1 );
                    ImGui::Separator();
                }

                currentContext = currentContext->parent();
                level++;
                ImGui::Separator();
            }
        }
        else
        {
            for( auto entityGuid : entityGuids )
            {
                auto entity = currentContext->entity( entityGuid );

                drawEntityNameText( *entity, entityGuid, true );
                ImGui::Separator();

                ImGui::Columns( 2, nullptr, false );
                for( const auto& [ propertyNameHash, propertyGuid ] : entity->properties() )
                {
                    auto propertyName = stringregister.get( propertyNameHash );
                    auto property = currentContext->property( propertyGuid );
                    ImGui::Text( "%s", propertyName.data() );
                    ImGui::NextColumn();
                    ImGui::Text( "%s", property->toString().c_str() );
                    ImGui::NextColumn();
                }
                ImGui::Columns( 1 );
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
}
