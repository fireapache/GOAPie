#pragma once

#include "blackboard.h"
#include "entity.h"

namespace gie
{
	// Forward declarations
	class World;

	// Represents a NPC in world context.
	class Agent : public Entity
	{
		Blackboard _opinions;

	public:
		Agent() = delete;
		Agent( World* world, std::string_view name = "" );
		Agent( const Agent& agent ) = default;
		Agent( Agent&& agent ) = default;
		~Agent() = default;

		Blackboard& opinions() { return _opinions; }
		const Blackboard& opinions() const { return _opinions; }
	};

	// Represents a NPC in simulation context. World context agent is referenced for opinions.
	class SimAgent
	{
		const Agent* _agent{ nullptr };

	public:
		SimAgent() = delete;
		SimAgent( const Agent* agent ) : _agent( agent ) { };

		const Agent* worldContextAgent() const { return _agent; }
		Guid guid() const { return _agent ? _agent->guid() : NullGuid; }
	};

	inline Agent* Blackboard::agent( const Guid guid )
	{
		Entity* dataEntity = entity( guid );
		return static_cast< Agent* >( dataEntity );
	}

	inline const Agent* Blackboard::agent( const Guid guid ) const
	{
		const Entity* dataEntity = entity( guid );
		return static_cast< const Agent* >( dataEntity );
	}

	inline Agent* Blackboard::createAgent( std::string_view name )
	{
		Agent agent{ _world, name };
		auto result = _entities.emplace( agent.guid(), std::move( agent ) );
		if( result.second )
		{
			return static_cast< Agent* >( &result.first->second );
		}
		return nullptr;
	}
}