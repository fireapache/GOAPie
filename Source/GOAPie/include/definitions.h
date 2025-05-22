#pragma once

#include <limits>
#include <utility>
#include <set>

namespace gie
{
	typedef size_t IdType;
	typedef IdType Guid;
	typedef IdType StringHash;
	typedef IdType Tag;
	typedef std::set< Tag > TagSet;
	typedef std::pair< StringHash, Guid > NamedGuid;
	typedef size_t Checksum;
	constexpr StringHash InvalidStringHash{ 0 };
	constexpr size_t NullGuid{ 0 };
	constexpr size_t NullArguments{ 0 };
	constexpr float MaxHeuristic{ std::numeric_limits< float >::max() };
	constexpr float MinHeuristic{ 0.f };
	constexpr float InvalidHeuristic{ std::numeric_limits< float >::quiet_NaN() };
	constexpr float MaxCost{ std::numeric_limits< float >::max() };
}
