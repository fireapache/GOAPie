
#include <functional>
#include <array>

#include <goapie.h>

#include "example.h"
#include "waypoint_navigation.h"

#include <imgui.h>

void printSimulatedActions( const gie::Planner& planner );
float remapRange( float source, float sourceFrom, float sourceTo, float targetFrom, float targetTo );
void ImGuiFunc( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid );

int treesOnHill( ExampleParameters& params )
{
	gie::World& world = params.world;
	gie::Planner& planner = params.planner;
	gie::Goal& goal = params.goal;

	// specific example visualization
	params.imGuiDrawFunc = &ImGuiFunc;

	// creating agent (aka npc)
	auto agentEntity = world.createAgent();

	// registering agent with tag to be found later in simulation
	world.context().entityTagRegister().tag( agentEntity, { gie::stringHasher( "Agent" ) } );

	// NOTE: this is a step towards the next (more complex)
	// tutorial, a wood house is not being built here yet.
	
	// property telling if agent has a wood house
	auto agentWoodHousePpt = agentEntity->createProperty( "WoodHouse", false );

	// creating agent properties for this tutorial

	// total money npc carries
	agentEntity->createProperty( "Money", 0.f );
	// things (e.g. axe) npc need to buy
	agentEntity->createProperty( "ThingsToBuy", gie::Property::GuidVector{} );
	// integrity of axe npc is carrying
	agentEntity->createProperty( "AxeIntegrity", 0.f );
	// initial position of agent
	agentEntity->createProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );

	// trees configuration
	constexpr glm::vec2 treeAreaExtent{ 2.f, 2.f };
	constexpr glm::vec3 treeAreaCenter{ 0.f, 20.f, 0.f };
	constexpr int32_t treeMatrixOrder = 3;
	static_assert( treeMatrixOrder > 1 );

	// creating way points which npc can move
	constexpr size_t waypointCount = 14;
	std::array< glm::vec3, waypointCount > waypointLocations
	{
		glm::vec3{   0.f,   0.f,  0.f },
		glm::vec3{  10.f,   5.f,  0.f },
		glm::vec3{  20.f,  10.f,  0.f },
		glm::vec3{  10.f,  15.f,  0.f },
		glm::vec3{   0.f,  20.f,  0.f },
		glm::vec3{ -10.f,  15.f,  0.f },
		glm::vec3{ -20.f,  10.f,  0.f },
		glm::vec3{ -10.f,   5.f,  0.f },
		glm::vec3{  10.f, -10.f,  0.f },
		glm::vec3{ -10.f, -10.f,  0.f },
		glm::vec3{ -15.f,  -5.f,  0.f },
		glm::vec3{ -20.f, -10.f,  0.f },
		glm::vec3{ -15.f, -15.f,  0.f },
		glm::vec3{   0.f,  20.f, 20.f }
	};
	std::array< gie::Property::GuidVector*, waypointCount > waypointLinks;

	std::vector< gie::Entity* > waypoints;
	std::vector< gie::Guid > waypointGuids;
	waypoints.reserve( waypointCount );
	waypointGuids.reserve( waypointCount );
	for( size_t i = 0; i < waypointCount; i++ )
	{
		auto& waypointLocation = waypointLocations[ i ];
		auto& createdWaypoint = waypoints.emplace_back( world.createEntity() );
		world.context().entityTagRegister().tag( createdWaypoint, { gie::stringHasher( "Waypoint" ) } );
		waypointGuids.push_back( createdWaypoint->guid() );
		createdWaypoint->createProperty( "Location", waypointLocation );
		auto linksPpt = createdWaypoint->createProperty( "Links", gie::Property::GuidVector{} );
		waypointLinks[ i ] = linksPpt->getGuidArray();
	}

	// connecting waypoints
	constexpr bool linkLadder = false;

	{
		gie::Guid wp0Guid = waypoints[ 0 ]->guid();
		gie::Guid wp1Guid = waypoints[ 1 ]->guid();
		gie::Guid wp2Guid = waypoints[ 2 ]->guid();
		gie::Guid wp3Guid = waypoints[ 3 ]->guid();
		gie::Guid wp4Guid = waypoints[ 4 ]->guid();
		gie::Guid wp5Guid = waypoints[ 5 ]->guid();
		gie::Guid wp6Guid = waypoints[ 6 ]->guid();
		gie::Guid wp7Guid = waypoints[ 7 ]->guid();
		gie::Guid wp8Guid = waypoints[ 8 ]->guid();
		gie::Guid wp9Guid = waypoints[ 9 ]->guid();
		gie::Guid wp10Guid = waypoints[ 10 ]->guid();
		gie::Guid wp11Guid = waypoints[ 11 ]->guid();
		gie::Guid wp12Guid = waypoints[ 12 ]->guid();
		gie::Guid wp13Guid = waypoints[ 13 ]->guid();

		// wp0
		waypointLinks[ 0 ]->push_back( wp1Guid );
		waypointLinks[ 0 ]->push_back( wp7Guid );
		// wp1 (not linked to wp13 initially)
		if( linkLadder )
		{
			waypointLinks[ 1 ]->push_back( wp13Guid );
		}
		waypointLinks[ 1 ]->push_back( wp2Guid );
		waypointLinks[ 1 ]->push_back( wp0Guid );
		// wp2
		waypointLinks[ 2 ]->push_back( wp3Guid );
		waypointLinks[ 2 ]->push_back( wp1Guid );
		// wp3
		waypointLinks[ 3 ]->push_back( wp4Guid );
		waypointLinks[ 3 ]->push_back( wp2Guid );
		// wp4
		waypointLinks[ 4 ]->push_back( wp5Guid );
		waypointLinks[ 4 ]->push_back( wp3Guid );
		waypointLinks[ 4 ]->push_back( wp13Guid );
		// wp5
		waypointLinks[ 5 ]->push_back( wp6Guid );
		waypointLinks[ 5 ]->push_back( wp4Guid );
		// wp6
		waypointLinks[ 6 ]->push_back( wp7Guid );
		waypointLinks[ 6 ]->push_back( wp5Guid );
		// wp7
		waypointLinks[ 7 ]->push_back( wp0Guid );
		waypointLinks[ 7 ]->push_back( wp6Guid );
		// wp8
		waypointLinks[ 8 ]->push_back( wp1Guid );
		waypointLinks[ 8 ]->push_back( wp0Guid );
		waypointLinks[ 8 ]->push_back( wp9Guid );
		// wp9
		waypointLinks[ 9 ]->push_back( wp0Guid );
		waypointLinks[ 9 ]->push_back( wp8Guid );
		waypointLinks[ 9 ]->push_back( wp10Guid );
		waypointLinks[ 9 ]->push_back( wp11Guid );
		waypointLinks[ 9 ]->push_back( wp12Guid );
		// wp10
		waypointLinks[ 10 ]->push_back( wp9Guid );
		// wp11
		waypointLinks[ 11 ]->push_back( wp9Guid );
		// wp12
		waypointLinks[ 12 ]->push_back( wp9Guid );
		// wp13 (not linking it initially)
		if( linkLadder )
		{
			waypointLinks[ 13 ]->push_back( wp1Guid );
		}
		waypointLinks[ 13 ]->push_back( wp4Guid );
	}

	auto pathResult = gie::getPath( world, waypointGuids, glm::vec3{ 0.f, 0.f, 0.f }, glm::vec3{ 0.f, 20.f, 20.f } );
	gie::printPath( waypointGuids, pathResult );

	// price to get an axe
	constexpr float axePrice = 15.f;

	// creating entity defining axe (a thing) npc can buy
	auto axeInfoEntity = world.createEntity();
	axeInfoEntity->createProperty( "Name", "Axe" );
	axeInfoEntity->createProperty( "Price", axePrice );

	// registering entity with tag to be found later in simulation
	world.context().entityTagRegister().tag( axeInfoEntity, { gie::stringHasher( "AxeInfo" ) } );

	// money earn from work
	constexpr float workSalary = 20.0;
	// brand new axe integrity 
	static constexpr float newAxeIntegrityValue = 3.f;

	// setting goal targets (agent's wood house must exist)
	goal.targets.emplace_back( agentWoodHousePpt->guid(), true );

	// creating string register to point string hashes back to strings
	static gie::StringRegister stringRegister;

	// defining a cut down tree action aiming a target tree
	class CutDownTreeAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "CutDownTree"; }
		gie::StringHash hash() const override { return stringRegister.add( "CutDownTree" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting target tree from arguments
			gie::Guid targetTreeGuid = std::get< gie::Guid >( arguments().get( "TargetTree" ) );
			auto targetTree = agent.world()->entity( targetTreeGuid );
			if( !targetTree )
			{
				return false;
			}

			// retagging tree
			auto& entityTagRegister = agent.world()->context().entityTagRegister();
			entityTagRegister.untag( targetTree, { gie::stringHasher( "TreeUp" ) } );
			entityTagRegister.tag( targetTree, { gie::stringHasher( "TreeDown" ) } );

			return true;
		}
	};

	// minimal amount of integrity an axe need to have to cut down a tree, so there is no need to buy another one
	constexpr float minAxeIntegrity = 1.f;

	// defining simulator for action cut down tree
	class CutDownTreeSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		std::string_view name() const override { return "CutDownTree"; }
		gie::StringHash hash() const override { return stringRegister.add( "CutDownTree" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& baseSimulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			auto& context			= baseSimulation.context();
			auto agentEntity		= context.entity( agent.guid() );
			auto axeIntegrityPpt	= agentEntity->property( "AxeIntegrity" );

			// checking minimal axe integrity to cut down a tree
			if( *axeIntegrityPpt->getFloat() < minAxeIntegrity )
			{
				return false;
			}

			// getting set of trees still up
			const auto treeUpTagSet = context.entityTagRegister().tagSet( "TreeUp" );

			// no tree up found
			if( treeUpTagSet->empty() )
			{
				return false;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			auto& context				= simulation.context();
			auto agentEntity			= context.entity( agent.guid() );
			auto axeIntegrityPpt		= agentEntity->property( "AxeIntegrity" );

			// decreasing axe integrity once tree was cut down
			axeIntegrityPpt->value		= *axeIntegrityPpt->getFloat() - 1.f;

			// getting set of trees still up
			const auto treeUpTagSet		= simulation.tagSet( "TreeUp" );

			// consuming first tree
			auto treeEntityGuid			= *treeUpTagSet->cbegin();
			auto treeEntity				= context.entity( treeEntityGuid );
			auto& entityTagRegister		= context.entityTagRegister();

			entityTagRegister.untag( treeEntity, { gie::stringHasher( "TreeUp" ) } );
			entityTagRegister.tag( treeEntity, { gie::stringHasher( "TreeDown" ) } );

			// creating cut down tree action
			if( auto cutDownTreeAction = std::make_shared< CutDownTreeAction >( arguments() ) )
			{
				cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), treeEntityGuid } );
				simulation.actions.emplace_back( cutDownTreeAction );
				return true;
			}

			return false;
		}

	};

	// defining a cut down tree action aiming a target tree
	class WorkAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "Work"; }
		gie::StringHash hash() const override { return stringRegister.add( "Work" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			auto moneyPpt = agent.property( "Money" );
			auto moneyNeededPpt = agent.property( "MoneyNeeded" );

			// adding money to agent in world's context
			moneyPpt->value = *moneyPpt->getFloat() + workSalary;

			// updating money needed
			moneyNeededPpt->value = std::get< float >( arguments().get( "NewMoneyNeeded" ) );

			return true;
		}
	};

	// defining simulator for action cut down tree
	class WorkSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		std::string_view name() const override { return "Work"; }
		gie::StringHash hash() const override { return stringRegister.add( "Work" ); }

		// define conditions for action
		bool prerequisites( const gie::Simulation& baseSimulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			auto& context		= baseSimulation.context();
			auto agentEntity	= context.entity( agent.guid() );
			auto moneyPpt		= agentEntity->property( "Money" );
			auto thingsToBuyPpt	= agentEntity->property( "ThingsToBuy" );

			// getting cost of things to buy
			float cost = 0.f;
			auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
			for( gie::Guid thingToBuyGuid : *thingsToBuyArray )
			{
				if( auto thingToBuyEntity = context.entity( thingToBuyGuid ) )
				{
					auto thingPricePpt = thingToBuyEntity->property( "Price" );
					if( thingPricePpt )
					{
						cost += *thingPricePpt->getFloat();
					}
				}
			}

			// no need to work if have enough money to buy stuff
			if( *moneyPpt->getFloat() >= cost )
			{
				return false;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			auto& context		= simulation.context();
			auto agentEntity	= context.entity( agent.guid() );
			auto moneyPpt		= agentEntity->property( "Money" );

			// adding up money
			moneyPpt->value		= *moneyPpt->getFloat() + workSalary;

			// creating work action
			if( auto workAction = std::make_shared< WorkAction >( arguments() ) )
			{
				simulation.actions.emplace_back( workAction );
				return true;
			}

			return false;
		}

	};

	// defining an action to tell npc it need to buy something
	class NewThingToBuyAction : public gie::Action
	{
	public:

		using gie::Action::Action;

		std::string_view name() const override { return "NewThingToBuy"; }
		gie::StringHash hash() const override { return stringRegister.add( "NewThingToBuy" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting guid of thing npc is suppose to buy
			auto thingToBuyGuid = std::get< gie::Guid >( arguments().get( gie::stringHasher( "ThingToBuy" ) ) );

			// getting agent property in world context
			auto ThingsToBuyPpt = agent.property( "ThingsToBuy" );
			if( !ThingsToBuyPpt )
			{
				return false;
			}

			// adding axe as a thing to be bought by npc
			ThingsToBuyPpt->getGuidArray()->emplace_back( thingToBuyGuid );

			return true;
		}
	};


	// defining a buy axe action in case npc needs it
	class BuyAxeAction : public gie::Action
	{
	public:

		using gie::Action::Action;

		std::string_view name() const override { return "BuyAxe"; }
		gie::StringHash hash() const override { return stringRegister.add( "BuyAxe" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting agent axe integrity property in world context
			auto axeIntegrityPpt = agent.property( "AxeIntegrity" );
			if( !axeIntegrityPpt )
			{
				return false;
			}

			// setting axe integrity to agent in world context
			axeIntegrityPpt->value = newAxeIntegrityValue;

			// getting agent money property in world context
			auto moneyPpt = agent.property( "Money" );
			if( !moneyPpt )
			{
				return false;
			}

			// setting new money value to agent in world context
			moneyPpt->value = std::max( *moneyPpt->getFloat() - axePrice, 0.f );

			return true;
		}
	};

	// defining simulator for action buy axe
	class BuyAxeSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;

		std::string_view name() const override { return "BuyAxe"; }
		gie::StringHash hash() const override { return stringRegister.add( "BuyAxe" ); }

		bool prerequisites( const gie::Simulation& baseSimulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			auto& context			= baseSimulation.context();
			auto agentEntity		= context.entity( agent.guid() );
			auto axeIntegrityPpt	= agentEntity->property( "AxeIntegrity" );

			// no need to buy axe if already has one with enough integrity
			if( *axeIntegrityPpt->getFloat() >= minAxeIntegrity )
			{
				return false;
			}

			// simulation will either buy axe or add axe to things to buy list

			return true;
		}

		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			auto& context				= simulation.context();
			auto agentEntity			= context.entity( agent.guid() );
			auto thingsToBuyPpt			= agentEntity->property( "ThingsToBuy" );
			auto axeInfoTagSet			= context.entityTagRegister().tagSet( "AxeInfo" );
			auto axeInfoEntityGuid		= *axeInfoTagSet->cbegin();
			auto moneyPpt				= agentEntity->property( "Money" );

			// buy axe if there is enough money
			if( *moneyPpt->getFloat() >= axePrice )
			{
				auto axeIntegrityPpt	= agentEntity->property( "AxeIntegrity" );
				axeIntegrityPpt->value	= newAxeIntegrityValue;

				// consuming money
				moneyPpt->value			= *moneyPpt->getFloat() - axePrice;

				// removing axe from things to buy property
				auto thingsToBuyArray	= thingsToBuyPpt->getGuidArray();
				auto newArrayEnd		= std::remove( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid );

				thingsToBuyArray->erase( newArrayEnd, thingsToBuyArray->end() );

				// creating buy axe action
				if( auto buyAxeAction = std::make_shared< BuyAxeAction >( arguments() ) )
				{
					simulation.actions.emplace_back( buyAxeAction );
					return true;
				}
			}
			else
			{
				// no enough money to buy an axe, lets add axe to the
				// "things to buy" list so agent has to look for money
				// to afford it.

				auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();

				if( std::find( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid ) != thingsToBuyArray->end() )
				{
					// it's already set to buy axe
					return false;
				}

				thingsToBuyArray->emplace_back( axeInfoEntityGuid );

				// setting up action to be used outside the simulation
				gie::NamedArguments actionArguments{};
				actionArguments.add( { gie::stringHasher( "ThingToBuy" ), axeInfoEntityGuid } );

				if( auto raiseMoneyNeededAction = std::make_shared< NewThingToBuyAction >( actionArguments ) )
				{
					simulation.actions.emplace_back( raiseMoneyNeededAction );
					return true;
				}
			}

			return false;
		}

	};

	// setting tree positions
	constexpr size_t treeCount = 6;
	std::array< glm::vec3, treeCount > treeLocations
	{
		glm::vec3{  -6.f,   0.f,  0.f },
		glm::vec3{  12.f,  -3.f,  0.f },
		glm::vec3{  25.f,   2.f,  0.f },
		glm::vec3{   8.f,  -4.f,  0.f },
		glm::vec3{   0.f,  -3.f,  0.f },
		glm::vec3{ -12.f,  -4.f,  0.f }
	};

	// adding trees to world
	for( size_t i = 0; i < treeCount; i++ )
	{
		auto treeEntity = world.createEntity();
		treeEntity->createProperty( "Location", treeLocations[ i ] );
		world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "Tree" ), gie::stringHasher( "TreeUp" ) } );
	}

	// setting up planner passing goal and agent to reach the goal
	planner.setup( goal, *agentEntity );

	// defining available actions and their simulators for planner
	DEFINE_ACTION_SET_ENTRY( CutDownTree )
	DEFINE_ACTION_SET_ENTRY( Work )
	DEFINE_ACTION_SET_ENTRY( BuyAxe )

	// setting available actions
	planner.addActionSetEntry< CutDownTreeActionSetEntry >( gie::stringHasher( "CutDownTree" ) );
	planner.addActionSetEntry< WorkActionSetEntry >( gie::stringHasher( "Work" ) );
	planner.addActionSetEntry< BuyAxeActionSetEntry >( gie::stringHasher( "BuyAxe" ) );

	return 0;
}

