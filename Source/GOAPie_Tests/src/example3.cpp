#include "goapie.h"

void printSimulatedActions( const gie::Planner& planner );

int cutDownTrees()
{
	// creating world
	gie::World world;

	// creating agent (aka npc)
	auto [ agentEntityGuid, agentEntity ] = world.createAgent();

	// NOTE: this is a step towards the next (more complex)
	// tutorial, a wood house is not being built here yet.
	
	// property telling if agent has a wood house
	auto [ agentWoodHousePptGuid, agentWoodHousePpt ] = agentEntity->createProperty( "WoodHouse", false );

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
	auto [ axeInfoEntityGuid, axeInfoEntity ] = world.createEntity();
	axeInfoEntity->createProperty( "Price", axePrice );

	// registering entity with tag to be found later in simulation
	world.context().entityTagRegister().tag( axeInfoEntity, { gie::stringHasher( "AxeInfo" ) } );

	// money earn from work
	constexpr float workSalary = 20.0;
	// brand new axe integrity 
	static constexpr float newAxeIntegrityValue = 3.f;

	// creating goal
	gie::Goal goal{ world };

	// setting goal targets (agent's wood house must exist)
	goal.targets.emplace_back( agentWoodHousePptGuid, true );

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
	constexpr float minIntegrity = 1.f;

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
			// getting Axe Integrity property guid from world context
			auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting Axe Integrity property from current simulation context
			auto simAxeIntegrityPpt = baseSimulation.context().property( axeIntegrityPptGuid );

			// return if no property was found
			if( !simAxeIntegrityPpt )
			{
				return false;
			}

			// checking minimal axe integrity to cut down a tree
			if( simAxeIntegrityPpt->getFloat().second < minIntegrity )
			{
				return false;
			}

			// getting set of trees still up
			const auto treeUpTagSet = baseSimulation.tagSet( gie::stringHasher( "TreeUp" ) );

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
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting Axe Integrity property guid from world context
			auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting Axe Integrity property from current simulation context
			auto simAxeIntegrityPpt = simulation.context().property( axeIntegrityPptGuid );

			// decreasing axe integrity once tree was cut down
			simAxeIntegrityPpt->value = simAxeIntegrityPpt->getFloat().second - 1.f;

			// getting set of trees still up
			const auto treeUpTagSet = simulation.tagSet( gie::stringHasher( "TreeUp" ) );

			// consuming first tree
			gie::Guid treeEntityGuid = *treeUpTagSet->cbegin();
			auto& simEntityTagRegister = simulation.context().entityTagRegister();
			simEntityTagRegister.untag( treeEntityGuid, { gie::stringHasher( "TreeUp" ) } );
			simEntityTagRegister.tag( treeEntityGuid, { gie::stringHasher( "TreeDown" ) } );

			// creating cut down tree action
			if( auto cutDownTreeAction = std::make_shared< CutDownTreeAction >( arguments() ) )
			{
				// passing target tree entity guid as argument for action
				cutDownTreeAction->arguments().add( { gie::stringHasher( "TargetTree" ), *treeUpTagSet->cbegin() } );
				// queueing action
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
			auto [ _, moneyPpt ] = agent.property( "Money" );
			auto [ __, moneyNeededPpt ] = agent.property( "MoneyNeeded" );

			// adding money to agent in world's context
			moneyPpt->value = moneyPpt->getFloat().second + workSalary;

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
			// getting property Guid to refer in the simulation
			auto [ moneyPptGuid, _ ] = agent.worldContextAgent()->property( "Money" );
			auto [ thingsToBuyPptGuid, __ ] = agent.worldContextAgent()->property( "ThingsToBuy" );

			// getting property in simulation property
			const auto simMoneyPpt = baseSimulation.context().property( moneyPptGuid );
			const auto simThingsToBuyPpt = baseSimulation.context().property( thingsToBuyPptGuid );

			// getting cost of things to buy
			float cost = 0.f;
			auto thingsToBuyArray = simThingsToBuyPpt->getGuidArray().second;
			for( gie::Guid thingToBuyGuid : *thingsToBuyArray )
			{
				if( const auto thingToBuyEntity = baseSimulation.context().entity( thingToBuyGuid ) )
				{
					auto [ ___, thingPricePpt ] = thingToBuyEntity->property( "Price" );
					if( thingPricePpt )
					{
						cost += thingPricePpt->getFloat().second;
					}
				}
			}

			// no need to work if have enough money to buy stuff
			if( simMoneyPpt->getFloat().second >= cost )
			{
				return false;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting property Guid to refer in the simulation
			auto [ moneyPptGuid, _ ] = agent.worldContextAgent()->property( "Money" );

			// getting property in simulation property
			auto simMoneyPpt = simulation.context().property( moneyPptGuid );

			// setting money property in simulation's context
			simMoneyPpt->value = simMoneyPpt->getFloat().second + workSalary;

			// creating work action
			if( auto workAction = std::make_shared< WorkAction >( arguments() ) )
			{
				// queueing action
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

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "NewThingToBuy"; }
		gie::StringHash hash() const override { return stringRegister.add( "NewThingToBuy" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting guid of thing npc is suppose to buy
			auto thingToBuyGuid = std::get< gie::Guid >( arguments().get( gie::stringHasher( "ThingToBuy" ) ) );

			// getting agent property in world context
			auto [ _, ThingsToBuyPpt ] = agent.property( "ThingsToBuy" );
			if( !ThingsToBuyPpt )
			{
				return false;
			}

			// adding axe as a thing to be bought by npc
			ThingsToBuyPpt->getGuidArray().second->emplace_back( thingToBuyGuid );

			return true;
		}
	};


	// defining a buy axe action in case npc needs it
	class BuyAxeAction : public gie::Action
	{
	public:

		// inheriting constructors
		using gie::Action::Action;

		std::string_view name() const override { return "BuyAxe"; }
		gie::StringHash hash() const override { return stringRegister.add( "BuyAxe" ); }

		// defines how world context and agent are affected by this action
		bool outcome( gie::Agent& agent ) override
		{
			// getting agent axe integrity property in world context
			auto [ _, axeIntegrityPpt ] = agent.property( "AxeIntegrity" );
			if( !axeIntegrityPpt )
			{
				return false;
			}

			// setting axe integrity to agent in world context
			axeIntegrityPpt->value = newAxeIntegrityValue;

			// getting agent money property in world context
			auto [ __, moneyPpt ] = agent.property( "Money" );
			if( !moneyPpt )
			{
				return false;
			}

			// setting new money value to agent in world context
			moneyPpt->value = std::max( moneyPpt->getFloat().second - axePrice, 0.f );

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

		// define conditions for action
		bool prerequisites( const gie::Simulation& baseSimulation, const gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting agent axe integrity property guid from world context
			auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

			// getting axe integrity property from simulation context
			auto simAxeIntegrityPpt = baseSimulation.context().property( axeIntegrityPptGuid );
			if( !simAxeIntegrityPpt )
			{
				return false;
			}

			// checking minimal axe integrity
			if( simAxeIntegrityPpt->getFloat().second >= minIntegrity )
			{
				return false;
			}

			// getting agent money property guid from world context
			auto [ moneyPptGuid, __ ] = agent.worldContextAgent()->property( "Money" );

			// getting money property from simulation context
			auto simMoneyPpt = baseSimulation.context().property( moneyPptGuid );
			if( !simMoneyPpt )
			{
				return false;
			}

			// checking if has enough money to buy axe
			if( simMoneyPpt->getFloat().second < axePrice )
			{
				// lets proceed with simulation so it will so it
				// will add action to buy an axe.
				return true;
			}

			return true;
		}

		// calculate cost and necessary steps (other actions) to achieve the action being simulated
		bool simulate( gie::Simulation& simulation, gie::SimAgent& agent, const gie::Goal& goal ) const override
		{
			// getting agent property guid from world context
			auto [ thingsToBuyPptGuid, __ ] = agent.worldContextAgent()->property( "ThingsToBuy" );

			// getting agent property from simulation context
			auto thingsToBuyPpt = simulation.context().property( thingsToBuyPptGuid );
			if( !thingsToBuyPpt )
			{
				return false;
			}

			// getting axe info entity
			auto axeInfoTagSet = agent.worldContextAgent()->world()->context().entityTagRegister().tagSet( gie::stringHasher( "AxeInfo" ) );
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
			auto [ moneyPptGuid, _ ] = agent.worldContextAgent()->property( "Money" );

			// getting money property from simulation context
			auto simMoneyPpt = simulation.context().property( moneyPptGuid );
			if( !simMoneyPpt )
			{
				return false;
			}

			// checking if has enough money to buy axe
			if( simMoneyPpt->getFloat().second >= axePrice )
			{
				// getting agent axe integrity property guid from world context
				auto [ axeIntegrityPptGuid, _ ] = agent.worldContextAgent()->property( "AxeIntegrity" );

				// getting axe integrity property from simulation context
				auto simAxeIntegrityPpt = simulation.context().property( axeIntegrityPptGuid );
				if( !simAxeIntegrityPpt )
				{
					return false;
				}

				// setting new axe integrity value
				simAxeIntegrityPpt->value = newAxeIntegrityValue;

				// consuming money
				simMoneyPpt->value = simMoneyPpt->getFloat().second - axePrice;

				// removing axe from things to buy property
				auto thingsToBuyArray = thingsToBuyPpt->getGuidArray().second;
				auto newArrayEnd = std::remove( thingsToBuyArray->begin(), thingsToBuyArray->end(), axeInfoEntityGuid );
				if( newArrayEnd != thingsToBuyArray->end() )
				{
					thingsToBuyArray->erase( newArrayEnd, thingsToBuyArray->end() );
				}

				// creating buy axe action
				if( auto buyAxeAction = std::make_shared< BuyAxeAction >( arguments() ) )
				{
					// queueing action
					simulation.actions.emplace_back( buyAxeAction );
					return true;
				}
			}
			else
			{
				// no money to buy an axe, lets raise money needed so
				// agent (character in simulation) and npc (actual character in game)
				// will look for more money.

				auto thingsToBuyArray = thingsToBuyPpt->getGuidArray().second;

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
					simulation.actions.emplace_back( raiseMoneyNeededAction );
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
		auto [ treeEntityGuid, treeEntity ] = world.createEntity();
		world.context().entityTagRegister().tag( treeEntity, { gie::stringHasher( "Tree" ), gie::stringHasher( "TreeUp" ) } );
	}

	// creating planner passing goal and agent to reach the goal
	gie::Planner planner{ goal, *agentEntity };

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

void printSimulatedActions( const gie::Planner& planner )
{
	std::vector< gie::Guid > leafSimulationGuids{ };
	auto& simulations = planner.simulations();
	for( auto& simulationItr : simulations )
	{
		if( simulationItr.second.outgoing.empty() )
		{
			leafSimulationGuids.push_back( simulationItr.first );
		}
	}

	for( auto leafSimulationGuid : leafSimulationGuids )
	{
		std::vector< std::string_view > actionNames;
		auto simulationItr = simulations.find( leafSimulationGuid );

		// iterating until root simulation
		while( !simulationItr->second.incoming.empty() )
		{
			auto& simulationActions = simulationItr->second.actions;
			for( auto simulationAction : simulationActions )
			{
				auto actionName = simulationAction->name();
				actionNames.push_back( actionName );
			}
			simulationItr = simulations.find( *simulationItr->second.incoming.begin() );
		}
		
		if( actionNames.empty() )
		{
			continue;
		}

		auto actionNameItr = actionNames.rbegin();
		std::cout << *actionNameItr;
		actionNameItr++;

		while( actionNameItr != actionNames.rend() )
		{
			std::cout << " | " << *actionNameItr;
			actionNameItr++;
		}

		std::cout << std::endl << "==============" << std::endl;
	}
}