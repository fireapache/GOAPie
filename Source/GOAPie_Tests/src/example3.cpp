#include <goapie.h>

#include "example.h"

extern void printSimulatedActions( const gie::Planner& planner );

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
	static constexpr float newAxeIntegrityValue = 3.f;

	// setting goal targets (agent's wood house must exist)
	goal.targets.emplace_back( agentWoodHousePpt->guid(), true );

	// defining a cut down tree action aiming a target tree
	DEFINE_DUMMY_ACTION_CLASS( CutDownTree )

	// minimal amount of integrity an axe need to have to cut down a tree, so there is no need to buy another one
	constexpr float minIntegrity = 1.f;

	// defining simulator for action cut down tree
	class CutDownTreeSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return gie::stringRegister().add( "CutDownTree" ); }

		// define conditions for action
		bool evaluate( gie::EvaluateSimulationParams params ) const override
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
			const auto treeUpTagSet = params.simulation.tagSet( gie::stringHasher( "TreeUp" ) );

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
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::SimulateSimulationParams params ) const override
		{
			// getting Axe Integrity property guid from world context
			auto axeIntegrityPpt = params.agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting Axe Integrity property from current simulation context
			auto simAxeIntegrityPpt = params.simulation.context().property( axeIntegrityPpt->guid() );

			// decreasing axe integrity once tree was cut down
			simAxeIntegrityPpt->value = *simAxeIntegrityPpt->getFloat() - 1.f;

			// getting set of trees still up
			const auto treeUpTagSet = params.simulation.tagSet( gie::stringHasher( "TreeUp" ) );

			// consuming first tree
			gie::Guid treeEntityGuid = *treeUpTagSet->cbegin();
			gie::Entity* treeEntity = params.simulation.context().entity( treeEntityGuid );
			auto& simEntityTagRegister = params.simulation.context().entityTagRegister();
			simEntityTagRegister.untag( treeEntity, { gie::stringHasher( "TreeUp" ) } );
			simEntityTagRegister.tag( treeEntity, { gie::stringHasher( "TreeDown" ) } );

			// creating cut down tree action
			if( auto cutDownTreeAction = std::make_shared< CutDownTreeAction >( arguments() ) )
			{
				// passing target tree entity guid as argument for action
				cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), *treeUpTagSet->cbegin() } );
				// queueing action
				params.simulation.actions.emplace_back( cutDownTreeAction );
				return true;
			}

			return false;
		}

	};

	// defining a cut down tree action aiming a target tree
	class WorkAction : public gie::Action
	{
	public:
		using gie::Action::Action;
		gie::StringHash hash() const override { return gie::stringRegister().add( "Work" ); }
	};

	// defining simulator for action cut down tree
	class WorkSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return gie::stringRegister().add( "Work" ); }

		// define conditions for action
		bool evaluate( gie::EvaluateSimulationParams params ) const override
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
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::SimulateSimulationParams params ) const override
		{
			// getting property Guid to refer in the simulation
			auto moneyPpt = params.agent.worldContextAgent()->property( "Money" );

			// getting property in simulation property
			auto simMoneyPpt = params.simulation.context().property( moneyPpt->guid() );

			// setting money property in simulation's context
			simMoneyPpt->value = *simMoneyPpt->getFloat() + workSalary;

			// creating work action
			if( auto workAction = std::make_shared< WorkAction >( arguments() ) )
			{
				// queueing action
				params.simulation.actions.emplace_back( workAction );
				return true;
			}

			return false;
		}

	};

	// defining an action to tell npc it need to buy something
	DEFINE_DUMMY_ACTION_CLASS( NewThingToBuy )
	
	// defining a buy axe action in case npc needs it
	DEFINE_DUMMY_ACTION_CLASS( BuyAxe )

	// defining simulator for action buy axe
	class BuyAxeSimulator : public gie::ActionSimulator
	{
	public:
		
		using gie::ActionSimulator::ActionSimulator;
		gie::StringHash hash() const override { return gie::stringRegister().add( "BuyAxe" ); }

		// define conditions for action
		bool evaluate( gie::EvaluateSimulationParams params ) const override
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
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::SimulateSimulationParams params ) const override
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

				// creating buy axe action
				if( auto buyAxeAction = std::make_shared< BuyAxeAction >( arguments() ) )
				{
					// queueing action
					params.simulation.actions.emplace_back( buyAxeAction );
					return true;
				}
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

				// creating arguments for action
				gie::NamedArguments actionArguments{};
				actionArguments.add( { gie::stringHasher( "ThingToBuy" ), axeInfoEntityGuid } );

				// creating raise money needed action
				if( auto raiseMoneyNeededAction = std::make_shared< NewThingToBuyAction >( actionArguments ) )
				{
					// queueing action
					params.simulation.actions.emplace_back( raiseMoneyNeededAction );
					return true;
				}
			}

			return true;
		}

	};

	// adding trees to world
	constexpr size_t treeCount = 6;
	for( size_t i = 0; i < treeCount; i++ )
	{
		auto treeEntity = world.createEntity();
		world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "Tree" ), gie::stringHasher( "TreeUp" ) } );
	}

	// setting up planner passing goal and agent to reach the goal
	planner.simulate( goal, *agentEntity );

	// defining available actions and their simulators for planner
	DEFINE_ACTION_SET_ENTRY( CutDownTree )
	DEFINE_ACTION_SET_ENTRY( Work )
	DEFINE_ACTION_SET_ENTRY( BuyAxe )

	// setting available actions
	planner.addActionSetEntry< CutDownTreeActionSetEntry >( gie::stringHasher( "CutDownTree" ) );
	planner.addActionSetEntry< WorkActionSetEntry >( gie::stringHasher( "Work" ) );
	planner.addActionSetEntry< BuyAxeActionSetEntry >( gie::stringHasher( "BuyAxe" ) );

	// finally planner doing its thing
	planner.plan();

	// printing actions from simulation leaf nodes
	printSimulatedActions( planner );

	return 0;
}