#pragma once

#include "archetype.h"
#include "world.h"
#include "entity.h"
#include "blackboard.h"

namespace gie
{
    inline Entity* Archetype::instantiate( World& world, std::string_view nameOverride, Blackboard* context ) const
    {
        // Create entity similar to how users do it
        std::string_view finalName = nameOverride.empty() && _nameHash != InvalidStringHash
            ? stringRegister().get( _nameHash )
            : nameOverride;

        Entity* e = world.createEntity( finalName, context );
        if( !e ) return nullptr;

        // Apply default properties
        for( const auto& [ nameHash, defVal ] : _properties )
        {
            if( auto* ppt = e->createProperty( nameHash ) )
            {
                ppt->value = defVal;
            }
        }

        // Apply default tags into world context tag register
        if( !_tags.empty() )
        {
            std::vector<Tag> tagVec;
            tagVec.reserve( _tags.size() );
            for( Tag t : _tags ) tagVec.push_back( t );
            world.context().entityTagRegister().tag( e, tagVec );
        }

        return e;
    }
}
