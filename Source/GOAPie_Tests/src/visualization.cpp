/*
* creates a window to draw GOAPie world context states
*/

#include <iostream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

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

int visualization()
{
	// Initialize GLFW
	glfwInit();
	glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 );
	glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 3 );
	glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );

	// Create a window
	GLFWwindow* window = glfwCreateWindow( 800, 600, "OpenGL Window", NULL, NULL );
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
	glViewport( 0, 0, 800, 600 );
	glfwSetFramebufferSizeCallback( window, framebuffer_size_callback );

	// Main loop
	while( !glfwWindowShouldClose( window ) )
	{
		processInput( window );

		// Render color
		glClearColor( 0.2f, 0.3f, 0.3f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );

		glfwSwapBuffers( window );
		glfwPollEvents();
	}

	// Clean up
	glfwTerminate();
	return 0;
}
