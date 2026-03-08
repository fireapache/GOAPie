#include <goapie.h>

#include "example.h"

extern void printSimulatedActions( const gie::Planner& planner );

const char* cutDownTreesDescription()
{
    return "Agent gathers logs by cutting down trees and may build a house when enough logs are available.";
}

int cutDownTrees( ExampleParameters& params )
{
	gie::World& world = params.world;
	gie::Planner& planner = params.planner;
	gie::Goal& goal = params.goal;

	// creating agent (aka npc)
	auto agentEntity = world.createAgent();

	// NOTE: this is a step towards the next (more complex)
	// tutorial, a wood house is not being built here yet.

	// property telling if agent has a wood house
	auto agentWoodHousePpt = agentEntity->createProperty( "WoodHouse", false );

	// Define archetypes used in this example (only once per world)
	if( world.archetypes().empty() )
	{
		// Tree archetype
		if( auto* a = world.createArchetype( "Tree" ) )
		{
			a->addTag( "Tree" );
			a->addTag( "TreeUp" );
			a->addTag( "Draw" );
			a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
		}

		// Waypoint archetype (not used here but useful in editor)
		if( auto* a = world.createArchetype( "Waypoint" ) )
		{
			a->addTag( "Waypoint" );
			a->addTag( "Draw" );
			a->addProperty( "Location", glm::vec3{ 0.f, 0.f, 0.f } );
			a->addProperty( "Links", gie::Property::GuidVector{} );
		}
	}

	// creating agent properties for this tutorial

	// total money npc carries
	agentEntity->createProperty( "Money", 0.f );
	// things (e.g. axe) npc need to buy
	agentEntity->createProperty( "ThingsToBuy", gie::Property::GuidVector{} );
	// integrity of axe npc is carrying
	agentEntity->createProperty( "AxeIntegrity", 0.f );

	// price to get an axe
	constexpr float axePrice = 15.f;

	// creating entity defining axe (a thing) npc can buy
	auto axeInfoEntity = world.createEntity();
	axeInfoEntity->createProperty( "Price", axePrice );

	// registering entity with tag to be found later in simulation
	world.context().entityTagRegister().tag( axeInfoEntity, { gie::stringHasher( "AxeInfo" ) } );

	// money earn from work
	constexpr float workSalary = 20.0;
	// brand new axe integrity
	constexpr float newAxeIntegrityValue = 3.f;

	// minimal amount of integrity an axe need to have to cut down a tree, so there is no need to buy another one
	constexpr float minIntegrity = 1.f;

	// setting goal targets (agent's wood house must exist)
	goal.targets.emplace_back( agentWoodHousePpt->guid(), true );

	// adding trees to world
	constexpr size_t treeCount = 6;
	for( size_t i = 0; i < treeCount; i++ )
	{
		auto treeEntity = world.createEntity();
		world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "Tree" ), gie::stringHasher( "TreeUp" ) } );
	}

	// setting up planner passing goal and agent to reach the goal
	planner.simulate( goal, *agentEntity );

	// defining simulator for action cut down tree
	planner.addLambdaAction( "CutDownTree",
		// define conditions for action
		[minIntegrity]( gie::EvaluateSimulationParams params ) -> bool
		{
			// getting Axe Integrity property guid from world context
			auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting Axe Integrity property from current simulation context
			auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );

			// return if no property was found
			if( !simAxeIntegrityPpt )
			{
				return false;
			}

			// checking minimal axe integrity to cut down a tree
			if( *simAxeIntegrityPpt->getFloat() < minIntegrity )
			{
				return false;
			}

			// getting set of trees still up
			auto treeUpTagSet = params.simulation.tagSet( gie::stringHasher( "TreeUp" ) );

			// no tag set found
			if( !treeUpTagSet )
			{
				return false;
			}

			// no tree up found
			if( treeUpTagSet->empty() )
			{
				return false;
			}

			return true;
		},
		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		[]( gie::SimulateSimulationParams params ) -> bool
		{
			// getting Axe Integrity property guid from world context
			auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting Axe Integrity property from simulation context
			auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );

			// decreasing axe integrity once tree was cut down
			simAxeIntegrityPpt->value = *simAxeIntegrityPpt->getFloat() - 1.f;

			// getting set of trees still up
			auto treeUpTagSet = params.simulation.tagSet( gie::stringHasher( "TreeUp" ) );

			// consuming first tree
			gie::Guid treeEntityGuid = *treeUpTagSet->cbegin();
			gie::Entity* treeEntity = params.simulation.context().entity( treeEntityGuid );
			auto& simEntityTagRegister = params.simulation.context().entityTagRegister();
			simEntityTagRegister.untag( treeEntity, { gie::stringHasher( "TreeUp" ) } );
			simEntityTagRegister.tag( treeEntity, { gie::stringHasher( "TreeDown" ) } );

			// creating cut down tree action with target tree argument
			auto cutDownTreeAction = std::make_shared< gie::Action >( gie::stringHasher( "CutDownTree" ) );
			cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), *treeUpTagSet->cbegin() } );
			params.simulation.actions.emplace_back( cutDownTreeAction );
			return true;
		}
	);

	// defining simulator for action work
	planner.addLambdaAction( "Work",
		// define conditions for action
		[workSalary]( gie::EvaluateSimulationParams params ) -> bool
		{
			// getting property Guid to refer in the simulation
			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );
			auto thingsToBuyPpt = params.agent.worldContextAgent()->property( "ThingsToBuy" );

			// getting property in simulation property
			const auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );
			const auto simThingsToBuyPpt = params.simulation.context().property( thingsToBuyPpt->guid() );

			// getting cost of things to buy
			float cost = 0.f;
			auto thingsToBuyArray = simThingsToBuyPpt->getGuidArray();
			for( gie::Guid thingToBuyGuid : *thingsToBuyArray )
			{
				if( const auto thingToBuyEntity = params.simulation.context().entity( thingToBuyGuid ) )
				{
					auto thingPricePpt = thingToBuyEntity->property( "Price" );
					if( thingPricePpt )
					{
						cost += *thingPricePpt->getFloat();
					}
				}
			}

			// no need to work if have enough money to buy stuff
			if( *simMoneyPpt->getFloat() >= cost )
			{
				return false;
			}

			return true;
		},
		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		[workSalary]( gie::SimulateSimulationParams params ) -> bool
		{
			// getting property Guid to refer in the simulation
			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );

			// getting property in simulation property
			auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );

			// setting money property in simulation's context
			simMoneyPpt->value = *simMoneyPpt->getFloat() + workSalary;

			return true;
		}
	);

	// defining simulator for action buy axe
	planner.addLambdaAction( "BuyAxe",
		// define conditions for action
		[axePrice, minIntegrity]( gie::EvaluateSimulationParams params ) -> bool
		{
			// getting agent axe integrity property guid from world context
			auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting axe integrity property from simulation context
			auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );
			if( !simAxeIntegrityPpt )
			{
				return false;
			}

			// checking minimal axe integrity
			if( *simAxeIntegrityPpt->getFloat() >= minIntegrity )
			{
				return false;
			}

			// getting agent money property guid from world context
			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );

			// getting money property from simulation context
			auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );
			if( !simMoneyPpt )
			{
				return false;
			}

			// checking if has enough money to buy axe
			if( *simMoneyPpt->getFloat() < axePrice )
			{
				// lets proceed with simulation so it will so it
				// will add action to buy an axe.
				return true;
			}

			return true;
		},
		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		[axePrice, newAxeIntegrityValue]( gie::SimulateSimulationParams params ) -> bool
		{
			// getting agent property guid from world context
			auto thingsToBuyPptGuid = params.agent.worldContextAgent()->property( "ThingsToBuy" )->guid();

			// getting agent property from simulation context
			auto thingsToBuyPpt = params.simulation.context().property( thingsToBuyPptGuid );
			if( !thingsToBuyPpt )
			{
				return false;
			}

			// getting axe info entity
			auto axeInfoTagSet = params.agent.worldContextAgent()->world()->context().entityTagRegister().tagSet( gie::stringHasher( "AxeInfo" ) );
			// TODO: check if entity tag register in simulation context reflects world context
			if( !axeInfoTagSet )
			{
				return false;
			}
			if( axeInfoTagSet->empty() )
			{
				return false;
			}
			gie::Guid axeInfoEntityGuid = *axeInfoTagSet->cbegin();

			// getting agent money property guid from world context
			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );

			// getting money property from simulation context
			auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );
			if( !simMoneyPpt )
			{
				return false;
			}

			// checking if has enough money to buy axe
			if( *simMoneyPpt->getFloat() >= axePrice )
			{
				// getting agent axe integrity property guid from world context
				auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );

				// getting axe integrity property from simulation context
				auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );
				if( !simAxeIntegrityPpt )
				{
					return false;
				}

				// setting new axe integrity value
				simAxeIntegrityPpt->value = newAxeIntegrityValue;

				// consuming money
				simMoneyPpt->value = *simMoneyPpt->getFloat() - axePrice;

				// removing axe from things to buy property
				auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();
				auto newArrayEnd = std::remove( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid );
				if( newArrayEnd != thingsToBuyArray->end() )
				{
					thingsToBuyArray->erase( newArrayEnd, thingsToBuyArray->end() );
				}

				return true;
			}
			else
			{
				// no money to buy an axe, lets raise money needed so
				// agent (character in simulation) and npc (actual character in game)
				// will look for more money.

				auto thingsToBuyArray = thingsToBuyPpt->getGuidArray();

				// is already set to buy thing
				if( std::find( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid ) != thingsToBuyArray->end() )
				{
					return false;
				}

				// finally adding axe to be bought by npc
				thingsToBuyArray->emplace_back( axeInfoEntityGuid );

				// creating raise money needed action
				auto raiseMoneyNeededAction = std::make_shared< gie::Action >( gie::stringHasher( "NewThingToBuy" ) );
				raiseMoneyNeededAction->arguments().add( { gie::stringHasher( "ThingToBuy" ), axeInfoEntityGuid } );
				params.simulation.actions.emplace_back( raiseMoneyNeededAction );
				return true;
			}

			return true;
		}
	);

	// finally planner doing its thing
	planner.plan();

	// printing actions from simulation leaf nodes
	printSimulatedActions( planner );

	return 0;
}

int cutDownTreesValidateResult( std::string& failMsg )
{
	gie::World world{};
	gie::Planner planner{};
	gie::Goal goal{ world };
	ExampleParameters params{ world, planner, goal };

	VALIDATE( cutDownTrees( params ) == 0, "cutDownTrees() setup failed" );

	// Goal is WoodHouse=true but no BuildHouse action exists, so plan is empty
	auto& planned = planner.planActions();
	VALIDATE( planned.empty(), "plan should be empty (no BuildHouse action available)" );

	VALIDATE_EQ( planner.simulations().size(), size_t( 11 ), "simulation count (depth limit 10 chain)" );

	return 0;
}
