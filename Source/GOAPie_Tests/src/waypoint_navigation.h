#pragma once

#include "goapie.h"

#include <limits>

namespace gie
{
	inline Guid nearestWaypoint( World& world, const std::vector< Guid >& waypointGuids, glm::vec3 referencelocation )
	{
		Guid closestWaypoint = NullGuid;
		float closestDistance = std::numeric_limits< float >::max();
		StringHash locationHash = stringHasher( "Location" );

		for( Guid waypointGuid : waypointGuids )
		{
			auto waypointEntity = world.entity( waypointGuid );
			if( auto locationPpt = waypointEntity->property( locationHash ) )
			{
				const float currentDistance = glm::distance( referencelocation, *locationPpt->getVec3() );
				if( currentDistance < closestDistance )
				{
					closestDistance = currentDistance;
					closestWaypoint = waypointGuid;
				}
			}
		}

		return closestWaypoint;
	}

	inline std::vector< Guid > getPath( World& world, const std::vector< Guid >& waypointGuids, glm::vec3 start, glm::vec3 end )
	{
		// validating inputs

		if( waypointGuids.empty() )
		{
			return {};
		}

		Guid startNode = nearestWaypoint( world, waypointGuids, start );
		Guid endNode = nearestWaypoint( world, waypointGuids, end );

		if( startNode == NullGuid || endNode == NullGuid )
		{
			return {};
		}

		// setting default values
		for( Guid nodeGuid : waypointGuids )
		{
			Entity* nodeEntity = world.entity( nodeGuid );
			nodeEntity->createProperty( "Cost", std::numeric_limits< float >::max() );
			nodeEntity->createProperty( "Heuristic", std::numeric_limits< float >::max() );
			nodeEntity->createProperty( "Backtrack", NullGuid );
		}

		const glm::vec3 endNodeLocation = *world.entity( endNode )->property( "Location" )->getVec3();

		{
			Entity* nodeEntity = world.entity( startNode );
			nodeEntity->property( "Cost" )->value = 0.f;
			glm::vec3 nodeLocation = *nodeEntity->property( "Location" )->getVec3();
			nodeEntity->property( "Heuristic" )->value = glm::distance( nodeLocation, endNodeLocation );
		}

		// searching for path

		std::vector< Guid > result{};
		std::vector< Guid > openedNodes{ startNode };
		std::vector< Guid > visitedNodes{};
		visitedNodes.reserve( waypointGuids.size() );

		Guid goalNode{ NullGuid };

		while( !openedNodes.empty() )
		{
			// getting best node to expand
			Guid bestNode = NullGuid;
			float bestNodeAssumedCost = std::numeric_limits< float >::max();
			for( auto openedNode : openedNodes )
			{
				Entity* nodeEntity = world.entity( openedNode );
				const float cost = *nodeEntity->property( "Cost" )->getFloat();
				const float heuristic = *nodeEntity->property( "Heuristic" )->getFloat();
				const float assumedCost = cost + heuristic;
				if( assumedCost < bestNodeAssumedCost )
				{
					bestNodeAssumedCost = assumedCost;
					bestNode = openedNode;
				}
			}

			// marking as visited so it's not checkd it again
			visitedNodes.push_back( bestNode );

			// checking if reached goal node
			if( bestNode == endNode )
			{
				goalNode = bestNode;
				break;
			}

			// expanding best node
			if( Entity* bestNodeEntity = world.entity( bestNode ) )
			{
				const glm::vec3 bestNodeLocation = *bestNodeEntity->property( "Location" )->getVec3();
				const float bestNodeCost = *bestNodeEntity->property( "Cost" )->getFloat();

				auto linkedNodes = bestNodeEntity->property( "Links" )->getGuidArray();
				for( Guid linkedNode : *linkedNodes )
				{
					auto visitedNodeItr = std::find( visitedNodes.begin(), visitedNodes.end(), linkedNode );
					if( visitedNodeItr != visitedNodes.end() )
					{
						// this node was already visited
						continue;
					}

					// marking linked node as opened node for further graph expansion
					auto openedNodeItr = std::find( openedNodes.begin(), openedNodes.end(), linkedNode );
					if( openedNodeItr == openedNodes.end() )
					{
						openedNodes.push_back( linkedNode );
					}

					// setting linked node cost and heuristic value
					Entity* linkedNodeEntity = world.entity( linkedNode );
					const glm::vec3 linkedNodeLocation = *linkedNodeEntity->property( "Location" )->getVec3();
					const float linkedNodeDistance = glm::distance( bestNodeLocation, linkedNodeLocation );
					linkedNodeEntity->property( "Cost" )->value = bestNodeCost + linkedNodeDistance;
					const float distanceToGoal = glm::distance( linkedNodeLocation, endNodeLocation );
					linkedNodeEntity->property( "Heuristic" )->value = distanceToGoal;
					linkedNodeEntity->property( "Backtrack" )->value = bestNode;
				}
			}

			// removing just expanded node from opened nodes set
			auto openedNodesNewEnd = std::remove( openedNodes.begin(), openedNodes.end(), bestNode );
			if( openedNodesNewEnd != openedNodes.end() )
			{
				openedNodes.erase( openedNodesNewEnd, openedNodes.end() );
			}
		}

		// backtracking path
		if( goalNode != NullGuid )
		{
			Guid backtrackNode = goalNode;
			while( backtrackNode != NullGuid )
			{
				result.push_back( backtrackNode );
				Entity* backtrackNodeEntity = world.entity( backtrackNode );
				backtrackNode = *backtrackNodeEntity->property( "Backtrack" )->getGuid();
			}
			std::reverse( result.begin(), result.end() );
		}

		return result;
	}

	inline void printPath( const std::vector< Guid >& waypointGuids, const std::vector< Guid >& path )
	{
		if( path.size() > 0 && waypointGuids.size() > 0 && waypointGuids.size() > path.size() )
		{
			for( gie::Guid pathNode : path )
			{
				auto waypointGuidItr = std::find( waypointGuids.begin(), waypointGuids.end(), pathNode );
				if( waypointGuidItr == waypointGuids.end() )
				{
					continue;
				}
				auto waypointIndex = std::distance( waypointGuids.begin(), waypointGuidItr );
				std::cout << "wp" << waypointIndex << " ";
			}
			std::cout << std::endl;
		}
	}

} // namespace gie