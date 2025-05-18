#pragma once

namespace gie
{
	class World;
	class Planner;
	class Goal;
}

struct ExampleParameters
{
	gie::World* world{ nullptr };
	gie::Planner* planner{ nullptr };
	gie::Goal* goal{ nullptr };

	inline bool isValid() const
	{
		return world && planner && goal;
	}
};