#pragma once

#include "goapie.h"

namespace gie
{
	gie::Guid nearestEntityGuid(
		gie::World& world,
		const std::vector< gie::Guid >& waypointGuids,
		glm::vec3 location ) 
	{
		for( gie::Guid waypointGuid : waypointGuids )
		{
			auto waypointEntity = world.entity( waypointGuid );
			waypointEntity->property( "Location" );
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
}