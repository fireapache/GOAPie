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
#include <cmath>
#include <cctype>

#include "example.h"

// our window dimensions
const GLuint WIDTH = 1280;
const GLint HEIGHT = 920;

// global defined indices for OpenGL
GLuint VAO;          // vertex array object
GLuint VBO;          // vertex buffer object
GLuint FBO;          // frame buffer object
GLuint RBO;          // rendering buffer object
GLuint texture_id; // the texture id we'll need later to create a texture 

struct DrawingLimits
{
    glm::vec3 minBounds{ std::numeric_limits< float >::max() };
    glm::vec3 maxBounds{ std::numeric_limits< float >::lowest() };
    const float margin = 0.1f; // 10% margin
};

// Add this at the top of your file or in a suitable scope
static gie::Guid selectedSimulationGuid = gie::NullGuid;
// Global toggle to show waypoint GUID suffixes
static bool g_ShowWaypointGuidSuffix = false;
// Global toggle to show arrowheads on waypoint links
static bool g_ShowWaypointArrows = false;

void drawSimulationTreeView( const gie::Planner& planner, const gie::Simulation* simulation );
void drawTrees( const gie::World& world, const gie::Planner& planner, DrawingLimits& drawingLimits );
void drawWaypointsAndLinks( const gie::World& world, const gie::Planner& planner, DrawingLimits& drawingLimits );
void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params );
void processInput( GLFWwindow* window );
void framebuffer_size_callback( GLFWwindow* window, int width, int height );
void create_framebuffer();
void bind_framebuffer();
void unbind_framebuffer();
void rescale_framebuffer( float width, float height );
void ShowExampleAppDockSpace( bool* p_open );
void drawSelectedSimulationPath( const gie::World& world, const gie::Planner& planner );
void drawWorldViewWindow( const gie::World& world, const gie::Planner& planner );
void drawAgentCrosshair( const gie::World& world, const gie::Planner& planner );

