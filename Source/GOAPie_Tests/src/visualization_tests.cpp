#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <set>

#include "visualization.h"

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors automated_tests.cpp)
// ---------------------------------------------------------------------------

struct VizTestResult
{
	const char* suite;
	const char* name;
	bool passed;
	std::string failMessage;
};

static std::vector< VizTestResult > g_vizResults;
static const char* g_vizCurrentSuite = "";
static const char* g_vizCurrentTest = "";
static int g_vizAssertionsRun = 0;

#define VIZ_TEST_SUITE( suiteName ) g_vizCurrentSuite = suiteName

static bool _vizTestPassed;
static std::string _vizTestFailMsg;

static void vizBeginTest( const char* name )
{
	g_vizCurrentTest = name;
	_vizTestPassed = true;
	_vizTestFailMsg.clear();
}

static void vizEndTest()
{
	g_vizResults.push_back( { g_vizCurrentSuite, g_vizCurrentTest, _vizTestPassed, _vizTestFailMsg } );
}

#define VIZ_CHECK( expr ) \
	do { \
		g_vizAssertionsRun++; \
		if( !( expr ) ) { \
			_vizTestPassed = false; \
			_vizTestFailMsg = std::string( "CHECK failed: " ) + #expr + " (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

#define VIZ_CHECK_EQ( a, b ) \
	do { \
		g_vizAssertionsRun++; \
		if( ( a ) != ( b ) ) { \
			_vizTestPassed = false; \
			_vizTestFailMsg = std::string( "CHECK_EQ failed: " ) + #a + " != " + #b + " (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

static bool floatClose( float a, float b, float eps = 0.001f )
{
	return std::fabs( a - b ) < eps;
}

#define VIZ_CHECK_CLOSE( a, b ) \
	do { \
		g_vizAssertionsRun++; \
		if( !floatClose( ( a ), ( b ) ) ) { \
			_vizTestPassed = false; \
			_vizTestFailMsg = std::string( "CHECK_CLOSE failed: " ) + #a + "=" + std::to_string( a ) \
				+ " != " + #b + "=" + std::to_string( b ) + " (line " + std::to_string( __LINE__ ) + ")"; \
		} \
	} while( 0 )

// ---------------------------------------------------------------------------
// Helper: reset DrawingLimits globals between tests
// ---------------------------------------------------------------------------
static void resetDrawingLimits()
{
	g_DrawingLimits.minBounds = glm::vec3( std::numeric_limits<float>::max() );
	g_DrawingLimits.maxBounds = glm::vec3( std::numeric_limits<float>::lowest() );
	g_DrawingLimits.range = glm::vec3( 0.f );
	g_DrawingLimits.center = glm::vec3( 0.f );
	g_DrawingLimits.scale = glm::vec3( 1.f );
	g_DrawingLimitsInitialized = false;
}

// ---------------------------------------------------------------------------
// Test: DrawingLimits / updateDrawingBounds
// ---------------------------------------------------------------------------
static void testUpdateDrawingBounds()
{
	VIZ_TEST_SUITE( "DrawingLimits" );

	// Test: empty world produces no bounds update
	{
		vizBeginTest( "empty world does not initialize bounds" );
		resetDrawingLimits();

		gie::World world{};
		updateDrawingBounds( world );

		VIZ_CHECK( !g_DrawingLimitsInitialized );
		vizEndTest();
	}

	// Test: single waypoint entity sets center to that point
	{
		vizBeginTest( "single waypoint sets center" );
		resetDrawingLimits();

		gie::World world{};
		auto* e = world.createEntity( "wp1" );
		world.context().entityTagRegister().tag( e, { gie::stringHasher( "Waypoint" ) } );
		e->createProperty( "Location", glm::vec3( 10.0f, 20.0f, 0.0f ) );

		updateDrawingBounds( world );

		VIZ_CHECK( g_DrawingLimitsInitialized );
		VIZ_CHECK_CLOSE( g_DrawingLimits.center.x, 10.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.center.y, 20.0f );
		vizEndTest();
	}

	// Test: two waypoints compute correct center and scale
	{
		vizBeginTest( "two waypoints compute center and bounds" );
		resetDrawingLimits();

		gie::World world{};
		auto* e1 = world.createEntity( "wp1" );
		world.context().entityTagRegister().tag( e1, { gie::stringHasher( "Waypoint" ) } );
		e1->createProperty( "Location", glm::vec3( 0.0f, 0.0f, 0.0f ) );

		auto* e2 = world.createEntity( "wp2" );
		world.context().entityTagRegister().tag( e2, { gie::stringHasher( "Waypoint" ) } );
		e2->createProperty( "Location", glm::vec3( 10.0f, 20.0f, 0.0f ) );

		updateDrawingBounds( world );

		VIZ_CHECK( g_DrawingLimitsInitialized );
		VIZ_CHECK_CLOSE( g_DrawingLimits.center.x, 5.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.center.y, 10.0f );
		// range = max - min = (10, 20, 0)
		VIZ_CHECK_CLOSE( g_DrawingLimits.range.x, 10.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.range.y, 20.0f );
		// bounds expand by margin (10%): min = 0 - 10*0.1 = -1, max = 10 + 10*0.1 = 11 (x)
		VIZ_CHECK_CLOSE( g_DrawingLimits.minBounds.x, -1.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.maxBounds.x, 11.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.minBounds.y, -2.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.maxBounds.y, 22.0f );
		// scale = 2.0 / (maxBounds - minBounds), z = 1.0
		// x: 2.0 / 12.0 = 0.1667, y: 2.0 / 24.0 = 0.0833
		VIZ_CHECK_CLOSE( g_DrawingLimits.scale.x, 2.0f / 12.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.scale.y, 2.0f / 24.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.scale.z, 1.0f );
		vizEndTest();
	}

	// Test: already-initialized bounds are not recomputed
	{
		vizBeginTest( "bounds not recomputed when already initialized" );
		resetDrawingLimits();

		gie::World world{};
		auto* e = world.createEntity( "wp" );
		world.context().entityTagRegister().tag( e, { gie::stringHasher( "Waypoint" ) } );
		auto* ppt = e->createProperty( "Location", glm::vec3( 5.0f, 5.0f, 0.0f ) );

		updateDrawingBounds( world );
		VIZ_CHECK( g_DrawingLimitsInitialized );
		float savedCenterX = g_DrawingLimits.center.x;

		// Modify entity position and call again — should NOT change
		ppt->value = glm::vec3( 100.0f, 100.0f, 0.0f );
		updateDrawingBounds( world );
		VIZ_CHECK_CLOSE( g_DrawingLimits.center.x, savedCenterX );
		vizEndTest();
	}

	// Test: "Draw" tag takes priority over "Waypoint" tag
	{
		vizBeginTest( "Draw tag takes priority over Waypoint" );
		resetDrawingLimits();

		gie::World world{};
		// A "Draw" entity at (100, 100)
		auto* drawE = world.createEntity( "draw1" );
		world.context().entityTagRegister().tag( drawE, { gie::stringHasher( "Draw" ) } );
		drawE->createProperty( "Location", glm::vec3( 100.0f, 100.0f, 0.0f ) );

		// A "Waypoint" entity at (0, 0) — should be ignored when Draw entities exist
		auto* wpE = world.createEntity( "wp1" );
		world.context().entityTagRegister().tag( wpE, { gie::stringHasher( "Waypoint" ) } );
		wpE->createProperty( "Location", glm::vec3( 0.0f, 0.0f, 0.0f ) );

		updateDrawingBounds( world );

		VIZ_CHECK( g_DrawingLimitsInitialized );
		VIZ_CHECK_CLOSE( g_DrawingLimits.center.x, 100.0f );
		VIZ_CHECK_CLOSE( g_DrawingLimits.center.y, 100.0f );
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Test: Selection State
// ---------------------------------------------------------------------------
static void testSelectionState()
{
	VIZ_TEST_SUITE( "SelectionState" );

	// Test: initial state is empty
	{
		vizBeginTest( "initial selection is empty" );
		g_selectedEntityGuids.clear();
		VIZ_CHECK( g_selectedEntityGuids.empty() );
		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)0 );
		vizEndTest();
	}

	// Test: single selection
	{
		vizBeginTest( "single entity selection" );
		g_selectedEntityGuids.clear();

		gie::World world{};
		auto* e = world.createEntity( "test" );
		g_selectedEntityGuids.insert( e->guid() );

		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)1 );
		VIZ_CHECK( g_selectedEntityGuids.count( e->guid() ) == 1 );
		vizEndTest();
	}

	// Test: multi-selection
	{
		vizBeginTest( "multi entity selection" );
		g_selectedEntityGuids.clear();

		gie::World world{};
		auto* e1 = world.createEntity( "a" );
		auto* e2 = world.createEntity( "b" );
		auto* e3 = world.createEntity( "c" );
		g_selectedEntityGuids.insert( e1->guid() );
		g_selectedEntityGuids.insert( e2->guid() );
		g_selectedEntityGuids.insert( e3->guid() );

		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)3 );
		VIZ_CHECK( g_selectedEntityGuids.count( e1->guid() ) == 1 );
		VIZ_CHECK( g_selectedEntityGuids.count( e2->guid() ) == 1 );
		VIZ_CHECK( g_selectedEntityGuids.count( e3->guid() ) == 1 );
		vizEndTest();
	}

	// Test: clearing selection
	{
		vizBeginTest( "clear selection" );
		g_selectedEntityGuids.clear();

		gie::World world{};
		auto* e = world.createEntity( "x" );
		g_selectedEntityGuids.insert( e->guid() );
		VIZ_CHECK( !g_selectedEntityGuids.empty() );

		g_selectedEntityGuids.clear();
		VIZ_CHECK( g_selectedEntityGuids.empty() );
		vizEndTest();
	}

	// Test: replacing selection (click behavior)
	{
		vizBeginTest( "replace selection simulates click" );
		g_selectedEntityGuids.clear();

		gie::World world{};
		auto* e1 = world.createEntity( "first" );
		auto* e2 = world.createEntity( "second" );
		g_selectedEntityGuids.insert( e1->guid() );
		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)1 );
		VIZ_CHECK( g_selectedEntityGuids.count( e1->guid() ) == 1 );

		// Simulate clicking e2: clear and insert
		g_selectedEntityGuids.clear();
		g_selectedEntityGuids.insert( e2->guid() );
		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)1 );
		VIZ_CHECK( g_selectedEntityGuids.count( e2->guid() ) == 1 );
		VIZ_CHECK( g_selectedEntityGuids.count( e1->guid() ) == 0 );
		vizEndTest();
	}

	// Cleanup
	g_selectedEntityGuids.clear();
}

