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

// our window dimensions
const GLuint WIDTH = 1280;
const GLint HEIGHT = 920;

// global defined indices for OpenGL
GLuint VAO;		   // vertex array object
GLuint VBO;		   // vertex buffer object
GLuint FBO;		   // frame buffer object
GLuint RBO;		   // rendering buffer object
GLuint texture_id; // the texture id we'll need later to create a texture 

struct DrawingLimits
{
	glm::vec3 minBounds{ std::numeric_limits< float >::max() };
	glm::vec3 maxBounds{ std::numeric_limits< float >::lowest() };
	const float margin = 0.1f; // 10% margin
};

// Add this at the top of your file or in a suitable scope
static gie::Guid selectedSimulationGuid = gie::NullGuid;

void drawSimulationTreeView( const gie::Planner& planner, const gie::Simulation* simulation );
void drawTrees( const gie::World& world, DrawingLimits& drawingLimits );
void drawWaypointsAndLinks( const gie::World& world, DrawingLimits& drawingLimits );
void drawImGuiWindow( bool& useHeuristics, gie::Planner& planner, gie::World& world );
void processInput( GLFWwindow* window );
void framebuffer_size_callback( GLFWwindow* window, int width, int height );
void create_framebuffer();
void bind_framebuffer();
void unbind_framebuffer();
void rescale_framebuffer( float width, float height );
void ShowExampleAppDockSpace( bool* p_open );

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
	GLFWwindow* window = glfwCreateWindow( WIDTH, HEIGHT, "OpenGL Window", NULL, NULL );
	if( !window )
	{
		std::cout << "Failed to create GLFW window\n";
		glfwTerminate();
		return -1;
	}

	// Center the window on the primary monitor
	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	if( primaryMonitor )
	{
		const GLFWvidmode* mode = glfwGetVideoMode( primaryMonitor );
		if( mode )
		{
			int xpos = ( mode->width - WIDTH ) / 2;
			int ypos = ( mode->height - HEIGHT ) / 2;
			glfwSetWindowPos( window, xpos, ypos );
		}
	}

	glfwMakeContextCurrent( window );

	// Load OpenGL functions using GLAD
	if( !gladLoadGLLoader( ( GLADloadproc )glfwGetProcAddress ) )
	{
		std::cout << "Failed to initialize GLAD\n";
		return -1;
	}

	// Set viewport and register resize callback
	glViewport( 0, 0, WIDTH, HEIGHT );
	glfwSetFramebufferSizeCallback( window, framebuffer_size_callback );

	create_framebuffer();

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable multi-viewport support
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;	// Enable docking support
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

		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ShowExampleAppDockSpace( nullptr ); // Create a dockspace for the ImGui windows

		// Render ImGui UI
		drawImGuiWindow( useHeuristics, planner, world );

		if( ImGui::Begin( "My Scene" ) )
		{
			// we access the ImGui window size
			const float window_width = ImGui::GetContentRegionAvail().x;
			const float window_height = ImGui::GetContentRegionAvail().y;

			// we rescale the framebuffer to the actual window size here and reset the glViewport
			rescale_framebuffer( window_width, window_height );
			glViewport( 0, 0, window_width, window_height );

			// we get the screen position of the window
			ImVec2 pos = ImGui::GetCursorScreenPos();

			// and here we can add our created texture as image to ImGui
			// unfortunately we need to use the cast to void* or I didn't find another way tbh
			ImGui::GetWindowDrawList()->AddImage(
				( void* )texture_id,
				ImVec2( pos.x, pos.y ),
				ImVec2( pos.x + window_width, pos.y + window_height ),
				ImVec2( 0, 1 ),
				ImVec2( 1, 0 ) );
		}
		ImGui::End();

		ImGui::Render();

		// Render ImGui draw data
		ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );

		// Update and Render additional Platform Windows
		if( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
		{
			GLFWwindow* currentGlfwContext = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent( currentGlfwContext );
		}

		// now we can bind our framebuffer
		bind_framebuffer();

		// Render OpenGL content
		glClearColor( 0.2f, 0.3f, 0.3f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );

		// rendering elements
		drawWaypointsAndLinks( world, drawingLimits );
		drawTrees( world, drawingLimits );

		// and unbind it again
		unbind_framebuffer();

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

// Process input (close window on ESC)
void processInput( GLFWwindow* window )
{
	if( glfwGetKey( window, GLFW_KEY_ESCAPE ) == GLFW_PRESS )
		glfwSetWindowShouldClose( window, true );
}

// Callback for resizing the window
void framebuffer_size_callback( GLFWwindow* window, int width, int height )
{
	glViewport( 0, 0, width, height );
}

void drawImGuiWindow( bool& useHeuristics, gie::Planner& planner, gie::World& world )
{
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

		// TreeView for simulation nodes
		if( ImGui::CollapsingHeader( "Simulation Nodes" ) )
		{
			auto selectedSim = planner.simulation( selectedSimulationGuid );
			ImGui::Separator();
			ImGui::Text( "Selected Node Info:" );
			if( selectedSim )
			{
				ImGui::Text( "Guid: %llu", selectedSim ? static_cast< unsigned long long >( selectedSim->guid() ) : 0 );
			}
			else
			{
				ImGui::TextUnformatted( "Guid: ?" );
			}
			ImGui::Text( "Actions: %zu", selectedSim ? selectedSim->actions.size() : 0 );
			const bool isMaxCost = selectedSim ? selectedSim->cost == gie::MaxCost : true;
			if( isMaxCost )
			{
				ImGui::TextUnformatted( "Cost: MAX" );
			}
			else if( selectedSim )
			{
				ImGui::Text( "Cost: %.2f", selectedSim ? selectedSim->cost : 0 );
			}
			else
			{
				ImGui::TextUnformatted( "Cost: ?" );
			}

			auto rootNode = planner.rootSimulation();
			if( rootNode )
			{
				drawSimulationTreeView( planner, rootNode );
			}
			else
			{
				ImGui::Text( "No simulation nodes available." );
			}
		}
	}
	ImGui::End();
}

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