int visualization( ExampleParameters& params )
{
    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
    gie::Goal& goal = params.goal;

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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // Enable docking support
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
        drawImGuiWindows( useHeuristics, params );

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
        drawWaypointsAndLinks( world, planner, drawingLimits );
        drawTrees( world, planner, drawingLimits );
        drawSelectedSimulationPath( world, planner );
        drawAgentCrosshair( world, planner );

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

void drawWorldViewWindow( const gie::World& world, const gie::Planner& planner )
{
    if( ImGui::Begin( "World View" ) )
    {
        // we access the ImGui window size
        const float windowWidth = ImGui::GetContentRegionAvail().x;
        const float windowHeight = ImGui::GetContentRegionAvail().y;

        // we rescale the framebuffer to the actual window size here and reset the glViewport
        rescale_framebuffer( static_cast< GLsizei >( windowWidth ), static_cast< GLsizei >( windowHeight ) );
        glViewport( 0, 0, static_cast< GLsizei >( windowWidth ), static_cast< GLsizei >( windowHeight ) );

        // we get the screen position of the window
        ImVec2 pos = ImGui::GetCursorScreenPos();

        // and here we can add our created texture as image to ImGui
        ImGui::GetWindowDrawList()->AddImage(
            texture_id,
            ImVec2( pos.x, pos.y ),
            ImVec2( pos.x + windowWidth, pos.y + windowHeight ),
            ImVec2( 0, 1 ),
            ImVec2( 1, 0 ) );

        // Optionally overlay last 4 digits of waypoint GUIDs
        if( g_ShowWaypointGuidSuffix )
        {
            const gie::Blackboard* contextBB = &world.context();
            if( const auto* sim = planner.simulation( selectedSimulationGuid ) )
            {
                contextBB = &sim->context();
            }

            // gather bounds from waypoints and trees to match transform
            glm::vec3 minBounds( std::numeric_limits< float >::max() );
            glm::vec3 maxBounds( std::numeric_limits< float >::lowest() );

            auto waypointTag = gie::stringHasher( "Waypoint" );
            auto treeTag = gie::stringHasher( "Tree" );
            const auto* waypointSet = contextBB->entityTagRegister().tagSet( { waypointTag } );
            const auto* treeSet = contextBB->entityTagRegister().tagSet( { treeTag } );

            if( waypointSet )
            {
                for( auto guid : *waypointSet )
                {
                    if( const auto* e = world.entity( guid ) )
                    {
                        if( const auto* loc = e->property( "Location" ) )
                        {
                            glm::vec3 p = *loc->getVec3();
                            minBounds = glm::min( minBounds, p );
                            maxBounds = glm::max( maxBounds, p );
                        }
                    }
                }
            }

            if( treeSet )
            {
                for( auto guid : *treeSet )
                {
                    if( const auto* e = contextBB->entity( guid ) )
                    {
                        if( const auto* loc = e->property( "Location" ) )
                        {
                            glm::vec3 p = *loc->getVec3();
                            minBounds = glm::min( minBounds, p );
                            maxBounds = glm::max( maxBounds, p );
                        }
                    }
                }
            }

            if( waypointSet && !waypointSet->empty() )
            {
                glm::vec3 range = maxBounds - minBounds;
                glm::vec3 minB = minBounds - range * 0.1f;
                glm::vec3 maxB = maxBounds + range * 0.1f;
                glm::vec3 offset = -( minB + maxB ) * 0.5f;
                glm::vec3 scale = 2.0f / ( maxB - minB );

                ImDrawList* dl = ImGui::GetWindowDrawList();

                for( auto guid : *waypointSet )
                {
                    if( const auto* e = world.entity( guid ) )
                    {
                        if( const auto* loc = e->property( "Location" ) )
                        {
                            glm::vec3 p = ( *loc->getVec3() + offset ) * scale; // NDC
                            // Map NDC [-1,1] to image pixels [0,w]x[0,h] and account for flipped Y in image UVs
                            float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
                            float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

                            // Position in screen space (base position for suffix)
                            ImVec2 suffixPos{ pos.x + x_px, pos.y + y_px - 12.0f };

                            // last 4 decimal digits
                            unsigned long long id = static_cast< unsigned long long >( guid );
                            unsigned int last4 = static_cast< unsigned int >( id % 10000ULL );
                            char buf[8];
                            snprintf( buf, sizeof( buf ), "%04u", last4 );

                            // center align suffix
                            ImVec2 suffixSize = ImGui::CalcTextSize( buf );
                            suffixPos.x -= suffixSize.x * 0.5f;

                            // draw suffix with slight shadow for readability
                            dl->AddText( ImVec2( suffixPos.x + 1, suffixPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), buf );
                            dl->AddText( suffixPos, IM_COL32( 255, 255, 0, 255 ), buf );

                            // Derive waypoint index from entity name, expected to be "waypoint<number>"
                            std::string idxLabel = "wp?";
                            auto nameHash = e->nameHash();
                            if( nameHash != gie::InvalidStringHash )
                            {
                                std::string name( gie::stringRegister().get( nameHash ) );
                                size_t j = name.size();
                                while( j > 0 && std::isdigit( static_cast< unsigned char >( name[ j - 1 ] ) ) )
                                {
                                    --j;
                                }
                                if( j < name.size() )
                                {
                                    idxLabel = std::string( "wp" ) + name.substr( j );
                                }
                            }

                            // Position index label above the suffix and center align
                            ImVec2 idxSize = ImGui::CalcTextSize( idxLabel.c_str() );
                            ImVec2 idxPos{ pos.x + x_px - idxSize.x * 0.5f, suffixPos.y - ( idxSize.y + 2.0f ) };

                            // draw index label (white) with shadow
                            dl->AddText( ImVec2( idxPos.x + 1, idxPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), idxLabel.c_str() );
                            dl->AddText( idxPos, IM_COL32( 255, 255, 255, 255 ), idxLabel.c_str() );
                        }
                    }
                }
            }
        }
    }
    ImGui::End();
}

void drawEntityNameText( const gie::Entity& entity, const gie::Guid entityGuid, const bool padding )
{
    auto nameHash = entity.nameHash();
    const bool isValidName = nameHash != gie::InvalidStringHash;
    const std::string_view entityName = isValidName ? gie::stringRegister().get( nameHash ) : "Unnamed Entity";

    if( padding )
    {
        ImGui::SetCursorPosX( ( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( entityName.data() ).x ) * 0.225f );
    }

    if( isValidName )
    {
        ImGui::Text( "%s", entityName.data() );
    }
    else
    {
        ImGui::Text( "%llu", static_cast< unsigned long long >( entityGuid ) );
    }
}

void drawBlackboardPropertiesWindow( const gie::Simulation* simulation )
{
    if( !simulation ) return;

    const gie::Blackboard* currentContext = &simulation->context();
    std::set< gie::Guid > entityGuids;
    const auto& stringregister = gie::stringRegister();

    while( currentContext )
    {
        for( const auto& [ entityGuid, entity ] : currentContext->entities() )
        {
            entityGuids.insert( entityGuid );
        }

        currentContext = currentContext->parent();
    }

    static bool multiLevel = false;

    if( ImGui::Begin( "Blackboard Properties" ) )
    {
        currentContext = &simulation->context();
        ImGui::Checkbox( "Multi-Level", &multiLevel );

        if( multiLevel )
        {
            int level = 0;

            while( currentContext )
            {
                ImGui::Text( "Blackboard Level: %d", level );
                ImGui::Separator();

                for( const auto& [ entityGuid, entity ] : currentContext->entities() )
                {
                    drawEntityNameText( entity, entityGuid, true );
                    ImGui::Separator();

                    ImGui::Columns( 2, nullptr, false ); // Create two columns
                    for( const auto& [ propertyNameHash, propertyGuid ] : entity.properties() )
                    {
                        auto propertyName = stringregister.get( propertyNameHash );
                        auto property = currentContext->property( propertyGuid );
                        ImGui::Text( "%s", propertyName.data() );
                        ImGui::NextColumn();
                        ImGui::Text( "%s", property->toString().c_str() );
                        ImGui::NextColumn();
                    }
                    ImGui::Columns( 1 ); // Reset to single column
                    ImGui::Separator();
                }

                currentContext = currentContext->parent();
                level++;
                ImGui::Separator();
            }
        }
        else
        {
            for( auto entityGuid : entityGuids )
            {
                auto entity = currentContext->entity( entityGuid );

                drawEntityNameText( *entity, entityGuid, true );
                ImGui::Separator();

                ImGui::Columns( 2, nullptr, false ); // Create two columns
                for( const auto& [ propertyNameHash, propertyGuid ] : entity->properties() )
                {
                    auto propertyName = stringregister.get( propertyNameHash );
                    auto property = currentContext->property( propertyGuid );
                    ImGui::Text( "%s", propertyName.data() );
                    ImGui::NextColumn();
                    ImGui::Text( "%s", property->toString().c_str() );
                    ImGui::NextColumn();
                }
                ImGui::Columns( 1 ); // Reset to single column
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
}

void drawGoapieVisualizationWindow( bool& useHeuristics, ExampleParameters& params )
{
    gie::World& world = params.world;
    gie::Planner& planner = params.planner;

    if( ImGui::Begin( "GOAPie Visualization" ) )
    {
        ImGui::Checkbox( "Use Heuristics", &useHeuristics );
        ImGui::Checkbox( "Log Plan", &planner.logStepsMutator() );
        ImGui::Checkbox( "Step Plan", &planner.stepMutator() );
        ImGui::Checkbox( "Show Waypoint IDs (last 4)", &g_ShowWaypointGuidSuffix );
        ImGui::Checkbox( "Show Waypoint Arrows", &g_ShowWaypointArrows );

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

            if( planner.stepMutator() && ImGui::Button( "Step" ) )
            {
                planner.plan( useHeuristics );
            }
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

            ImGui::Separator();

            if( params.imGuiDrawFunc )
            {
                params.imGuiDrawFunc( world, planner, params.goal, selectedSimulationGuid );
                ImGui::Separator();
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

            drawBlackboardPropertiesWindow( selectedSim );
        }
    }
    ImGui::End();
}

void drawDebugMessagesWindow( ExampleParameters& params )
{
    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation )
    {
        return;
    }

    const auto& debugMessages = selectedSimulation->debugMessages();

    if( ImGui::Begin( "Debug Messages" ) )
    {
        if( !debugMessages.messages() || debugMessages.messages()->empty() )
        {
            ImGui::Text( "No debug messages available." );
        }
        else
        {
            for( const auto& message : *debugMessages.messages() )
            {
                ImGui::Text( "* %s", message.c_str() );
            }
        }
    }
    ImGui::End();
}  

void drawSimulationArgumentsWindow( ExampleParameters& params )
{  
    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation )
        return;

    const auto& arguments = selectedSimulation->arguments();

    if( ImGui::Begin( "Simulation Arguments" ) )
    {
        if( arguments.empty() )
        {
            ImGui::Text( "No arguments available." );
        }
        else
        {
            for( const auto& [ key, value ] : arguments.storage() )
            {
                // using property to access toString function for variant.
                // This is a workaround since NamedArguments does not have a toString method.
                // TODO: This should be replaced with a more robust solution in the future.
                gie::Property ppt( 0, 0 );
                ppt.value = value;
                ImGui::Text( "Key: %s", gie::stringRegister().get( key ).data() );
                ImGui::Text( "Value: %s", ppt.toString().c_str() );
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
}  

void drawPlannerLogWindow( ExampleParameters& params )
{
    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation ) return;

    gie::Planner& planner = params.planner;

    if( ImGui::Begin( "Planner Log" ) )
    {
        const std::string& logContent = planner.logContent();

        if( logContent.empty() )
        {
            ImGui::Text( "No log content available." );
        }
        else
        {
            ImGui::TextUnformatted( logContent.c_str() );
        }
    }
    ImGui::End();
}

void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params )
{
    drawGoapieVisualizationWindow( useHeuristics, params );
    drawWorldViewWindow( params.world, params.planner );
    drawDebugMessagesWindow( params );
    drawSimulationArgumentsWindow( params );
    drawPlannerLogWindow( params );
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

                    if( g_ShowWaypointArrows )
                    {
                        // Draw an arrowhead pointing to the linked waypoint
                        // Using a small triangle at the end of the line oriented along the link direction (2D on XY)
                        glm::vec2 start2{ scaledLocation.x, scaledLocation.y };
                        glm::vec2 end2{ scaledLinkedLocation.x, scaledLinkedLocation.y };
                        glm::vec2 dir = end2 - start2;
                        float len = glm::length( dir );
                        if( len > 1e-6f )
                        {
                            dir /= len; // normalize
                            glm::vec2 perp{ -dir.y, dir.x };

                            // sizes in clip-space units (tweak as desired)
                            const float arrowLength = 0.03f;
                            const float arrowWidth = 0.02f;

                            glm::vec2 base = end2 - dir * arrowLength; // base of the arrow near the tip
                            glm::vec2 left = base + perp * ( arrowWidth * 0.5f );
                            glm::vec2 right = base - perp * ( arrowWidth * 0.5f );

                            float z = scaledLinkedLocation.z;
                            glBegin( GL_TRIANGLES );
                            glVertex3f( end2.x, end2.y, z );
                            glVertex3f( left.x, left.y, z );
                            glVertex3f( right.x, right.y, z );
                            glEnd();
                        }
                    }
                }
            }
        }
    }
}

