#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

#include <goapie_main.h>
#include <persistency.h>

using namespace gie;

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

struct TestResult
{
	const char* suite;
	const char* name;
	bool passed;
	std::string failMessage;
};

static std::vector< TestResult > g_results;
static const char* g_currentSuite = "";
static const char* g_currentTest = "";
static int g_assertionsRun = 0;

#define TEST_SUITE( suiteName ) g_currentSuite = suiteName

static bool _testPassed;
static std::string _testFailMsg;

static void beginTest( const char* name )
{
	g_currentTest = name;
	_testPassed = true;
	_testFailMsg.clear();
}

static void endTest()
{
	g_results.push_back( { g_currentSuite, g_currentTest, _testPassed, _testFailMsg } );
}

#define CHECK( expr ) \
	do { \
		g_assertionsRun++; \
		if( !( expr ) ) { \
			_testPassed = false; \
			_testFailMsg = std::string( "CHECK failed: " ) + #expr + " (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

#define CHECK_EQ( a, b ) \
	do { \
		g_assertionsRun++; \
		if( ( a ) != ( b ) ) { \
			_testPassed = false; \
			_testFailMsg = std::string( "CHECK_EQ failed: " ) + #a + " != " + #b + " (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

#define CHECK_NEQ( a, b ) \
	do { \
		g_assertionsRun++; \
		if( ( a ) == ( b ) ) { \
			_testPassed = false; \
			_testFailMsg = std::string( "CHECK_NEQ failed: " ) + #a + " == " + #b + " (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

#define CHECK_NULL( ptr ) \
	do { \
		g_assertionsRun++; \
		if( ( ptr ) != nullptr ) { \
			_testPassed = false; \
			_testFailMsg = std::string( "CHECK_NULL failed: " ) + #ptr + " is not null (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

#define CHECK_NOT_NULL( ptr ) \
	do { \
		g_assertionsRun++; \
		if( ( ptr ) == nullptr ) { \
			_testPassed = false; \
			_testFailMsg = std::string( "CHECK_NOT_NULL failed: " ) + #ptr + " is null (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

#define CHECK_FLOAT_EQ( a, b ) \
	do { \
		g_assertionsRun++; \
		if( std::fabs( (float)( a ) - (float)( b ) ) > 0.001f ) { \
			_testPassed = false; \
			_testFailMsg = std::string( "CHECK_FLOAT_EQ failed: " ) + #a + " != " + #b + " (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

// ---------------------------------------------------------------------------
// StringRegister tests
// ---------------------------------------------------------------------------

static void testStringRegister()
{
	TEST_SUITE( "StringRegister" );

	beginTest( "add and retrieve string" ); {
		StringHash h = stringRegister().add( "TestString_SR1" );
		CHECK_NEQ( h, InvalidStringHash );
		auto sv = stringRegister().get( h );
		CHECK_EQ( sv, "TestString_SR1" );
	} endTest();

	beginTest( "add same string returns same hash" ); {
		StringHash h1 = stringRegister().add( "TestString_SR2" );
		StringHash h2 = stringRegister().add( "TestString_SR2" );
		CHECK_EQ( h1, h2 );
	} endTest();

	beginTest( "add empty string returns InvalidStringHash" ); {
		StringHash h = stringRegister().add( "" );
		CHECK_EQ( h, InvalidStringHash );
	} endTest();

	beginTest( "get unknown hash returns empty" ); {
		auto sv = stringRegister().get( 99999999 );
		CHECK( sv.empty() );
	} endTest();

	beginTest( "stringHasher registers and returns hash" ); {
		StringHash h = stringHasher( "TestString_SR3" );
		CHECK_NEQ( h, InvalidStringHash );
		CHECK_EQ( stringRegister().get( h ), "TestString_SR3" );
	} endTest();
}

// ---------------------------------------------------------------------------
// Property tests
// ---------------------------------------------------------------------------

static void testProperty()
{
	TEST_SUITE( "Property" );

	beginTest( "bool property" ); {
		Property p( NullGuid, stringHasher( "BoolProp" ) );
		p.value = true;
		CHECK_EQ( p.type(), Property::Boolean );
		CHECK_NOT_NULL( p.getBool() );
		CHECK_EQ( *p.getBool(), true );
		CHECK_NULL( p.getFloat() );
		CHECK_NULL( p.getInteger() );
	} endTest();

	beginTest( "float property" ); {
		Property p( NullGuid, stringHasher( "FloatProp" ) );
		p.value = 3.14f;
		CHECK_EQ( p.type(), Property::Float );
		CHECK_NOT_NULL( p.getFloat() );
		CHECK_FLOAT_EQ( *p.getFloat(), 3.14f );
		CHECK_NULL( p.getBool() );
	} endTest();

	beginTest( "integer property" ); {
		Property p( NullGuid, stringHasher( "IntProp" ) );
		p.value = int32_t( 42 );
		CHECK_EQ( p.type(), Property::Integer );
		CHECK_NOT_NULL( p.getInteger() );
		CHECK_EQ( *p.getInteger(), 42 );
	} endTest();

	beginTest( "guid property" ); {
		Guid testGuid = randGuid();
		Property p( NullGuid, stringHasher( "GuidProp" ) );
		p.value = testGuid;
		CHECK_EQ( p.type(), Property::GUID );
		CHECK_NOT_NULL( p.getGuid() );
		CHECK_EQ( *p.getGuid(), testGuid );
	} endTest();

	beginTest( "vec3 property" ); {
		Property p( NullGuid, stringHasher( "Vec3Prop" ) );
		p.value = glm::vec3( 1.f, 2.f, 3.f );
		CHECK_EQ( p.type(), Property::Vec3 );
		CHECK_NOT_NULL( p.getVec3() );
		CHECK_FLOAT_EQ( p.getVec3()->x, 1.f );
		CHECK_FLOAT_EQ( p.getVec3()->y, 2.f );
		CHECK_FLOAT_EQ( p.getVec3()->z, 3.f );
	} endTest();

	beginTest( "boolean array property" ); {
		Property p( NullGuid, stringHasher( "BoolArrProp" ) );
		p.value = Property::BooleanVector{ true, false, true };
		CHECK_EQ( p.type(), Property::BooleanArray );
		CHECK_NOT_NULL( p.getBooleanArray() );
		CHECK_EQ( p.getBooleanArray()->size(), size_t( 3 ) );
	} endTest();

	beginTest( "float array property" ); {
		Property p( NullGuid, stringHasher( "FloatArrProp" ) );
		p.value = Property::FloatVector{ 1.f, 2.f };
		CHECK_EQ( p.type(), Property::FloatArray );
		CHECK_NOT_NULL( p.getFloatArray() );
		CHECK_EQ( p.getFloatArray()->size(), size_t( 2 ) );
	} endTest();

	beginTest( "integer array property" ); {
		Property p( NullGuid, stringHasher( "IntArrProp" ) );
		p.value = Property::IntegerVector{ 10, 20, 30 };
		CHECK_EQ( p.type(), Property::IntegerArray );
		CHECK_NOT_NULL( p.getIntegerArray() );
		CHECK_EQ( p.getIntegerArray()->size(), size_t( 3 ) );
	} endTest();

	beginTest( "guid array property" ); {
		Property p( NullGuid, stringHasher( "GuidArrProp" ) );
		p.value = Property::GuidVector{ randGuid(), randGuid() };
		CHECK_EQ( p.type(), Property::GUIDArray );
		CHECK_NOT_NULL( p.getGuidArray() );
		CHECK_EQ( p.getGuidArray()->size(), size_t( 2 ) );
	} endTest();

	beginTest( "vec3 array property" ); {
		Property p( NullGuid, stringHasher( "Vec3ArrProp" ) );
		p.value = Property::Vec3Vector{ { 1, 2, 3 }, { 4, 5, 6 } };
		CHECK_EQ( p.type(), Property::Vec3Array );
		CHECK_NOT_NULL( p.getVec3Array() );
		CHECK_EQ( p.getVec3Array()->size(), size_t( 2 ) );
	} endTest();

	beginTest( "toString for various types" ); {
		Property pb( NullGuid, stringHasher( "TsBool" ) );
		pb.value = true;
		CHECK_EQ( pb.toString(), "true" );

		Property pi( NullGuid, stringHasher( "TsInt" ) );
		pi.value = int32_t( 7 );
		CHECK_EQ( pi.toString(), "7" );

		Property pf( NullGuid, stringHasher( "TsFalse" ) );
		pf.value = false;
		CHECK_EQ( pf.toString(), "false" );
	} endTest();

	beginTest( "guid and ownerGuid" ); {
		Guid ownerGuid = randGuid();
		Property p( ownerGuid, stringHasher( "OwnerTest" ) );
		CHECK_NEQ( p.guid(), NullGuid );
		CHECK_EQ( p.ownerGuid(), ownerGuid );
	} endTest();
}

// ---------------------------------------------------------------------------
// Entity tests
// ---------------------------------------------------------------------------

static void testEntity()
{
	TEST_SUITE( "Entity" );

	beginTest( "create entity with name" ); {
		World world;
		auto* e = world.createEntity( "TestEntity" );
		CHECK_NOT_NULL( e );
		CHECK_NEQ( e->guid(), NullGuid );
		CHECK_EQ( e->nameHash(), stringHasher( "TestEntity" ) );
	} endTest();

	beginTest( "create entity without name" ); {
		World world;
		auto* e = world.createEntity();
		CHECK_NOT_NULL( e );
		CHECK_NEQ( e->guid(), NullGuid );
	} endTest();

	beginTest( "entity GUID uniqueness" ); {
		World world;
		auto* e1 = world.createEntity( "E1" );
		auto* e2 = world.createEntity( "E2" );
		CHECK_NOT_NULL( e1 );
		CHECK_NOT_NULL( e2 );
		CHECK_NEQ( e1->guid(), e2->guid() );
	} endTest();

	beginTest( "create and retrieve property by name" ); {
		World world;
		auto* e = world.createEntity( "PropEntity" );
		auto* p = e->createProperty( "Health", int32_t( 100 ) );
		CHECK_NOT_NULL( p );
		CHECK_EQ( *p->getInteger(), 100 );

		auto* pGet = e->property( "Health" );
		CHECK_NOT_NULL( pGet );
		CHECK_EQ( pGet->guid(), p->guid() );
	} endTest();

	beginTest( "retrieve nonexistent property returns null" ); {
		World world;
		auto* e = world.createEntity( "NoPropEntity" );
		CHECK_NULL( e->property( "DoesNotExist" ) );
	} endTest();

	beginTest( "create duplicate property returns existing" ); {
		World world;
		auto* e = world.createEntity( "DupPropEntity" );
		auto* p1 = e->createProperty( "Dup", int32_t( 1 ) );
		auto* p2 = e->createProperty( "Dup" );
		CHECK_EQ( p1->guid(), p2->guid() );
	} endTest();

	beginTest( "setName updates name hash" ); {
		World world;
		auto* e = world.createEntity( "OldName" );
		CHECK_EQ( e->nameHash(), stringHasher( "OldName" ) );
		e->setName( "NewName" );
		CHECK_EQ( e->nameHash(), stringHasher( "NewName" ) );
	} endTest();

	beginTest( "entity tags" ); {
		World world;
		auto* e = world.createEntity( "TaggedEntity" );
		Tag t = stringHasher( "TestTag_E" );
		e->tags().insert( t );
		CHECK( e->hasTag( t ) );
		CHECK( !e->hasTag( stringHasher( "OtherTag" ) ) );
		CHECK( isTagged( e, t ) );
	} endTest();

	beginTest( "removeProperty" ); {
		World world;
		auto* e = world.createEntity( "RemPropEntity" );
		e->createProperty( "ToRemove", true );
		CHECK_NOT_NULL( e->property( "ToRemove" ) );
		e->removeProperty( stringHasher( "ToRemove" ) );
		CHECK_NULL( e->property( "ToRemove" ) );
	} endTest();

	beginTest( "entity lookup by guid" ); {
		World world;
		auto* e = world.createEntity( "LookupEntity" );
		auto* found = world.entity( e->guid() );
		CHECK_NOT_NULL( found );
		CHECK_EQ( found->guid(), e->guid() );
	} endTest();

	beginTest( "entity lookup with NullGuid returns null" ); {
		World world;
		CHECK_NULL( world.entity( NullGuid ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// Agent tests
// ---------------------------------------------------------------------------

static void testAgent()
{
	TEST_SUITE( "Agent" );

	beginTest( "create agent" ); {
		World world;
		auto* a = world.createAgent( "TestAgent" );
		CHECK_NOT_NULL( a );
		CHECK_NEQ( a->guid(), NullGuid );
		CHECK_EQ( a->nameHash(), stringHasher( "TestAgent" ) );
	} endTest();

	beginTest( "agent has opinions blackboard" ); {
		World world;
		auto* a = world.createAgent( "OpinionAgent" );
		// opinions blackboard exists and has world context as parent
		CHECK_NOT_NULL( &a->opinions() );
		CHECK_NOT_NULL( a->opinions().world() );
	} endTest();

	beginTest( "agent inherits entity features" ); {
		World world;
		auto* a = world.createAgent( "AgentProps" );
		auto* p = a->createProperty( "Stamina", int32_t( 50 ) );
		CHECK_NOT_NULL( p );
		CHECK_EQ( *a->property( "Stamina" )->getInteger(), 50 );
	} endTest();

	beginTest( "agent lookup via world" ); {
		World world;
		auto* a = world.createAgent( "AgentLookup" );
		auto* found = world.agent( a->guid() );
		CHECK_NOT_NULL( found );
		CHECK_EQ( found->guid(), a->guid() );
	} endTest();
}

// ---------------------------------------------------------------------------
// Blackboard tests
// ---------------------------------------------------------------------------

static void testBlackboard()
{
	TEST_SUITE( "Blackboard" );

	beginTest( "parent chain property access copies on write" ); {
		World world;
		auto* e = world.createEntity( "BBParentEntity" );
		auto* p = e->createProperty( "Val", int32_t( 10 ) );

		// child blackboard with world context as parent
		Blackboard child( &world, &world.context() );
		// accessing property through child should copy from parent
		auto* childP = child.property( p->guid() );
		CHECK_NOT_NULL( childP );
		CHECK_EQ( *childP->getInteger(), 10 );

		// modifying child should not affect parent
		childP->value = int32_t( 20 );
		CHECK_EQ( *world.property( p->guid() )->getInteger(), 10 );
		CHECK_EQ( *child.property( p->guid() )->getInteger(), 20 );
	} endTest();

	beginTest( "parent chain entity access copies on mutable access" ); {
		World world;
		auto* e = world.createEntity( "BBEntityCopy" );
		e->createProperty( "Marker", true );

		Blackboard child( &world, &world.context() );
		CHECK_EQ( child.entities().size(), size_t( 0 ) );

		// mutable access copies entity to child
		auto* childE = child.entity( e->guid() );
		CHECK_NOT_NULL( childE );
		CHECK_EQ( child.entities().size(), size_t( 1 ) );
	} endTest();

	beginTest( "const entity access does not copy" ); {
		World world;
		auto* e = world.createEntity( "BBConstEntity" );

		Blackboard child( &world, &world.context() );
		const Blackboard& constChild = child;
		auto* found = constChild.entity( e->guid() );
		CHECK_NOT_NULL( found );
		CHECK_EQ( child.entities().size(), size_t( 0 ) );
	} endTest();

	beginTest( "eraseAll clears entities" ); {
		World world;
		world.createEntity( "E1" );
		world.createEntity( "E2" );
		CHECK_EQ( world.context().entities().size(), size_t( 2 ) );
		world.eraseAll();
		CHECK_EQ( world.context().entities().size(), size_t( 0 ) );
	} endTest();

	beginTest( "propertyNameHashes accumulative" ); {
		World world;
		auto* e = world.createEntity( "HashEntity" );
		e->createProperty( "PropA_PNH", true );
		e->createProperty( "PropB_PNH", int32_t( 1 ) );

		Blackboard child( &world, &world.context() );
		// create entity in child with child as context so property goes into child blackboard
		auto* childE = child.createEntity( "ChildEntity", &child );
		childE->createProperty( "PropC_PNH", 2.f );

		auto localHashes = child.propertyNameHashes( false );
		CHECK_EQ( localHashes.size(), size_t( 1 ) );

		auto allHashes = child.propertyNameHashes( true );
		CHECK( allHashes.size() >= size_t( 3 ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// EntityTagRegister tests
// ---------------------------------------------------------------------------

static void testEntityTagRegister()
{
	TEST_SUITE( "EntityTagRegister" );

	beginTest( "tag and query entities" ); {
		World world;
		auto* e1 = world.createEntity( "Tagged1" );
		auto* e2 = world.createEntity( "Tagged2" );
		Tag enemyTag = stringHasher( "Enemy_ETR" );
		std::vector< Tag > tags{ enemyTag };
		world.context().entityTagRegister().tag( e1, tags );
		world.context().entityTagRegister().tag( e2, tags );

		auto* set = world.context().entityTagRegister().tagSet( enemyTag );
		CHECK_NOT_NULL( set );
		CHECK_EQ( set->size(), size_t( 2 ) );
		CHECK( set->count( e1->guid() ) == 1 );
		CHECK( set->count( e2->guid() ) == 1 );
	} endTest();

	beginTest( "untag removes entity from tag set" ); {
		World world;
		auto* e = world.createEntity( "UntagMe" );
		Tag t = stringHasher( "Removable_ETR" );
		std::vector< Tag > tags{ t };
		world.context().entityTagRegister().tag( e, tags );
		CHECK( e->hasTag( t ) );

		world.context().entityTagRegister().untag( e, tags );
		CHECK( !e->hasTag( t ) );
	} endTest();

	beginTest( "query nonexistent tag returns null" ); {
		World world;
		CHECK_NULL( world.context().entityTagRegister().tagSet( stringHasher( "NeverUsed_ETR" ) ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// Archetype tests
// ---------------------------------------------------------------------------

static void testArchetype()
{
	TEST_SUITE( "Archetype" );

	beginTest( "create archetype" ); {
		World world;
		auto* arch = world.createArchetype( "Soldier" );
		CHECK_NOT_NULL( arch );
		CHECK_NEQ( arch->guid(), NullGuid );
		CHECK_EQ( arch->nameHash(), stringHasher( "Soldier" ) );
	} endTest();

	beginTest( "archetype with tags and properties" ); {
		World world;
		auto* arch = world.createArchetype( "Goblin" );
		arch->addTag( "Monster" );
		arch->addProperty( "Health", int32_t( 30 ) );
		arch->addProperty( "Hostile", true );

		CHECK_EQ( arch->tags().size(), size_t( 1 ) );
		CHECK_EQ( arch->properties().size(), size_t( 2 ) );
	} endTest();

	beginTest( "instantiate archetype creates entity with defaults" ); {
		World world;
		auto* arch = world.createArchetype( "Orc" );
		arch->addTag( "Enemy_Arch" );
		arch->addProperty( "Strength", int32_t( 50 ) );
		arch->addProperty( "Alive", true );

		auto* e = arch->instantiate( world );
		CHECK_NOT_NULL( e );
		CHECK_EQ( e->nameHash(), stringHasher( "Orc" ) );
		CHECK( e->hasTag( stringHasher( "Enemy_Arch" ) ) );

		auto* strengthPpt = e->property( "Strength" );
		CHECK_NOT_NULL( strengthPpt );
		CHECK_EQ( *strengthPpt->getInteger(), 50 );

		auto* alivePpt = e->property( "Alive" );
		CHECK_NOT_NULL( alivePpt );
		CHECK_EQ( *alivePpt->getBool(), true );
	} endTest();

	beginTest( "instantiate with name override" ); {
		World world;
		auto* arch = world.createArchetype( "Villager" );
		auto* e = arch->instantiate( world, "Bob" );
		CHECK_NOT_NULL( e );
		CHECK_EQ( e->nameHash(), stringHasher( "Bob" ) );
	} endTest();

	beginTest( "remove archetype" ); {
		World world;
		auto* arch = world.createArchetype( "Temp" );
		Guid id = arch->guid();
		CHECK_NOT_NULL( world.archetype( id ) );
		world.removeArchetype( id );
		CHECK_NULL( world.archetype( id ) );
	} endTest();

	beginTest( "setName updates archetype" ); {
		World world;
		auto* arch = world.createArchetype( "OldArch" );
		arch->setName( "NewArch" );
		CHECK_EQ( arch->nameHash(), stringHasher( "NewArch" ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// NamedArguments tests
// ---------------------------------------------------------------------------

static void testNamedArguments()
{
	TEST_SUITE( "NamedArguments" );

	beginTest( "add and get argument" ); {
		NamedArguments args;
		CHECK( args.empty() );
		args.add( "Speed", 5.f );
		CHECK( !args.empty() );
		auto* val = args.get( "Speed" );
		CHECK_NOT_NULL( val );
		CHECK_FLOAT_EQ( std::get< float >( *val ), 5.f );
	} endTest();

	beginTest( "get nonexistent returns null" ); {
		NamedArguments args;
		CHECK_NULL( args.get( "Nothing" ) );
	} endTest();

	beginTest( "add overwrites existing" ); {
		NamedArguments args;
		args.add( "Count", int32_t( 1 ) );
		args.add( "Count", int32_t( 2 ) );
		CHECK_EQ( std::get< int32_t >( *args.get( "Count" ) ), 2 );
	} endTest();

	beginTest( "type checksum changes with arguments" ); {
		NamedArguments args1;
		auto type0 = args1.type();
		args1.add( "X", true );
		CHECK_NEQ( args1.type(), type0 );
	} endTest();

	beginTest( "add vector of arguments" ); {
		NamedArguments args;
		args.add( {
			{ stringHasher( "A_NA" ), true },
			{ stringHasher( "B_NA" ), int32_t( 2 ) }
		} );
		CHECK_NOT_NULL( args.get( "A_NA" ) );
		CHECK_NOT_NULL( args.get( "B_NA" ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// Goal tests
// ---------------------------------------------------------------------------

static void testGoal()
{
	TEST_SUITE( "Goal" );

	beginTest( "goal reached when targets match" ); {
		World world;
		auto* e = world.createEntity( "GoalEntity" );
		auto* p = e->createProperty( "Done", false );

		Goal goal( world );
		goal.targets.emplace_back( p->guid(), true );

		auto* agent = world.createAgent( "GoalAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );

		// property is false, goal not reached
		CHECK( !goal.reached( sim ) );

		// set property to true in simulation
		sim.context().property( p->guid() )->value = true;
		CHECK( goal.reached( sim ) );
	} endTest();

	beginTest( "goal reached with multiple targets" ); {
		World world;
		auto* e = world.createEntity( "MultiGoalEntity" );
		auto* p1 = e->createProperty( "A_Goal", false );
		auto* p2 = e->createProperty( "B_Goal", false );

		Goal goal( world );
		goal.targets.emplace_back( p1->guid(), true );
		goal.targets.emplace_back( p2->guid(), true );

		auto* agent = world.createAgent( "MGoalAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );

		// only one target met
		sim.context().property( p1->guid() )->value = true;
		CHECK( !goal.reached( sim ) );

		// both targets met
		sim.context().property( p2->guid() )->value = true;
		CHECK( goal.reached( sim ) );
	} endTest();

	beginTest( "empty goal is reached immediately" ); {
		World world;
		Goal goal( world );
		auto* agent = world.createAgent( "EmptyGoalAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );
		CHECK( goal.reached( sim ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// Simulation tests
// ---------------------------------------------------------------------------

static void testSimulation()
{
	TEST_SUITE( "Simulation" );

	beginTest( "simulation has unique guid" ); {
		World world;
		auto* agent = world.createAgent( "SimAgent" );
		SimAgent simAgent( agent );
		Guid g1 = randGuid();
		Guid g2 = randGuid();
		Simulation s1( g1, &world, &world.context(), simAgent );
		Simulation s2( g2, &world, &world.context(), simAgent );
		CHECK_NEQ( s1.guid(), s2.guid() );
	} endTest();

	beginTest( "simulation context inherits world data" ); {
		World world;
		auto* e = world.createEntity( "SimContextEntity" );
		e->createProperty( "Status", int32_t( 1 ) );

		auto* agent = world.createAgent( "SimCtxAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );

		// entity accessible through simulation context
		const auto* constCtx = &sim.context();
		auto* found = constCtx->entity( e->guid() );
		CHECK_NOT_NULL( found );
	} endTest();

	beginTest( "simulation context isolation" ); {
		World world;
		auto* e = world.createEntity( "IsoEntity" );
		auto* p = e->createProperty( "Val_Sim", int32_t( 10 ) );

		auto* agent = world.createAgent( "IsoAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );

		// modify in simulation
		sim.context().property( p->guid() )->value = int32_t( 99 );

		// world unchanged
		CHECK_EQ( *world.property( p->guid() )->getInteger(), 10 );
	} endTest();

	beginTest( "simulation defaults" ); {
		World world;
		auto* agent = world.createAgent( "DefAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );
		CHECK_EQ( sim.cost, MaxCost );
		CHECK_EQ( sim.depth, size_t( 0 ) );
		CHECK( sim.incoming.empty() );
		CHECK( sim.outgoing.empty() );
		CHECK( sim.actions.empty() );
	} endTest();

	beginTest( "simulation f-score comparison" ); {
		World world;
		auto* agent = world.createAgent( "CmpAgent" );
		SimAgent simAgent( agent );
		Simulation s1( randGuid(), &world, &world.context(), simAgent );
		Simulation s2( randGuid(), &world, &world.context(), simAgent );

		s1.cost = 5.f; s1.heuristic.value = 3.f; // f = 8
		s2.cost = 4.f; s2.heuristic.value = 6.f; // f = 10
		CHECK( s1 < s2 );
		CHECK( Simulation::smallerThan( &s1, &s2 ) );
	} endTest();

	beginTest( "simulation tagSet queries world context" ); {
		World world;
		auto* e = world.createEntity( "TagSimEntity" );
		Tag t = stringHasher( "SimTag" );
		std::vector< Tag > tags{ t };
		world.context().entityTagRegister().tag( e, tags );

		auto* agent = world.createAgent( "TagSimAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );

		auto* set = sim.tagSet( t );
		CHECK_NOT_NULL( set );
		CHECK( set->count( e->guid() ) == 1 );
	} endTest();
}

// ---------------------------------------------------------------------------
// Planner tests
// ---------------------------------------------------------------------------

// Simple action: sets a bool property to true
class SetBoolAction : public Action
{
public:
	using Action::Action;
	StringHash hash() const override { return stringHasher( "SetBool" ); }
};

class SetBoolSimulator : public ActionSimulator
{
	Guid _targetPropGuid;

public:
	SetBoolSimulator( const NamedArguments& args, Guid propGuid )
		: ActionSimulator( args ), _targetPropGuid( propGuid ) {}

	StringHash hash() const override { return stringHasher( "SetBool" ); }

	bool evaluate( EvaluateSimulationParams params ) const override
	{
		auto* p = params.simulation.context().property( _targetPropGuid );
		return p && p->getBool() && *p->getBool() == false;
	}

	bool simulate( SimulateSimulationParams params ) const override
	{
		params.simulation.context().property( _targetPropGuid )->value = true;
		params.simulation.cost = 1.f;
		params.simulation.actions.emplace_back( std::make_shared< SetBoolAction >() );
		return true;
	}
};

// A two-step action: sets ValA=true, cost=5
class StepAAction : public Action
{
public:
	using Action::Action;
	StringHash hash() const override { return stringHasher( "StepA" ); }
};

class StepASimulator : public ActionSimulator
{
	Guid _propA;

public:
	StepASimulator( const NamedArguments& args, Guid propA )
		: ActionSimulator( args ), _propA( propA ) {}

	StringHash hash() const override { return stringHasher( "StepA" ); }

	bool evaluate( EvaluateSimulationParams params ) const override
	{
		auto* p = params.simulation.context().property( _propA );
		return p && p->getBool() && *p->getBool() == false;
	}

	bool simulate( SimulateSimulationParams params ) const override
	{
		params.simulation.context().property( _propA )->value = true;
		params.simulation.cost = 5.f;
		params.simulation.actions.emplace_back( std::make_shared< StepAAction >() );
		return true;
	}
};

// Sets ValB=true but requires ValA=true first, cost=3
class StepBAction : public Action
{
public:
	using Action::Action;
	StringHash hash() const override { return stringHasher( "StepB" ); }
};

class StepBSimulator : public ActionSimulator
{
	Guid _propA, _propB;

public:
	StepBSimulator( const NamedArguments& args, Guid propA, Guid propB )
		: ActionSimulator( args ), _propA( propA ), _propB( propB ) {}

	StringHash hash() const override { return stringHasher( "StepB" ); }

	bool evaluate( EvaluateSimulationParams params ) const override
	{
		auto* pa = params.simulation.context().property( _propA );
		auto* pb = params.simulation.context().property( _propB );
		return pa && pa->getBool() && *pa->getBool() == true
			&& pb && pb->getBool() && *pb->getBool() == false;
	}

	bool simulate( SimulateSimulationParams params ) const override
	{
		params.simulation.context().property( _propB )->value = true;
		params.simulation.cost = 3.f;
		params.simulation.actions.emplace_back( std::make_shared< StepBAction >() );
		return true;
	}
};

// Custom ActionSetEntry wrappers
class SetBoolActionSetEntry : public ActionSetEntry
{
	Guid _propGuid;

public:
	SetBoolActionSetEntry( Guid propGuid ) : _propGuid( propGuid ) {}
	std::string_view name() const override { return "SetBool"; }
	StringHash hash() const override { return stringHasher( "SetBool" ); }
	std::shared_ptr< ActionSimulator > simulator( const NamedArguments& args ) const override
	{
		return std::make_shared< SetBoolSimulator >( args, _propGuid );
	}
};

class StepAActionSetEntry : public ActionSetEntry
{
	Guid _propA;

public:
	StepAActionSetEntry( Guid propA ) : _propA( propA ) {}
	std::string_view name() const override { return "StepA"; }
	StringHash hash() const override { return stringHasher( "StepA" ); }
	std::shared_ptr< ActionSimulator > simulator( const NamedArguments& args ) const override
	{
		return std::make_shared< StepASimulator >( args, _propA );
	}
};

class StepBActionSetEntry : public ActionSetEntry
{
	Guid _propA, _propB;

public:
	StepBActionSetEntry( Guid propA, Guid propB ) : _propA( propA ), _propB( propB ) {}
	std::string_view name() const override { return "StepB"; }
	StringHash hash() const override { return stringHasher( "StepB" ); }
	std::shared_ptr< ActionSimulator > simulator( const NamedArguments& args ) const override
	{
		return std::make_shared< StepBSimulator >( args, _propA, _propB );
	}
};

static void testPlanner()
{
	TEST_SUITE( "Planner" );

	beginTest( "single-step plan (BFS)" ); {
		World world;
		auto* e = world.createEntity( "PlannerEntity" );
		auto* p = e->createProperty( "Flag_P", false );
		auto* agent = world.createAgent( "PlannerAgent" );

		Goal goal( world );
		goal.targets.emplace_back( p->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.actionSet().emplace_back( std::make_shared< SetBoolActionSetEntry >( p->guid() ) );
		planner.plan( false );

		CHECK( !planner.planActions().empty() );
		CHECK( planner.simulations().size() > size_t( 1 ) );
	} endTest();

	beginTest( "two-step sequential plan (BFS)" ); {
		World world;
		auto* e = world.createEntity( "TwoStepEntity" );
		auto* pA = e->createProperty( "ValA_P", false );
		auto* pB = e->createProperty( "ValB_P", false );
		auto* agent = world.createAgent( "TwoStepAgent" );

		Goal goal( world );
		goal.targets.emplace_back( pB->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.actionSet().emplace_back( std::make_shared< StepAActionSetEntry >( pA->guid() ) );
		planner.actionSet().emplace_back( std::make_shared< StepBActionSetEntry >( pA->guid(), pB->guid() ) );
		planner.plan( false );

		CHECK( !planner.planActions().empty() );
		// plan must have at least 2 simulations beyond root (StepA then StepB)
		CHECK( planner.simulations().size() >= size_t( 3 ) );
	} endTest();

	beginTest( "heuristic A* mode produces plan" ); {
		World world;
		auto* e = world.createEntity( "AStarEntity" );
		auto* p = e->createProperty( "Flag_AStar", false );
		auto* agent = world.createAgent( "AStarAgent" );

		Goal goal( world );
		goal.targets.emplace_back( p->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.actionSet().emplace_back( std::make_shared< SetBoolActionSetEntry >( p->guid() ) );
		planner.plan( true );

		CHECK( !planner.planActions().empty() );
	} endTest();

	beginTest( "depth limit prevents deep plans" ); {
		World world;
		auto* e = world.createEntity( "DepthEntity" );
		auto* pA = e->createProperty( "ValA_D", false );
		auto* pB = e->createProperty( "ValB_D", false );
		auto* agent = world.createAgent( "DepthAgent" );

		Goal goal( world );
		goal.targets.emplace_back( pB->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.depthLimitMutator() = 1; // only 1 level deep
		planner.actionSet().emplace_back( std::make_shared< StepAActionSetEntry >( pA->guid() ) );
		planner.actionSet().emplace_back( std::make_shared< StepBActionSetEntry >( pA->guid(), pB->guid() ) );
		planner.plan( false );

		// cannot reach goal in 1 step (needs 2)
		CHECK( planner.planActions().empty() );
	} endTest();

	beginTest( "no plan when no actions registered" ); {
		World world;
		auto* e = world.createEntity( "NoActEntity" );
		auto* p = e->createProperty( "NoPlan", false );
		auto* agent = world.createAgent( "NoActAgent" );

		Goal goal( world );
		goal.targets.emplace_back( p->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.plan( false );

		CHECK( planner.planActions().empty() );
	} endTest();

	beginTest( "planner isReady and stages" ); {
		World world;
		auto* agent = world.createAgent( "StageAgent" );
		Goal goal( world );
		Planner planner;
		CHECK( planner.isReady() );
		planner.simulate( goal, *agent );
		planner.plan( false );
		CHECK( planner.isReady() );
	} endTest();

	beginTest( "root simulation is set after plan" ); {
		World world;
		auto* agent = world.createAgent( "RootSimAgent" );
		auto* e = world.createEntity( "RootSimEntity" );
		auto* p = e->createProperty( "RFlag", false );

		Goal goal( world );
		goal.targets.emplace_back( p->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.actionSet().emplace_back( std::make_shared< SetBoolActionSetEntry >( p->guid() ) );
		planner.plan( false );

		CHECK_NOT_NULL( planner.rootSimulation() );
		CHECK_EQ( planner.rootSimulation()->depth, size_t( 0 ) );
	} endTest();

	beginTest( "createSimulation builds parent-child links" ); {
		World world;
		auto* agent = world.createAgent( "LinkAgent" );
		Goal goal( world );
		Planner planner;
		planner.simulate( goal, *agent );

		auto [rootGuid, rootSim] = planner.createRootSimulation( agent );
		CHECK_NOT_NULL( rootSim );

		auto [childGuid, childSim] = planner.createSimulation( rootGuid );
		CHECK_NOT_NULL( childSim );
		CHECK_EQ( childSim->depth, size_t( 1 ) );
		CHECK_EQ( childSim->incoming.size(), size_t( 1 ) );
		CHECK_EQ( childSim->incoming.front(), rootGuid );
		CHECK_EQ( rootSim->outgoing.size(), size_t( 1 ) );
		CHECK_EQ( rootSim->outgoing.front(), childGuid );
	} endTest();

	beginTest( "deleteSimulation removes node" ); {
		World world;
		auto* agent = world.createAgent( "DelSimAgent" );
		Goal goal( world );
		Planner planner;
		planner.simulate( goal, *agent );

		auto [guid, sim] = planner.createRootSimulation( agent );
		CHECK_NOT_NULL( planner.simulation( guid ) );
		planner.deleteSimulation( guid );
		CHECK_NULL( planner.simulation( guid ) );
	} endTest();

	beginTest( "logContent is populated when logging enabled" ); {
		World world;
		auto* e = world.createEntity( "LogEntity" );
		auto* p = e->createProperty( "LogFlag", false );
		auto* agent = world.createAgent( "LogAgent" );

		Goal goal( world );
		goal.targets.emplace_back( p->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.logStepsMutator() = true;
		planner.actionSet().emplace_back( std::make_shared< SetBoolActionSetEntry >( p->guid() ) );
		planner.plan( false );

		CHECK( !planner.logContent().empty() );
	} endTest();

	beginTest( "A* accumulates g-cost across depth" ); {
		World world;
		auto* e = world.createEntity( "CostEntity" );
		auto* pA = e->createProperty( "ValA_C", false );
		auto* pB = e->createProperty( "ValB_C", false );
		auto* agent = world.createAgent( "CostAgent" );

		Goal goal( world );
		goal.targets.emplace_back( pB->guid(), true );

		Planner planner;
		planner.simulate( goal, *agent );
		planner.actionSet().emplace_back( std::make_shared< StepAActionSetEntry >( pA->guid() ) );
		planner.actionSet().emplace_back( std::make_shared< StepBActionSetEntry >( pA->guid(), pB->guid() ) );
		planner.plan( true );

		// find the goal simulation (depth 2) and check cost accumulation
		bool foundGoal = false;
		for( auto& [guid, sim] : planner.simulations() )
		{
			if( sim.depth == 2 )
			{
				// cost should be StepA(5) + StepB(3) = 8
				CHECK_FLOAT_EQ( sim.cost, 8.f );
				foundGoal = true;
				break;
			}
		}
		CHECK( foundGoal );
	} endTest();
}

// ---------------------------------------------------------------------------
// Action system tests
// ---------------------------------------------------------------------------

class TestDummyAction : public Action
{
public:
	using Action::Action;
	StringHash hash() const override { return stringHasher( "TestDummy" ); }
};

static void testActionSystem()
{
	TEST_SUITE( "ActionSystem" );

	beginTest( "action class has correct hash and name" ); {
		auto action = std::make_shared< TestDummyAction >();
		CHECK_EQ( action->hash(), stringHasher( "TestDummy" ) );
		CHECK_EQ( action->name(), "TestDummy" );
	} endTest();

	beginTest( "action arguments" ); {
		auto action = std::make_shared< TestDummyAction >();
		action->arguments().add( "Target", int32_t( 42 ) );
		CHECK_NOT_NULL( action->arguments().get( "Target" ) );
		CHECK_EQ( std::get< int32_t >( *action->arguments().get( "Target" ) ), 42 );
	} endTest();

	beginTest( "action default tick returns Done" ); {
		World world;
		auto* agent = world.createAgent( "TickAgent" );
		auto action = std::make_shared< TestDummyAction >();
		auto state = action->tick( *agent );
		CHECK_EQ( state, Action::State::Done );
	} endTest();

	beginTest( "ActionSimulator default evaluate returns false" ); {
		World world;
		auto* agent = world.createAgent( "DefEvalAgent" );
		SimAgent simAgent( agent );
		Simulation sim( randGuid(), &world, &world.context(), simAgent );
		Goal goal( world );

		// use a basic ActionSimulator to test defaults
		class DefaultSimulator : public ActionSimulator
		{
		public:
			using ActionSimulator::ActionSimulator;
		};
		DefaultSimulator ds( NamedArguments{} );
		CHECK( !ds.evaluate( { sim, simAgent, goal } ) );
		CHECK( !ds.simulate( { sim, simAgent, goal } ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// Persistency tests
// ---------------------------------------------------------------------------

static void testPersistency()
{
	TEST_SUITE( "Persistency" );

	beginTest( "JSON parse and dump roundtrip" ); {
		using namespace persistency::json;
		std::string json = "{\"name\":\"test\",\"value\":42,\"flag\":true,\"items\":[1,2,3]}";
		Value v = Value::parse( json );
		CHECK( v.isObject() );
		CHECK_EQ( v.asObject()[ "name" ].asString(), "test" );
		CHECK_FLOAT_EQ( v.asObject()[ "value" ].asNumber(), 42.0 );
		CHECK_EQ( v.asObject()[ "flag" ].asBoolean(), true );
		CHECK( v.asObject()[ "items" ].isArray() );
		CHECK_EQ( v.asObject()[ "items" ].asArray().size(), size_t( 3 ) );

		std::string dumped = v.dump();
		Value v2 = Value::parse( dumped );
		CHECK( v2.isObject() );
		CHECK_EQ( v2.asObject()[ "name" ].asString(), "test" );
	} endTest();

	beginTest( "save and load world roundtrip" ); {
		World world;
		auto* e1 = world.createEntity( "PersistEntity1" );
		e1->createProperty( "Health_Per", int32_t( 100 ) );
		e1->createProperty( "Alive_Per", true );
		e1->createProperty( "Speed_Per", 3.5f );
		e1->createProperty( "Pos_Per", glm::vec3( 1.f, 2.f, 3.f ) );

		auto* e2 = world.createEntity( "PersistEntity2" );
		e2->createProperty( "Name_Per", stringHasher( "Bob_Per" ) );

		// tag e1
		Tag t = stringHasher( "Persist_Tag" );
		std::vector< Tag > tags{ t };
		world.context().entityTagRegister().tag( e1, tags );

		// save
		bool saved = persistency::SaveWorldToJson( world, "test_automated.json" );
		CHECK( saved );

		// load into fresh world
		World world2;
		bool loaded = persistency::LoadWorldFromJson( world2, "test_automated.json" );
		CHECK( loaded );

		// verify entity count
		CHECK_EQ( world2.context().entities().size(), size_t( 2 ) );

		// find entity by iterating (GUIDs are remapped)
		Entity* loadedE1 = nullptr;
		Entity* loadedE2 = nullptr;
		for( auto& [guid, ent] : world2.context().entities() )
		{
			if( ent.nameHash() == stringHasher( "PersistEntity1" ) ) loadedE1 = &ent;
			if( ent.nameHash() == stringHasher( "PersistEntity2" ) ) loadedE2 = &ent;
		}
		CHECK_NOT_NULL( loadedE1 );
		CHECK_NOT_NULL( loadedE2 );

		// verify properties
		auto* hp = loadedE1->property( "Health_Per" );
		CHECK_NOT_NULL( hp );
		CHECK_EQ( *hp->getInteger(), 100 );

		auto* alive = loadedE1->property( "Alive_Per" );
		CHECK_NOT_NULL( alive );
		CHECK_EQ( *alive->getBool(), true );

		auto* spd = loadedE1->property( "Speed_Per" );
		CHECK_NOT_NULL( spd );
		CHECK_FLOAT_EQ( *spd->getFloat(), 3.5f );

		auto* pos = loadedE1->property( "Pos_Per" );
		CHECK_NOT_NULL( pos );
		CHECK_FLOAT_EQ( pos->getVec3()->x, 1.f );
		CHECK_FLOAT_EQ( pos->getVec3()->y, 2.f );
		CHECK_FLOAT_EQ( pos->getVec3()->z, 3.f );

		// verify string hash property
		auto* nameProp = loadedE2->property( "Name_Per" );
		CHECK_NOT_NULL( nameProp );
		CHECK_EQ( stringRegister().get( *nameProp->getGuid() ), "Bob_Per" );

		// verify tag
		CHECK( loadedE1->hasTag( stringHasher( "Persist_Tag" ) ) );
	} endTest();

	beginTest( "load nonexistent file returns false" ); {
		World world;
		bool loaded = persistency::LoadWorldFromJson( world, "does_not_exist_12345.json" );
		CHECK( !loaded );
	} endTest();

	beginTest( "save and load empty world" ); {
		World emptyWorld;
		bool saved = persistency::SaveWorldToJson( emptyWorld, "test_empty.json" );
		CHECK( saved );

		World loaded;
		bool ok = persistency::LoadWorldFromJson( loaded, "test_empty.json" );
		CHECK( ok );
		CHECK_EQ( loaded.context().entities().size(), size_t( 0 ) );
	} endTest();

	beginTest( "array property types roundtrip" ); {
		World world;
		auto* e = world.createEntity( "ArrayPersist" );
		e->createProperty( "Bools_AP", Property::BooleanVector{ true, false, true } );
		e->createProperty( "Floats_AP", Property::FloatVector{ 1.5f, 2.5f } );
		e->createProperty( "Ints_AP", Property::IntegerVector{ 10, 20, 30 } );
		e->createProperty( "Vecs_AP", Property::Vec3Vector{ { 1, 2, 3 }, { 4, 5, 6 } } );

		bool saved = persistency::SaveWorldToJson( world, "test_arrays.json" );
		CHECK( saved );

		World world2;
		bool loaded = persistency::LoadWorldFromJson( world2, "test_arrays.json" );
		CHECK( loaded );

		Entity* le = nullptr;
		for( auto& [guid, ent] : world2.context().entities() )
		{
			if( ent.nameHash() == stringHasher( "ArrayPersist" ) ) le = &ent;
		}
		CHECK_NOT_NULL( le );

		auto* bools = le->property( "Bools_AP" );
		CHECK_NOT_NULL( bools );
		CHECK_EQ( bools->type(), Property::BooleanArray );
		CHECK_EQ( bools->getBooleanArray()->size(), size_t( 3 ) );

		auto* floats = le->property( "Floats_AP" );
		CHECK_NOT_NULL( floats );
		CHECK_EQ( floats->type(), Property::FloatArray );
		CHECK_EQ( floats->getFloatArray()->size(), size_t( 2 ) );

		auto* ints = le->property( "Ints_AP" );
		CHECK_NOT_NULL( ints );
		CHECK_EQ( ints->type(), Property::IntegerArray );
		CHECK_EQ( ints->getIntegerArray()->size(), size_t( 3 ) );

		auto* vecs = le->property( "Vecs_AP" );
		CHECK_NOT_NULL( vecs );
		CHECK_EQ( vecs->type(), Property::Vec3Array );
		CHECK_EQ( vecs->getVec3Array()->size(), size_t( 2 ) );
	} endTest();
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

int RunAutomatedTests()
{
	g_results.clear();
	g_assertionsRun = 0;

	std::printf( "\n" );
	std::printf( "========================================\n" );
	std::printf( "  GOAPie Automated Tests\n" );
	std::printf( "========================================\n\n" );
	std::fflush( stdout );

	testStringRegister();
	testProperty();
	testEntity();
	testAgent();
	testBlackboard();
	testEntityTagRegister();
	testArchetype();
	testNamedArguments();
	testGoal();
	testSimulation();
	testPlanner();
	testActionSystem();
	testPersistency();

	// Print results
	int passed = 0;
	int failed = 0;
	std::string currentSuite;

	for( const auto& r : g_results )
	{
		if( currentSuite != r.suite )
		{
			currentSuite = r.suite;
			std::printf( "[%s]\n", r.suite );
		}

		if( r.passed )
		{
			std::printf( "  PASS  %s\n", r.name );
			passed++;
		}
		else
		{
			std::printf( "  FAIL  %s\n", r.name );
			if( !r.failMessage.empty() )
			{
				std::printf( "        %s\n", r.failMessage.c_str() );
			}
			failed++;
		}
	}

	std::printf( "\n" );
	std::printf( "----------------------------------------\n" );
	std::printf( "  Results: %d passed, %d failed, %d total\n", passed, failed, passed + failed );
	std::printf( "  Assertions: %d\n", g_assertionsRun );
	std::printf( "----------------------------------------\n" );

	if( failed > 0 )
	{
		std::printf( "  SOME TESTS FAILED\n" );
	}
	else
	{
		std::printf( "  ALL TESTS PASSED\n" );
	}

	std::printf( "========================================\n\n" );

	return failed;
}
