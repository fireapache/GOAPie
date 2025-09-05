#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "definitions.h"
#include "common.h"
#include "property.h"

namespace gie
{
    class World;     // fwd
    class Entity;    // fwd
    class Blackboard;// fwd

    // Archetype: template describing an entity's default properties and tags.
    // It does NOT live in any Blackboard and does not affect Context or Planning
    // until instantiated into a real Entity.
    class Archetype
    {
        Guid _guid{ NullGuid };
        StringHash _nameHash{ InvalidStringHash };
        TagSet _tags; // default tags applied on instantiation
        std::unordered_map<StringHash, Property::Variant> _properties; // default properties applied on instantiation

    public:
        Archetype() = default;
        explicit Archetype( std::string_view name )
            : _guid( randGuid() )
            , _nameHash( stringHasher( name ) )
        {}

        Guid guid() const { return _guid; }
        StringHash nameHash() const { return _nameHash; }
        void setName( std::string_view name ) { _nameHash = stringHasher( name ); }

        const TagSet& tags() const { return _tags; }
        TagSet& tags() { return _tags; }

        const auto& properties() const { return _properties; }
        auto& properties() { return _properties; }

        // Convenience helpers to define template
        void addTag( std::string_view tagName ) { _tags.insert( stringHasher( tagName ) ); }
        void addTag( Tag tag ) { _tags.insert( tag ); }

        void addProperty( std::string_view name, const Property::Variant& defaultValue )
        {
            _properties[ stringHasher( name ) ] = defaultValue;
        }
        void addProperty( StringHash nameHash, const Property::Variant& defaultValue )
        {
            _properties[ nameHash ] = defaultValue;
        }

        // Instantiate a concrete entity in the given world using this archetype.
        // Returns the created Entity*, or nullptr on failure.
        Entity* instantiate( World& world, std::string_view nameOverride = "", Blackboard* context = nullptr ) const;
    };
}
