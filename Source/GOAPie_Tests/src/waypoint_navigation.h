#pragma once

#include "goapie.h"

namespace gie
{
	gie::Guid nearestEntityGuid( gie::World& world, const std::vector< gie::Guid >& waypointGuids, glm::vec3 location )
	{
		gie::StringHash locationHash = gie::stringHasher( "Location" );
		for( gie::Guid waypointGuid : waypointGuids )
		{
			auto waypointEntity = world.entity( waypointGuid );
			if( auto locationPpt = waypointEntity->property( locationHash ) )
			{
				locationPpt->getVec3()
			}
		}
	}

	std::vector< gie::Guid > getPath(
		gie::World& world,
		const std::vector< gie::Guid >& waypointGuids,
		glm::vec3 start,
		glm::vec3 end )
	{
		std::vector< gie::Guid > result;

		return result;
	}
} // namespace gie