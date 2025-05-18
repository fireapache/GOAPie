/*
* creates a window to draw GOAPie world context states
*/

// clang-format off
#include <imgui.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>  
#include <imgui_impl_opengl3.h>
// clang-format on

#include <goapie.h>

#include <glm/glm.hpp>

#include "example.h"

struct DrawingLimits
{
	glm::vec3 minBounds{ std::numeric_limits< float >::max() };
	glm::vec3 maxBounds{ std::numeric_limits< float >::lowest() };
	const float margin = 0.1f; // 10% margin
};

void drawLinks(
	const gie::Entity* waypointEntity,
	const gie::World& world,
	const glm::vec3& offset,
	const glm::vec3& scale,
	const glm::vec3& scaledLocation )
{
	// Draw links if they exist
	if( auto linksPpt = waypointEntity->property( "Links" ) )
	{
		auto linkedGuids = linksPpt->getGuidArray();
		if( linkedGuids )
		{
			glColor3f( 0.5f, 0.5f, 0.5f ); // Set color to gray for links
			for( const auto& linkedGuid : *linkedGuids )
			{
				auto linkedEntity = world.entity( linkedGuid );
				if( auto linkedLocationPpt = linkedEntity->property( "Location" ) )
				{
					glm::vec3 linkedLocation = *linkedLocationPpt->getVec3();
					glm::vec3 scaledLinkedLocation = ( linkedLocation + offset ) * scale;

					// Draw a line between the waypoint and the linked waypoint
					glBegin( GL_LINES );
					glVertex3f( scaledLocation.x, scaledLocation.y, scaledLocation.z );
					glVertex3f( scaledLinkedLocation.x, scaledLinkedLocation.y, scaledLinkedLocation.z );
					glEnd();
				}
			}
		}
	}
}

void drawWaypointsAndLinks( const gie::World& world, DrawingLimits& drawingLimits )
{
	const auto waypointGuids = world.context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
	if( !waypointGuids )
	{
		return; // No waypoints to draw
	}

	for( gie::Guid waypointGuid : *waypointGuids )
	{
		auto waypointEntity = world.entity( waypointGuid );
		if( auto locationPpt = waypointEntity->property( "Location" ) )
		{
			glm::vec3 location = *locationPpt->getVec3();
			drawingLimits.minBounds = glm::min( drawingLimits.minBounds, location );
			drawingLimits.maxBounds = glm::max( drawingLimits.maxBounds, location );
		}
	}

	// Setting up drawing offset
	glm::vec3 range = drawingLimits.maxBounds - drawingLimits.minBounds;
	glm::vec3 minBounds = drawingLimits.minBounds - range * drawingLimits.margin;
	glm::vec3 maxBounds = drawingLimits.maxBounds + range * drawingLimits.margin;
	glm::vec3 offset = -( minBounds + maxBounds ) * 0.5f;
	glm::vec3 scale = 2.0f / ( maxBounds - minBounds ); // Scale to fit in clip space

	// Set up OpenGL for rendering
	glPointSize( 10.0f );		   // Set point size for waypoints
	glColor3f( 1.0f, 1.0f, 1.0f ); // Set color to white for waypoints

	// Iterate through waypoints and draw them as points and links
	for( gie::Guid waypointGuid : *waypointGuids )
	{
		auto waypointEntity = world.entity( waypointGuid );
		if( auto locationPpt = waypointEntity->property( "Location" ) )
		{
			glm::vec3 location = *locationPpt->getVec3();
			glm::vec3 scaledLocation = ( location + offset ) * scale;

			// Draw the waypoint as a point
			glBegin( GL_POINTS );
			glVertex3f( scaledLocation.x, scaledLocation.y, scaledLocation.z );
			glEnd();

			drawLinks( waypointEntity, world, offset, scale, scaledLocation );
		}
	}
}