void drawSimulationTreeView( const gie::Planner& planner, const gie::Simulation* simulation )
{
	if( !simulation )
		return;

	std::string actionsText{ "" };
	for( auto action : simulation->actions )
	{
		if( action )
		{
			actionsText.append( action->name() );
			if( action != simulation->actions.back() )
			{
				actionsText.append( ", " );
			}
		}
	}

	if( actionsText.empty() )
	{
		actionsText = "Root";
	}

	// Add unique ID to label
	std::string label = actionsText + "##" + std::to_string( simulation->guid() );

	ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if( selectedSimulationGuid == simulation->guid() )
		nodeFlags |= ImGuiTreeNodeFlags_Selected;

	bool nodeOpen = ImGui::TreeNodeEx( label.c_str(), nodeFlags );
	if( ImGui::IsItemClicked() )
	{
		selectedSimulationGuid = simulation->guid();
	}

	if( nodeOpen )
	{
		for( auto childSimulationGuid : simulation->outgoing )
		{
			auto childSimulation = planner.simulation( childSimulationGuid );
			if( childSimulation )
			{
				drawSimulationTreeView( planner, childSimulation );
			}
		}
		ImGui::TreePop();
	}
}

// here we create our framebuffer and our renderbuffer
// you can find a more detailed explanation of framebuffer
// on the official opengl homepage, see the link above
void create_framebuffer()
{
	glGenFramebuffers( 1, &FBO );
	glBindFramebuffer( GL_FRAMEBUFFER, FBO );

	glGenTextures( 1, &texture_id );
	glBindTexture( GL_TEXTURE_2D, texture_id );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, WIDTH, HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0 );

	glGenRenderbuffers( 1, &RBO );
	glBindRenderbuffer( GL_RENDERBUFFER, RBO );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WIDTH, HEIGHT );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, RBO );

	if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
		std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!\n";

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glBindRenderbuffer( GL_RENDERBUFFER, 0 );
}

// here we bind our framebuffer
void bind_framebuffer()
{
	glBindFramebuffer( GL_FRAMEBUFFER, FBO );
}

// here we unbind our framebuffer
void unbind_framebuffer()
{
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

// and we rescale the buffer, so we're able to resize the window
void rescale_framebuffer( float width, float height )
{
	glBindTexture( GL_TEXTURE_2D, texture_id );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0 );

	glBindRenderbuffer( GL_RENDERBUFFER, RBO );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, RBO );
}