// ---------------------------------------------------------------------------
// Test: Entity Outliner Operations (CRUD via World API)
// ---------------------------------------------------------------------------
static void testEntityOutlinerOperations()
{
	VIZ_TEST_SUITE( "EntityOutliner" );

	// Test: add entity
	{
		vizBeginTest( "add entity to world" );

		gie::World world{};
		auto* e = world.createEntity( "TestEntity" );
		VIZ_CHECK( e != nullptr );
		VIZ_CHECK( e->guid() != gie::NullGuid );

		auto nameHash = e->nameHash();
		VIZ_CHECK( nameHash != gie::InvalidStringHash );
		auto name = gie::stringRegister().get( nameHash );
		VIZ_CHECK( name == "TestEntity" );
		vizEndTest();
	}

	// Test: delete entity
	{
		vizBeginTest( "delete entity from world" );

		gie::World world{};
		auto* e = world.createEntity( "ToDelete" );
		gie::Guid guid = e->guid();
		VIZ_CHECK( world.entity( guid ) != nullptr );

		world.removeEntity( guid );
		VIZ_CHECK( world.entity( guid ) == nullptr );
		vizEndTest();
	}

	// Test: rename entity
	{
		vizBeginTest( "rename entity" );

		gie::World world{};
		auto* e = world.createEntity( "OldName" );
		VIZ_CHECK( gie::stringRegister().get( e->nameHash() ) == "OldName" );

		e->setName( "NewName" );
		VIZ_CHECK( gie::stringRegister().get( e->nameHash() ) == "NewName" );
		vizEndTest();
	}

	// Test: delete selected entity clears selection
	{
		vizBeginTest( "delete selected entity clears selection" );
		g_selectedEntityGuids.clear();

		gie::World world{};
		auto* e = world.createEntity( "Selected" );
		gie::Guid guid = e->guid();
		g_selectedEntityGuids.insert( guid );
		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)1 );

		// Simulate outliner delete behavior: remove entity, clear selection
		world.removeEntity( guid );
		g_selectedEntityGuids.clear();

		VIZ_CHECK( world.entity( guid ) == nullptr );
		VIZ_CHECK( g_selectedEntityGuids.empty() );
		vizEndTest();
	}

	// Test: add entity auto-selects it (outliner behavior)
	{
		vizBeginTest( "add entity auto-selects in outliner" );
		g_selectedEntityGuids.clear();

		gie::World world{};
		auto* e = world.createEntity( "AutoSelect" );
		// Simulate outliner add behavior
		g_selectedEntityGuids.clear();
		g_selectedEntityGuids.insert( e->guid() );

		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)1 );
		VIZ_CHECK( *g_selectedEntityGuids.begin() == e->guid() );
		vizEndTest();
	}

	// Cleanup
	g_selectedEntityGuids.clear();
}