void drawWaypointsAndLinks( const gie::World& world, const gie::Planner& planner, DrawingLimits& drawingLimits )
{
    const gie::TagSet* waypointGuids = nullptr;
    const gie::Blackboard* context = &world.context();

    // in case we have a valid simulation selected, we use the simulation context
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation )
    {
        context = &selectedSimulation->context();
    }

    waypointGuids = context->entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
    if( !waypointGuids || waypointGuids->empty() )
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
    glPointSize( 10.0f );        // Set point size for waypoints
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

void drawTrees( const gie::World& world, const gie::Planner& planner, DrawingLimits& drawingLimits )
{
    const gie::TagSet* treeGuids = nullptr;
    const gie::Blackboard* context = &world.context();

    // in case we have a valid simulation selected, we use the simulation context
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation )
    {
        context = &selectedSimulation->context();
    }

    treeGuids = context->entityTagRegister().tagSet( { gie::stringHasher( "Tree" ) } );
    if( !treeGuids || treeGuids->empty() )
    {
        return; // No trees to draw
    }

    for( gie::Guid treeGuid : *treeGuids )
    {
        auto waypointEntity = context->entity( treeGuid );
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
        auto treeEntity = context->entity( treeGuid );
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

    // Reduce horizontal padding for tree nodes
    ImVec2 oldPadding = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 1.0f, oldPadding.y ) ); // 2.0f is a small horizontal padding

    // Reduce indentation for child nodes
    float oldIndent = ImGui::GetStyle().IndentSpacing;
    ImGui::PushStyleVar( ImGuiStyleVar_IndentSpacing, 6.0f ); // child right padding

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

    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_SpanAvailWidth;
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

    ImGui::PopStyleVar( 2 ); // Restore previous padding
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
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, static_cast< GLsizei >( width ), static_cast< GLsizei >( height ), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_id, 0 );

    glBindRenderbuffer( GL_RENDERBUFFER, RBO );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast< GLsizei >( width ), static_cast< GLsizei >( height ) );
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, RBO );
}