// Demonstrate using DockSpace() to create an explicit docking node within an existing window.
// Note: You can use most Docking facilities without calling any API. You DO NOT need to call DockSpace() to use Docking!
// - Drag from window title bar or their tab to dock/undock. Hold SHIFT to disable docking.
// - Drag from window menu button (upper-left button) to undock an entire node (all windows).
// About dockspaces:
// - Use DockSpace() to create an explicit dock node _within_ an existing window.
// - Use DockSpaceOverViewport() to create an explicit dock node covering the screen or a specific viewport.
//   This is often used with ImGuiDockNodeFlags_PassthruCentralNode.
// - Important: Dockspaces need to be submitted _before_ any window they can host. Submit it early in your frame! (*)
// - Important: Dockspaces need to be kept alive if hidden, otherwise windows docked into it will be undocked.
//   e.g. if you have multiple tabs with a dockspace inside each tab: submit the non-visible dockspaces with ImGuiDockNodeFlags_KeepAliveOnly.
// (*) because of this constraint, the implicit \"Debug\" window can not be docked into an explicit DockSpace() node,
// because that window is submitted as part of the part of the NewFrame() call. An easy workaround is that you can create
// your own implicit "Debug##2" window after calling DockSpace() and leave it in the window stack for anyone to use.
void ShowExampleAppDockSpace( bool* p_open )
{
	// Variables to configure the Dockspace example.
	static bool opt_fullscreen = true; // Is the Dockspace full-screen?
	static bool opt_padding = false;   // Is there padding (a blank space) between the window edge and the Dockspace?
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None; // Config flags for the Dockspace

	// In this example, we're embedding the Dockspace into an invisible parent window to make it more configurable.
	// We set ImGuiWindowFlags_NoDocking to make sure the parent isn't dockable into because this is handled by the Dockspace.
	//
	// ImGuiWindowFlags_MenuBar is to show a menu bar with config options. This isn't necessary to the functionality of a
	// Dockspace, but it is here to provide a way to change the configuration flags interactively.
	// You can remove the MenuBar flag if you don't want it in your app, but also remember to remove the code which actually
	// renders the menu bar, found at the end of this function.
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

	// Is the example in Fullscreen mode?
	if( opt_fullscreen )
	{
		// If so, get the main viewport:
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		// Set the parent window's position, size, and viewport to match that of the main viewport. This is so the parent window
		// completely covers the main viewport, giving it a "full-screen" feel.
		ImGui::SetNextWindowPos( viewport->Pos );
		ImGui::SetNextWindowSize( viewport->Size );
		ImGui::SetNextWindowViewport( viewport->ID );

		// Set the parent window's styles to match that of the main viewport:
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );	 // No corner rounding on the window
		ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f ); // No border around the window

		// Manipulate the window flags to make it inaccessible to the user (no titlebar, resize/move, or navigation)
		window_flags
			|= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}
	else
	{
		// The example is not in Fullscreen mode (the parent window can be dragged around and resized), disable the
		// ImGuiDockNodeFlags_PassthruCentralNode flag.
		dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
	}

	// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
	// and handle the pass-thru hole, so the parent window should not have its own background:
	if( dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode )
		window_flags |= ImGuiWindowFlags_NoBackground;

	// If the padding option is disabled, set the parent window's padding size to 0 to effectively hide said padding.
	if( !opt_padding )
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );

	// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
	// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
	// all active windows docked into it will lose their parent and become undocked.
	// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
	// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
	ImGui::Begin( "DockSpace Demo", p_open, window_flags );

	// Remove the padding configuration - we pushed it, now we pop it:
	if( !opt_padding )
		ImGui::PopStyleVar();

	// Pop the two style rules set in Fullscreen mode - the corner rounding and the border size.
	if( opt_fullscreen )
		ImGui::PopStyleVar( 2 );

	// Check if Docking is enabled:
	ImGuiIO& io = ImGui::GetIO();
	if( io.ConfigFlags & ImGuiConfigFlags_DockingEnable )
	{
		// If it is, draw the Dockspace with the DockSpace() function.
		// The GetID() function is to give a unique identifier to the Dockspace - here, it's "MyDockSpace".
		ImGuiID dockspace_id = ImGui::GetID( "MyDockSpace" );
		ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), dockspace_flags );
	}

	// This is to show the menu bar that will change the config settings at runtime.
	// If you copied this demo function into your own code and removed ImGuiWindowFlags_MenuBar at the top of the function,
	// you should remove the below if-statement as well.
	if( ImGui::BeginMenuBar() )
	{
		if( ImGui::BeginMenu( "Options" ) )
		{
			// Disabling fullscreen would allow the window to be moved to the front of other windows,
			// which we can't undo at the moment without finer window depth/z control.
			ImGui::MenuItem( "Fullscreen", NULL, &opt_fullscreen );
			ImGui::MenuItem( "Padding", NULL, &opt_padding );
			ImGui::Separator();

			// Display a menu item for each Dockspace flag, clicking on one will toggle its assigned flag.
			if( ImGui::MenuItem( "Flag: NoSplit", "", ( dockspace_flags & ImGuiDockNodeFlags_NoSplit ) != 0 ) )
			{
				dockspace_flags ^= ImGuiDockNodeFlags_NoSplit;
			}
			if( ImGui::MenuItem( "Flag: NoResize", "", ( dockspace_flags & ImGuiDockNodeFlags_NoResize ) != 0 ) )
			{
				dockspace_flags ^= ImGuiDockNodeFlags_NoResize;
			}
			if( ImGui::MenuItem(
					"Flag: NoDockingInCentralNode",
					"",
					( dockspace_flags & ImGuiDockNodeFlags_NoDockingInCentralNode ) != 0 ) )
			{
				dockspace_flags ^= ImGuiDockNodeFlags_NoDockingInCentralNode;
			}
			if( ImGui::MenuItem( "Flag: AutoHideTabBar", "", ( dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar ) != 0 ) )
			{
				dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar;
			}
			if( ImGui::MenuItem(
					"Flag: PassthruCentralNode",
					"",
					( dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode ) != 0,
					opt_fullscreen ) )
			{
				dockspace_flags ^= ImGuiDockNodeFlags_PassthruCentralNode;
			}
			ImGui::Separator();

			// Display a menu item to close this example.
			if( ImGui::MenuItem( "Close", NULL, false, p_open != NULL ) )
				if( p_open
					!= NULL ) // Remove MSVC warning C6011 (NULL dereference) - the `p_open != NULL` in MenuItem() does prevent NULL derefs, but IntelliSense doesn't analyze that deep so we need to add this in ourselves.
					*p_open
						= false; // Changing this variable to false will close the parent window, therefore closing the Dockspace as well.
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	// End the parent window that contains the Dockspace:
	ImGui::End();
}