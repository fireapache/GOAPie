/*
* creates a window to draw GOAPie world context states
*/

#include <iostream>
#include <imgui.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>  
#include <imgui_impl_opengl3.h>  
#include <glm/glm.hpp>
#include <goapie.h>

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

void drawWaypointsAndLinks( GLFWwindow* window, const gie::World& world, const std::vector< gie::Guid >& waypointGuids )
{  
   // Set up OpenGL for rendering  
   glPointSize( 10.0f );  
   glColor3f( 1.0f, 1.0f, 1.0f ); // Set color to white for waypoints  

   // Calculate offset and scale based on current waypoints  
   glm::vec3 minBounds( std::numeric_limits<float>::max() );  
   glm::vec3 maxBounds( std::numeric_limits<float>::lowest() );  

   for( gie::Guid waypointGuid : waypointGuids )  
   {  
       auto waypointEntity = world.entity( waypointGuid );  
       if( auto locationPpt = waypointEntity->property( "Location" ) )  
       {  
           glm::vec3 location = *locationPpt->getVec3();  
           minBounds = glm::min( minBounds, location );  
           maxBounds = glm::max( maxBounds, location );  
       }  
   }  

   // Add a margin around the bounds  
   const float margin = 0.1f; // 10% margin  
   glm::vec3 range = maxBounds - minBounds;  
   minBounds -= range * margin;  
   maxBounds += range * margin;  

   glm::vec3 offset = -( minBounds + maxBounds ) * 0.5f;  
   glm::vec3 scale = 2.0f / ( maxBounds - minBounds ); // Scale to fit in clip space  

   // Iterate through waypoints and draw them as points and links  
   for( gie::Guid waypointGuid : waypointGuids )  
   {  
       auto waypointEntity = world.entity( waypointGuid );  
       if( auto locationPpt = waypointEntity->property( "Location" ) )  
       {  
           glm::vec3 location = *locationPpt->getVec3();  
           glm::vec3 scaledLocation = (location + offset) * scale;  

           // Draw the waypoint as a point  
           glBegin( GL_POINTS );  
           glVertex3f( scaledLocation.x, scaledLocation.y, scaledLocation.z );  
           glEnd();  

           drawLinks( waypointEntity, world, offset, scale, scaledLocation );
       }  
   }  
}

void drawWaypointsOnly( GLFWwindow* window, const gie::World& world, const std::vector< gie::Guid >& waypointGuids )  
{  
    // Set up OpenGL for rendering waypoints  
    glPointSize( 10.0f );  
    glColor3f( 1.0f, 1.0f, 1.0f ); // Set color to white  

    // Calculate offset and scale based on current waypoints  
    glm::vec3 minBounds( std::numeric_limits<float>::max() );  
    glm::vec3 maxBounds( std::numeric_limits<float>::lowest() );  

    for( gie::Guid waypointGuid : waypointGuids )  
    {  
	    auto waypointEntity = world.entity( waypointGuid );  
	    if( auto locationPpt = waypointEntity->property( "Location" ) )  
	    {  
		    glm::vec3 location = *locationPpt->getVec3();  
		    minBounds = glm::min( minBounds, location );  
		    maxBounds = glm::max( maxBounds, location );  
	    }  
    }  

    // Add a margin around the bounds  
    const float margin = 0.1f; // 10% margin  
    glm::vec3 range = maxBounds - minBounds;  
    minBounds -= range * margin;  
    maxBounds += range * margin;  

    glm::vec3 offset = -(minBounds + maxBounds) * 0.5f;  
    glm::vec3 scale = 2.0f / (maxBounds - minBounds); // Scale to fit in clip space  

    // Iterate through waypoints and draw them as points  
    for( gie::Guid waypointGuid : waypointGuids )  
    {  
	    auto waypointEntity = world.entity( waypointGuid );  
	    if( auto locationPpt = waypointEntity->property( "Location" ) )  
	    {  
		    glm::vec3 location = *locationPpt->getVec3();  
		    location += offset; // Apply calculated offset  
		    location *= scale;  // Apply scaling to fit in clip space  

		    // Draw a single point at the waypoint location  
		    glBegin( GL_POINTS );  
		    glVertex3f( location.x, location.y, location.z );  
		    glEnd();  
	    }  
    }  
}

