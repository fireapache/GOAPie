#pragma once

#include <limits>

namespace gie
{
	typedef size_t Guid;
	typedef size_t StringHash;
	constexpr StringHash InvalidStringHash{ 0 };
	constexpr size_t NullGuid{ 0 };
	constexpr float MaxHeuristic{ std::numeric_limits< float >::max() };
	constexpr float MinHeuristic{ 0.f };
	constexpr float InvalidHeuristic{ std::numeric_limits< float >::infinity() };
	constexpr float MaxCost{ std::numeric_limits< float >::max() };
}