inline float remapRange( float source, float sourceFrom, float sourceTo, float targetFrom, float targetTo )
{
	return targetFrom + ( source - sourceFrom ) * ( targetTo - targetFrom ) / ( sourceTo - sourceFrom );
}

void ImGuiFunc( gie::World& world, gie::Planner& planner, gie::Goal& goal, gie::Guid selectedSimulationGuid )
{
	// context comes from either a simulation or the world
	gie::Blackboard* context = nullptr;

	auto selectedSimulation = planner.simulation( selectedSimulationGuid );
	if( selectedSimulation )
	{
		context = &selectedSimulation->context();
	}

	if( !context )
	{
		context = &world.context();
	}

	// getting agent entity
	auto agentEntity = context->entity( planner.agent()->guid() );
	if( !agentEntity )
	{
		return;
	}
	// getting agent location property
	auto agentLocationPpt = agentEntity->property( "Location" );
	if( !agentLocationPpt )
	{
		return;
	}

	glm::vec3 agentLocation = *agentLocationPpt->getVec3();
	ImGui::Text( "Agent Location: (%.2f, %.2f, %.2f)", agentLocation.x, agentLocation.y, agentLocation.z );
	ImGui::Text( "Agent Money: %.2f", *agentEntity->property( "Money" )->getFloat() );
	ImGui::Text( "Axe Integrity: %.2f", *agentEntity->property( "AxeIntegrity" )->getFloat() );
	ImGui::Text( "Things to buy: " );

	auto thingsToBuyPpt = agentEntity->property( "ThingsToBuy" );
	if( thingsToBuyPpt )
	{
		auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
		for( const auto& thingGuid : *thingsToBuyArray )
		{
			if( auto thingEntity = context->entity( thingGuid ) )
			{
				auto thingNameHash = thingEntity->property( "Name" )->getStringHash();
				if( thingNameHash == gie::InvalidStringHash )
				{
					continue;
				}
				auto thingName = gie::stringRegister().get( *thingNameHash );
				ImGui::Text( "- %s", thingName );
			}
		}
	}
}