void ShowExampleAppDockSpace(bool* p_open)
{
    // READ THIS !!!
    // TL;DR; this demo is more complicated than what most users you would normally use.
    // If we remove all options we are showcasing, this demo would become:
    //     void ShowExampleAppDockSpace()
    //     {
    //         ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    //     }
    // In most cases you should be able to just call DockSpaceOverViewport() and ignore all the code below!
    // In this specific demo, we are not using DockSpaceOverViewport() because:
    // - (1) we allow the host window to be floating/moveable instead of filling the viewport (when opt_fullscreen == false)
    // - (2) we allow the host window to have padding (when opt_padding == true)
    // - (3) we expose many flags and need a way to have them visible.
    // - (4) we have a local menu bar in the host window (vs. you could use BeginMainMenuBar() + DockSpaceOverViewport()
    //      in your code, but we don't here because we allow the window to be floating)

    static bool opt_fullscreen = true;
    static bool opt_padding = false;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    // because it would be confusing to have two docking targets within each others.
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if (opt_fullscreen)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }
    else
    {
        dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
    }

    // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
    // and handle the pass-thru hole, so we ask Begin() to not render a background.
    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;

    // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
    // all active windows docked into it will lose their parent and become undocked.
    // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
    // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
    if (!opt_padding)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace Demo", p_open, window_flags);
    if (!opt_padding)
        ImGui::PopStyleVar();

    if (opt_fullscreen)
        ImGui::PopStyleVar(2);

    // Submit the DockSpace
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Options"))
        {
            // Disabling fullscreen would allow the window to be moved to the front of other windows,
            // which we can't undo at the moment without finer window depth/z control.
            ImGui::MenuItem("Fullscreen", NULL, &opt_fullscreen);
            ImGui::MenuItem("Padding", NULL, &opt_padding);
            ImGui::Separator();

            if (ImGui::MenuItem("Flag: NoDockingOverCentralNode", "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingOverCentralNode) != 0)) { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingOverCentralNode; }
            if (ImGui::MenuItem("Flag: NoDockingSplit",         "", (dockspace_flags & ImGuiDockNodeFlags_NoDockingSplit) != 0))             { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingSplit; }
            if (ImGui::MenuItem("Flag: NoUndocking",            "", (dockspace_flags & ImGuiDockNodeFlags_NoUndocking) != 0))                { dockspace_flags ^= ImGuiDockNodeFlags_NoUndocking; }
            if (ImGui::MenuItem("Flag: NoResize",               "", (dockspace_flags & ImGuiDockNodeFlags_NoResize) != 0))                   { dockspace_flags ^= ImGuiDockNodeFlags_NoResize; }
            if (ImGui::MenuItem("Flag: AutoHideTabBar",         "", (dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar) != 0))             { dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar; }
            if (ImGui::MenuItem("Flag: PassthruCentralNode",    "", (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) != 0, opt_fullscreen)) { dockspace_flags ^= ImGuiDockNodeFlags_PassthruCentralNode; }
            ImGui::Separator();

            if (ImGui::MenuItem("Close", NULL, false, p_open != NULL))
                *p_open = false;
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    ImGui::End();
}

void drawSelectedSimulationPath( const gie::World& world, const gie::Planner& planner )
{
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( !selectedSimulation ) return;

    const auto* pathArg = selectedSimulation->arguments().get( "PathToTarget" );
    if( !pathArg ) return;

    const auto& pathGuids = std::get< std::vector< gie::Guid > >( *pathArg );
    if( pathGuids.size() <= 1 ) return;

    // recompute drawing transform to match drawWaypointsAndLinks/drawTrees
    const gie::Blackboard* contextBB = &world.context();
    contextBB = &selectedSimulation->context();

    // compute bounds again (same as tree/waypoint drawing)
    glm::vec3 minBounds( std::numeric_limits< float >::max() );
    glm::vec3 maxBounds( std::numeric_limits< float >::lowest() );
    auto waypointGuids = contextBB->entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
    if( waypointGuids )
    {
        for( auto guid : *waypointGuids )
        {
            auto e = world.entity( guid );
            if( auto loc = e->property( "Location" ) )
            {
                glm::vec3 p = *loc->getVec3();
                minBounds = glm::min( minBounds, p );
                maxBounds = glm::max( maxBounds, p );
            }
        }
    }
    auto treeGuids = contextBB->entityTagRegister().tagSet( { gie::stringHasher( "Tree" ) } );
    if( treeGuids )
    {
        for( auto guid : *treeGuids )
        {
            auto e = contextBB->entity( guid );
            if( auto loc = e->property( "Location" ) )
            {
                glm::vec3 p = *loc->getVec3();
                minBounds = glm::min( minBounds, p );
                maxBounds = glm::max( maxBounds, p );
            }
        }
    }

    glm::vec3 range = maxBounds - minBounds;
    glm::vec3 minB = minBounds - range * 0.1f;
    glm::vec3 maxB = maxBounds + range * 0.1f;
    glm::vec3 offset = -( minB + maxB ) * 0.5f;
    glm::vec3 scale = 2.0f / ( maxB - minB );

    // draw magenta path with thicker lines
    glLineWidth( 3.0f );
    glColor3f( 1.0f, 0.0f, 1.0f );
    glBegin( GL_LINE_STRIP );
    for( auto guid : pathGuids )
    {
        auto e = world.entity( guid );
        if( auto loc = e->property( "Location" ) )
        {
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
    }
    glEnd();

    auto drawArrowAtMid = []( const glm::vec3& a, const glm::vec3& b )
    {
        glm::vec2 a2{ a.x, a.y };
        glm::vec2 b2{ b.x, b.y };
        glm::vec2 dir = b2 - a2;
        float len = glm::length( dir );
        if( len <= 1e-6f ) return;
        dir /= len;
        glm::vec2 perp{ -dir.y, dir.x };

        // Arrow centered near the middle of the segment
        const float arrowLen = 0.03f;
        const float arrowWidth = 0.025f;
        glm::vec2 mid = ( a2 + b2 ) * 0.5f;
        glm::vec2 tip = mid + dir * arrowLen * 0.5f;
        glm::vec2 base = mid - dir * arrowLen * 0.5f;
        glm::vec2 left = base + perp * ( arrowWidth * 0.5f );
        glm::vec2 right = base - perp * ( arrowWidth * 0.5f );

        float z = ( a.z + b.z ) * 0.5f;
        glBegin( GL_TRIANGLES );
        glVertex3f( tip.x, tip.y, z );
        glVertex3f( left.x, left.y, z );
        glVertex3f( right.x, right.y, z );
        glEnd();
    };

    // draw arrows at mid of each path segment
    glColor3f( 1.0f, 0.0f, 1.0f );
    for( size_t i = 1; i < pathGuids.size(); ++i )
    {
        auto e0 = world.entity( pathGuids[ i - 1 ] );
        auto e1 = world.entity( pathGuids[ i ] );
        if( !e0 || !e1 ) continue;
        auto l0 = e0->property( "Location" );
        auto l1 = e1->property( "Location" );
        if( !l0 || !l1 ) continue;
        glm::vec3 a = ( *l0->getVec3() + offset ) * scale;
        glm::vec3 b = ( *l1->getVec3() + offset ) * scale;
        drawArrowAtMid( a, b );
    }

    // draw segment from agent to first waypoint in the path (if available)
    if( !pathGuids.empty() )
    {
        // use simulation context agent location (not world context)
        auto agentEntity = contextBB->entity( planner.agent()->guid() );
        if( agentEntity )
        {
            auto agentLocPpt = agentEntity->property( "Location" );
            if( agentLocPpt )
            {
                auto firstWp = world.entity( pathGuids.front() );
                if( auto wpLoc = firstWp->property( "Location" ) )
                {
                    glm::vec3 a = ( *agentLocPpt->getVec3() + offset ) * scale;
                    glm::vec3 b = ( *wpLoc->getVec3() + offset ) * scale;
                    glBegin( GL_LINES );
                    glVertex3f( a.x, a.y, a.z );
                    glVertex3f( b.x, b.y, b.z );
                    glEnd();

                    // arrow for this segment
                    drawArrowAtMid( a, b );
                }
            }
        }
    }

    // draw segment from last waypoint to tree target if available
    const auto* targetArg = selectedSimulation->arguments().get( "PathTarget" );
    if( targetArg && !pathGuids.empty() )
    {
        gie::Guid targetGuid = std::get< gie::Guid >( *targetArg );
        auto lastWp = world.entity( pathGuids.back() );
        auto targetEntity = contextBB->entity( targetGuid );
        if( lastWp && targetEntity )
        {
            auto lastLoc = lastWp->property( "Location" );
            auto targetLoc = targetEntity->property( "Location" );
            if( lastLoc && targetLoc )
            {
                glm::vec3 a = ( *lastLoc->getVec3() + offset ) * scale;
                glm::vec3 b = ( *targetLoc->getVec3() + offset ) * scale;
                glBegin( GL_LINES );
                glVertex3f( a.x, a.y, a.z );
                glVertex3f( b.x, b.y, b.z );
                glEnd();

                // arrow for this segment
                drawArrowAtMid( a, b );
            }
        }
    }

    // reset line width
    glLineWidth( 1.0f );
}

void drawAgentCrosshair( const gie::World& world, const gie::Planner& planner )
{
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( !selectedSimulation ) return;

    const gie::Blackboard* contextBB = &selectedSimulation->context();

    // compute bounds from waypoints and trees to match transform
    glm::vec3 minBounds( std::numeric_limits< float >::max() );
    glm::vec3 maxBounds( std::numeric_limits< float >::lowest() );

    auto waypointSet = contextBB->entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
    if( waypointSet )
    {
        for( auto guid : *waypointSet )
        {
            if( auto e = world.entity( guid ) )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    glm::vec3 p = *loc->getVec3();
                    minBounds = glm::min( minBounds, p );
                    maxBounds = glm::max( maxBounds, p );
                }
            }
        }
    }

    auto treeSet = contextBB->entityTagRegister().tagSet( { gie::stringHasher( "Tree" ) } );
    if( treeSet )
    {
        for( auto guid : *treeSet )
        {
            if( auto e = contextBB->entity( guid ) )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    glm::vec3 p = *loc->getVec3();
                    minBounds = glm::min( minBounds, p );
                    maxBounds = glm::max( maxBounds, p );
                }
            }
        }
    }

    glm::vec3 range = maxBounds - minBounds;
    glm::vec3 minB = minBounds - range * 0.1f;
    glm::vec3 maxB = maxBounds + range * 0.1f;
    glm::vec3 offset = -( minB + maxB ) * 0.5f;
    glm::vec3 scale = 2.0f / ( maxB - minB );

    // get agent location from simulation context
    auto agentEntity = contextBB->entity( planner.agent()->guid() );
    if( !agentEntity ) return;
    auto agentLocPpt = agentEntity->property( "Location" );
    if( !agentLocPpt ) return;

    glm::vec3 p = ( *agentLocPpt->getVec3() + offset ) * scale;

    // draw orange crosshair
    glColor3f( 1.0f, 0.5f, 0.0f );
    const float half = 0.03f; // half-size in clip units
    glLineWidth( 2.0f );
    glBegin( GL_LINES );
    // horizontal
    glVertex3f( p.x - half, p.y, p.z );
    glVertex3f( p.x + half, p.y, p.z );
    // vertical
    glVertex3f( p.x, p.y - half, p.z );
    glVertex3f( p.x, p.y + half, p.z );
    glEnd();
    glLineWidth( 1.0f );
}