void drawWaypointLinksOnly( GLFWwindow* window, const gie::World& world, const std::vector< gie::Guid >& waypointGuids )  
{  
	glColor3f( 0.5f, 0.5f, 0.5f ); // Set color to gray for links  

	// Calculate offset and scale based on current waypoints  
	glm::vec3 minBounds( std::numeric_limits<float>::max() );  
	glm::vec3 maxBounds( std::numeric_limits<float>::lowest() );  

	for( gie::Guid waypointGuid : waypointGuids )  
	{  
		auto waypointEntity = world.entity( waypointGuid );  
		if( auto locationPpt = waypointEntity->property( "Location" ) )  
		{  
			glm::vec3 location = *locationPpt->getVec3();  
			minBounds = glm::min( minBounds, location );  
			maxBounds = glm::max( maxBounds, location );  
		}  
	}  

	// Add a margin around the bounds  
	const float margin = 0.1f; // 10% margin  
	glm::vec3 range = maxBounds - minBounds;  
	minBounds -= range * margin;  
	maxBounds += range * margin;  

	glm::vec3 offset = -(minBounds + maxBounds) * 0.5f;  
	glm::vec3 scale = 2.0f / (maxBounds - minBounds); // Scale to fit in clip space  

	for( gie::Guid waypointGuid : waypointGuids )  
	{  
		auto waypointEntity = world.entity( waypointGuid );  
		if( auto locationPpt = waypointEntity->property( "Location" ) )  
		{  
			glm::vec3 startLocation = *locationPpt->getVec3();  
			startLocation += offset; // Apply calculated offset  
			startLocation *= scale;  // Apply scaling to fit in clip space  

			if( auto linksPpt = waypointEntity->property( "Links" ) )  
			{  
				auto linkedGuids = linksPpt->getGuidArray();  
				if( !linkedGuids )  
				{  
					continue;  
				}  
				// Iterate through linked waypoints  
				for( const auto& linkedGuid : *linkedGuids )  
				{  
					auto linkedEntity = world.entity( linkedGuid );  
					if( auto linkedLocationPpt = linkedEntity->property( "Location" ) )  
					{  
						glm::vec3 endLocation = *linkedLocationPpt->getVec3();  
						endLocation += offset; // Apply calculated offset  
						endLocation *= scale;  // Apply scaling to fit in clip space  

						// Draw a line between startLocation and endLocation  
						glBegin( GL_LINES );  
						glVertex3f( startLocation.x, startLocation.y, startLocation.z );  
						glVertex3f( endLocation.x, endLocation.y, endLocation.z );  
						glEnd();  
					}  
				}  
			}  
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

int visualization( const gie::World& world )  
{  
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

	// Collect waypoint GUIDs from the world  
	std::vector<gie::Guid> waypointGuids;  
	for( const auto& itr : world.context().entities() )  
	{  
		if( itr.second.property( "Location" ) )  
		{  
			waypointGuids.push_back( itr.first );  
		}  
	}  

	// Main loop  
	while( !glfwWindowShouldClose( window ) )  
	{  
		processInput( window );  

		// Start ImGui frame  
		ImGui_ImplOpenGL3_NewFrame();  
		ImGui_ImplGlfw_NewFrame();  
		ImGui::NewFrame();  

		// Render ImGui UI  
		ImGui::Begin( "GOAPie Visualization" );  
		ImGui::Text( "Waypoint Count: %d", static_cast<int>( waypointGuids.size() ) );  
		ImGui::End();  

		ImGui::Render();  

		// Render OpenGL content  
		glClearColor( 0.2f, 0.3f, 0.3f, 1.0f );  
		glClear( GL_COLOR_BUFFER_BIT );  

		drawWaypointsAndLinks( window, world, waypointGuids );  

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
