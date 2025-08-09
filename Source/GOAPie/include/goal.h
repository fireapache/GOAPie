#pragma once

#include <vector>

#include "property.h"
#include "common.h"

namespace gie
{
	class World;
	class Agent;
	class Simulation; // Forward declaration

	class Heuristic
	{
    public:
		Heuristic() = default;
		~Heuristic() = default;

		struct Result
		{
			float value{ InvalidHeuristic };
		};

		virtual Result calculate( const World& world, const Agent& agent ) { return Result{ }; };
	};

	// Class representing a specific goal to be achieved by an agent.
	class Goal
	{
	protected:
		World* _world{ nullptr };
		std::vector< Heuristic > _heuristics;

	public:
		Goal() = delete;
		Goal( World& world ) : _world( &world ) { }
		~Goal() = default;

		std::vector< Definition > targets;

		// @Return True if given simulation has reached goal targets, False otherwise.
		bool reached( const Simulation& simulation ) const;

		World* world() const { return _world; }
		const auto& heuristis() const { return _heuristics; }
	};
}