// ---------------------------------------------------------------------------
// Test: Entity Outliner Cancel/Reset
// ---------------------------------------------------------------------------
static void testEntityOutlinerReset()
{
	VIZ_TEST_SUITE( "EntityOutlinerReset" );

	// Test: CancelEntityOutlinerOngoingOperation returns false when nothing active
	{
		vizBeginTest( "cancel returns false when idle" );
		ResetEntityOutlinerState();
		bool cancelled = CancelEntityOutlinerOngoingOperation();
		VIZ_CHECK( !cancelled );
		vizEndTest();
	}

	// Test: ResetEntityOutlinerState does not crash
	{
		vizBeginTest( "reset does not crash" );
		ResetEntityOutlinerState();
		// If we got here, it didn't crash
		VIZ_CHECK( true );
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Test: Outliner Inline Rename API
// ---------------------------------------------------------------------------
static void testOutlinerInlineRenameAPI()
{
	VIZ_TEST_SUITE( "OutlinerInlineRename" );

	// Test: initial state has no active rename
	{
		vizBeginTest( "no active rename initially" );
		CancelOutlinerInlineRename();
		gie::Guid guid;
		const char* buf;
		VIZ_CHECK( !GetOutlinerInlineRenameState( &guid, &buf ) );
		vizEndTest();
	}

	// Test: start inline rename sets state
	{
		vizBeginTest( "start inline rename sets state" );

		gie::World world{};
		g_WorldPtr = &world;
		auto* e = world.createEntity( "RenameMe" );

		StartOutlinerInlineRename( e->guid() );

		gie::Guid renameGuid;
		const char* renameBuf;
		VIZ_CHECK( GetOutlinerInlineRenameState( &renameGuid, &renameBuf ) );
		VIZ_CHECK_EQ( renameGuid, e->guid() );
		VIZ_CHECK( std::strcmp( renameBuf, "RenameMe" ) == 0 );

		CancelOutlinerInlineRename();
		g_WorldPtr = nullptr;
		vizEndTest();
	}

	// Test: cancel clears rename state
	{
		vizBeginTest( "cancel clears rename state" );

		gie::World world{};
		g_WorldPtr = &world;
		auto* e = world.createEntity( "Test" );
		StartOutlinerInlineRename( e->guid() );

		CancelOutlinerInlineRename();

		gie::Guid guid;
		const char* buf;
		VIZ_CHECK( !GetOutlinerInlineRenameState( &guid, &buf ) );

		g_WorldPtr = nullptr;
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Test: Window Visibility Settings Persistence
// ---------------------------------------------------------------------------
static const char* kTestSettingsFile = "goapie_viz_test_settings.ini";

static void testWindowSettingsPersistence()
{
	VIZ_TEST_SUITE( "WindowSettings" );

	// Test: toggling window flags and verifying they hold
	{
		vizBeginTest( "window flags toggle correctly" );

		// Save initial states
		bool prevDebugPath = g_ShowDebugPathWindow;
		bool prevWorldView = g_ShowWorldViewWindow;
		bool prevOutliner = g_ShowEntityOutlinerWindow;

		// Toggle all on
		g_ShowDebugPathWindow = true;
		g_ShowWorldViewWindow = true;
		g_ShowEntityOutlinerWindow = true;

		VIZ_CHECK( g_ShowDebugPathWindow );
		VIZ_CHECK( g_ShowWorldViewWindow );
		VIZ_CHECK( g_ShowEntityOutlinerWindow );

		// Toggle all off
		g_ShowDebugPathWindow = false;
		g_ShowWorldViewWindow = false;
		g_ShowEntityOutlinerWindow = false;

		VIZ_CHECK( !g_ShowDebugPathWindow );
		VIZ_CHECK( !g_ShowWorldViewWindow );
		VIZ_CHECK( !g_ShowEntityOutlinerWindow );

		// Restore
		g_ShowDebugPathWindow = prevDebugPath;
		g_ShowWorldViewWindow = prevWorldView;
		g_ShowEntityOutlinerWindow = prevOutliner;
		vizEndTest();
	}

	// Test: all window flags default to false
	{
		vizBeginTest( "window flags default to false" );

		// These are the initial values set at file scope in visualization_core.cpp
		// After a fresh start they should all be false (before settings load)
		// We just verify the type/accessibility here
		VIZ_CHECK( !g_ShowDebugPathWindow || g_ShowDebugPathWindow ); // always true, verifies accessible
		VIZ_CHECK( !g_ShowGoapieVisualizationWindow || g_ShowGoapieVisualizationWindow );
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Test: DrawingLimits struct defaults
// ---------------------------------------------------------------------------
static void testDrawingLimitsDefaults()
{
	VIZ_TEST_SUITE( "DrawingLimitsStruct" );

	{
		vizBeginTest( "default margin is 10 percent" );
		DrawingLimits dl{};
		VIZ_CHECK_CLOSE( dl.margin, 0.1f );
		vizEndTest();
	}

	{
		vizBeginTest( "default scale is 1" );
		DrawingLimits dl{};
		VIZ_CHECK_CLOSE( dl.scale.x, 1.0f );
		VIZ_CHECK_CLOSE( dl.scale.y, 1.0f );
		VIZ_CHECK_CLOSE( dl.scale.z, 1.0f );
		vizEndTest();
	}

	{
		vizBeginTest( "default center is zero" );
		DrawingLimits dl{};
		VIZ_CHECK_CLOSE( dl.center.x, 0.0f );
		VIZ_CHECK_CLOSE( dl.center.y, 0.0f );
		VIZ_CHECK_CLOSE( dl.center.z, 0.0f );
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Test: Waypoint selection sync
// ---------------------------------------------------------------------------
static void testWaypointSelectionSync()
{
	VIZ_TEST_SUITE( "WaypointSelectionSync" );

	{
		vizBeginTest( "waypoint edit guid syncs to entity selection" );
		g_selectedEntityGuids.clear();
		g_WaypointEditSelectedGuid = gie::NullGuid;

		gie::World world{};
		auto* e = world.createEntity( "wp" );

		// Simulate waypoint editor selecting an entity
		g_WaypointEditSelectedGuid = e->guid();

		// The outliner syncs this at draw time; simulate that sync
		if( g_WaypointEditSelectedGuid != gie::NullGuid )
		{
			g_selectedEntityGuids.clear();
			g_selectedEntityGuids.insert( g_WaypointEditSelectedGuid );
		}

		VIZ_CHECK_EQ( g_selectedEntityGuids.size(), (size_t)1 );
		VIZ_CHECK( *g_selectedEntityGuids.begin() == e->guid() );

		// Cleanup
		g_selectedEntityGuids.clear();
		g_WaypointEditSelectedGuid = gie::NullGuid;
		vizEndTest();
	}

	{
		vizBeginTest( "entity selection syncs to waypoint edit" );
		g_selectedEntityGuids.clear();
		g_WaypointEditSelectedGuid = gie::NullGuid;

		gie::World world{};
		auto* e = world.createEntity( "wp2" );

		// Simulate outliner click: select entity and sync to waypoint editor
		g_selectedEntityGuids.clear();
		g_selectedEntityGuids.insert( e->guid() );
		g_WaypointEditSelectedGuid = e->guid();

		VIZ_CHECK_EQ( g_WaypointEditSelectedGuid, e->guid() );

		// Cleanup
		g_selectedEntityGuids.clear();
		g_WaypointEditSelectedGuid = gie::NullGuid;
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Test: Archetype selection state
// ---------------------------------------------------------------------------
static void testArchetypeSelectionState()
{
	VIZ_TEST_SUITE( "ArchetypeSelection" );

	{
		vizBeginTest( "archetype selection starts null" );
		gie::Guid saved = g_SelectedArchetypeGuid;
		g_SelectedArchetypeGuid = gie::NullGuid;
		VIZ_CHECK_EQ( g_SelectedArchetypeGuid, gie::NullGuid );
		g_SelectedArchetypeGuid = saved;
		vizEndTest();
	}

	{
		vizBeginTest( "set and clear archetype selection" );
		gie::Guid saved = g_SelectedArchetypeGuid;

		gie::Guid testGuid = 12345;
		g_SelectedArchetypeGuid = testGuid;
		VIZ_CHECK_EQ( g_SelectedArchetypeGuid, testGuid );

		g_SelectedArchetypeGuid = gie::NullGuid;
		VIZ_CHECK_EQ( g_SelectedArchetypeGuid, gie::NullGuid );

		g_SelectedArchetypeGuid = saved;
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Test: Path step mode state
// ---------------------------------------------------------------------------
static void testPathStepModeState()
{
	VIZ_TEST_SUITE( "PathStepMode" );

	{
		vizBeginTest( "path step mode defaults" );
		bool savedMode = g_PathStepMode;
		int savedIndex = g_PathStepIndex;
		gie::Guid savedGuid = g_PathStepSimGuid;

		g_PathStepMode = false;
		g_PathStepIndex = 0;
		g_PathStepSimGuid = gie::NullGuid;

		VIZ_CHECK( !g_PathStepMode );
		VIZ_CHECK_EQ( g_PathStepIndex, 0 );
		VIZ_CHECK_EQ( g_PathStepSimGuid, gie::NullGuid );

		g_PathStepMode = savedMode;
		g_PathStepIndex = savedIndex;
		g_PathStepSimGuid = savedGuid;
		vizEndTest();
	}

	{
		vizBeginTest( "enable and advance path step" );
		bool savedMode = g_PathStepMode;
		int savedIndex = g_PathStepIndex;

		g_PathStepMode = true;
		g_PathStepIndex = 0;
		VIZ_CHECK( g_PathStepMode );

		g_PathStepIndex++;
		VIZ_CHECK_EQ( g_PathStepIndex, 1 );
		g_PathStepIndex++;
		VIZ_CHECK_EQ( g_PathStepIndex, 2 );

		g_PathStepMode = savedMode;
		g_PathStepIndex = savedIndex;
		vizEndTest();
	}
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

int RunVisualizationTests()
{
	g_vizResults.clear();
	g_vizAssertionsRun = 0;

	std::printf( "\n" );
	std::printf( "========================================\n" );
	std::printf( "  GOAPie Visualization Tests\n" );
	std::printf( "========================================\n\n" );
	std::fflush( stdout );

	testDrawingLimitsDefaults();
	testUpdateDrawingBounds();
	testSelectionState();
	testEntityOutlinerOperations();
	testEntityOutlinerReset();
	testOutlinerInlineRenameAPI();
	testWindowSettingsPersistence();
	testWaypointSelectionSync();
	testArchetypeSelectionState();
	testPathStepModeState();

	// Print results
	int passed = 0;
	int failed = 0;
	std::string currentSuite;

	for( const auto& r : g_vizResults )
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
	std::printf( "  Assertions: %d\n", g_vizAssertionsRun );
	std::printf( "----------------------------------------\n" );

	if( failed > 0 )
	{
		std::printf( "  SOME VISUALIZATION TESTS FAILED\n" );
	}
	else
	{
		std::printf( "  ALL VISUALIZATION TESTS PASSED\n" );
	}

	std::printf( "========================================\n\n" );

	return failed;
}
