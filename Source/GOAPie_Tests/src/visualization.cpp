/*
* creates a window to draw GOAPie world context states
*/

#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <goapie.h>

void drawWaypoints( GLFWwindow* window, const gie::World& world, const std::vector< gie::Guid >& waypointGuids )  
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
   //glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );  
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

       // Render color  
       glClearColor( 0.2f, 0.3f, 0.3f, 1.0f );  
       glClear( GL_COLOR_BUFFER_BIT );  

       // Draw waypoints  
       drawWaypoints( window, world, waypointGuids );  

       // Draw a simple triangle in the middle of the canvas  
       glBegin( GL_TRIANGLES );  
       glColor3f( 1.0f, 0.0f, 0.0f ); // Red  
       glVertex2f( -0.1f, -0.1f );  
       glColor3f( 0.0f, 1.0f, 0.0f ); // Green  
       glVertex2f( 0.1f, -0.1f );  
       glColor3f( 0.0f, 0.0f, 1.0f ); // Blue  
       glVertex2f( 0.0f, 0.1f );  
       glEnd();  
       
       glfwSwapBuffers( window );  
       glfwPollEvents();  
   }  

   // Clean up  
   glfwTerminate();  
   return 0;  
}