void drawTrees( const gie::World& world, DrawingLimits& drawingLimits )
{
	const auto treeGuids = world.context().entityTagRegister().tagSet( { gie::stringHasher( "Tree" ) } );
	if( !treeGuids )
	{
		return; // No trees to draw
	}

	for( gie::Guid treeGuid : *treeGuids )
	{
		auto waypointEntity = world.entity( treeGuid );
		if( auto locationPpt = waypointEntity->property( "Location" ) )
		{
			glm::vec3 location = *locationPpt->getVec3();
			drawingLimits.minBounds = glm::min( drawingLimits.minBounds, location );
			drawingLimits.maxBounds = glm::max( drawingLimits.maxBounds, location );
		}
	}

	// Setting up drawing offset
	glm::vec3 range = drawingLimits.maxBounds - drawingLimits.minBounds;
	glm::vec3 minBounds = drawingLimits.minBounds - range * drawingLimits.margin;
	glm::vec3 maxBounds = drawingLimits.maxBounds + range * drawingLimits.margin;
	glm::vec3 offset = -( minBounds + maxBounds ) * 0.5f;
	glm::vec3 scale = 2.0f / ( maxBounds - minBounds ); // Scale to fit in clip space

	auto treeUpTag = gie::stringHasher( "TreeUp" );
	auto treeDownTag = gie::stringHasher( "TreeDown" );

	glPointSize( 10.0f ); // Set point size

	// Iterate through trees and draw them as points
	for( gie::Guid treeGuid : *treeGuids )
	{
		auto treeEntity = world.entity( treeGuid );
		if( auto locationPpt = treeEntity->property( "Location" ) )
		{
			glm::vec3 location = *locationPpt->getVec3();
			glm::vec3 scaledLocation = ( location + offset ) * scale;

			if( treeEntity->hasTag( treeUpTag ) )
			{
				glColor3f( 0.0f, 1.0f, 0.0f ); // Set color to green for trees that are up
			}
			else
			{
				glColor3f( 1.0f, 0.0f, 0.0f ); // Set color to red for trees that are down
			}

			// Draw the waypoint as a point
			glBegin( GL_POINTS );
			glVertex3f( scaledLocation.x, scaledLocation.y, scaledLocation.z );
			glEnd();
		}
	}
}

// Callback for resizing the window
void framebuffer_size_callback( GLFWwindow* window, int width, int height )
{
	glViewport( 0, 0, width, height );
}

// Process input (close window on ESC)
void processInput( GLFWwindow* window )
{
	if( glfwGetKey( window, GLFW_KEY_ESCAPE ) == GLFW_PRESS )
		glfwSetWindowShouldClose( window, true );
}

int visualization( ExampleParameters params )
{
	assert( params.world && params.planner && params.goal && "Invalid example parameters!" );

	gie::World& world = *params.world;
	gie::Planner& planner = *params.planner;
	gie::Goal& goal = *params.goal;

	// Initialize GLFW
	glfwInit();
	glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 );
	glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 3 );
	glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE );

	// Create a window
	GLFWwindow* window = glfwCreateWindow( 800, 800, "OpenGL Window", NULL, NULL );
	if( !window )
	{
		std::cout << "Failed to create GLFW window\n";
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent( window );

	// Load OpenGL functions using GLAD
	if( !gladLoadGLLoader( ( GLADloadproc )glfwGetProcAddress ) )
	{
		std::cout << "Failed to initialize GLAD\n";
		return -1;
	}

	// Set viewport and register resize callback
	glViewport( 0, 0, 800, 800 );
	glfwSetFramebufferSizeCallback( window, framebuffer_size_callback );

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL( window, true );
	ImGui_ImplOpenGL3_Init( "#version 130" );

	// Bounds of elements to be drawn
	DrawingLimits drawingLimits;
	bool useHeuristics = false;

	// Main loop
	while( !glfwWindowShouldClose( window ) )
	{
		processInput( window );

		// Render OpenGL content
		glClearColor( 0.2f, 0.3f, 0.3f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );

		// rendering elements
		drawWaypointsAndLinks( world, drawingLimits );
		drawTrees( world, drawingLimits );

		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Render ImGui UI
		if( ImGui::Begin( "GOAPie Visualization" ) )
		{
			ImGui::Checkbox( "Use Heuristics", &useHeuristics );

			if( planner.isReady() )
			{
				if( ImGui::Button( "Plan!" ) )
				{
					planner.plan( useHeuristics );
				}
			}
			else
			{
				ImGui::PushStyleVar( ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f );
				ImGui::Button( "Plan!" );
				ImGui::PopStyleVar();
				ImGui::SameLine();
				ImGui::Text( "Planner is busy!" );
			}

			auto waypointGuids = world.context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
			if( waypointGuids )
			{
				ImGui::Text( "Waypoint Count: %d", static_cast< int >( waypointGuids->size() ) );
			}
		}
		ImGui::End();

		ImGui::Render();

		// Render ImGui draw data
		ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );

		glfwSwapBuffers( window );
		glfwPollEvents();
	}

	// Cleanup ImGui
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	// Clean up
	glfwTerminate();
	return 0;
}
