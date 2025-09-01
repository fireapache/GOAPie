/*
* creates a window to draw GOAPie world context states
*/

// clang-format off
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
// clang-format on

#include <goapie.h>
#include <persistency.h>

#include <glm/glm.hpp>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <limits>

#include "example.h"

// our window dimensions
const GLuint WIDTH = 1280;
const GLint HEIGHT = 920;

// global defined indices for OpenGL
GLuint VAO;          // vertex array object
GLuint VBO;          // vertex buffer object
GLuint FBO;          // frame buffer object
GLuint RBO;          // rendering buffer object
GLuint texture_id;   // the texture id we'll need later to create a texture

/** Drawing limits structure used for calculating bounds and scaling of the drawn world elements. */
struct DrawingLimits
{
    glm::vec3 minBounds{ std::numeric_limits< float >::max() }; /**< Minimum bounds of the drawing area. */
    glm::vec3 maxBounds{ std::numeric_limits< float >::lowest() }; /**< Maximum bounds of the drawing area. */
	glm::vec3 range{ 0.f }; /**< Range of the drawing area. */
	glm::vec3 center{ 0.f }; /**< Center point of the drawing area. */
	glm::vec3 scale{ 1.f }; /**< Scale factor for transforming world coordinates to clip space. */
    const float margin = 0.1f; // 10% margin
};
static DrawingLimits g_DrawingLimits; /**< Global instance of DrawingLimits used for the entire visualization. */
static bool g_DrawingLimitsInitialized = false;
static bool g_BoundsEditorVisible = false;
static float g_BoundsInputX[ 2 ] = { 0.0f, 0.0f }; // [min, max]
static float g_BoundsInputY[ 2 ] = { 0.0f, 0.0f }; // [min, max]

// Add this at the top of your file or in a suitable scope
static gie::Guid selectedSimulationGuid = gie::NullGuid; /**< GUID of the currently selected simulation. */
// Global toggle to show waypoint GUID suffixes
static bool g_ShowWaypointGuidSuffix = false;
// Global toggle to show arrowheads on waypoint links
static bool g_ShowWaypointArrows = false;
// Path-stepping visualization state
static bool g_PathStepMode = false;              // render path steps instead of final path
static int g_PathStepIndex = 0;                  // current step index
static gie::Guid g_PathStepSimGuid = gie::NullGuid; // simulation guid bound to the current step session
// Tools -> window visibility toggles (all false so dock space is empty at first launch)
static bool g_ShowDebugPathWindow = false;
static bool g_ShowGoapieVisualizationWindow = false;
static bool g_ShowWorldViewWindow = false;
static bool g_ShowDebugMessagesWindow = false;
static bool g_ShowSimulationArgumentsWindow = false;
static bool g_ShowPlannerLogWindow = false;
static bool g_ShowBlackboardPropertiesWindow = false;
static bool g_ShowMorePlannerOptions = false;
// NEW: Waypoint editor tool window (only when visible interactions are enabled)
static bool g_ShowWaypointEditorWindow = false;
// NEW: Waypoint editor state
static gie::Guid g_WaypointEditSelectedGuid = gie::NullGuid;
static float g_WaypointPickRadiusPx = 14.0f;
// NEW: Global pointers to world/planner so World View (const refs) can still interact when editor visible
static gie::World* g_WorldPtr = nullptr;
static gie::Planner* g_PlannerPtr = nullptr;
// NEW: Double-click to arm waypoint repositioning
static bool g_WaypointEditPlaceArmed = false;
// NEW: Target preview when move is armed
static glm::vec3 g_WaypointEditTargetWorldPos{ 0.f, 0.f, 0.f };
static bool g_WaypointEditHasTargetWorldPos = false;
// NEW: Dragging state for moving waypoint with same click used to select
static bool g_WaypointDragActive = false;
static float g_WaypointDragZ = 0.0f;
// NEW: begin moving only after threshold exceeded
static bool g_WaypointDragMoving = false;
static float g_WaypointDragStartLocalX = 0.0f;
static float g_WaypointDragStartLocalY = 0.0f;

// Helper: reset waypoint editor tool state when window closes
static void ResetWaypointEditorState()
{
    g_WaypointEditSelectedGuid = gie::NullGuid;
    g_WaypointEditPlaceArmed = false;
    g_WaypointEditHasTargetWorldPos = false;
    g_WaypointDragActive = false;
}

// UI settings persistence
static const char* kUiWindowsSettingsFile = "goapie_ui_windows.ini";
static void LoadWindowVisibilitySettings()
{
    std::ifstream in( kUiWindowsSettingsFile );
    if( !in.good() ) return; // first run or no file
    std::string line;
    auto parseBool = []( const std::string& s ) -> bool
    {
        return s == "1" || s == "true" || s == "True" || s == "TRUE" || s == "yes" || s == "on";
    };
    while( std::getline( in, line ) )
    {
        if( line.empty() || line[ 0 ] == '#' ) continue;
        size_t eq = line.find( '=' );
        if( eq == std::string::npos ) continue;
        std::string key = line.substr( 0, eq );
        std::string val = line.substr( eq + 1 );
        if( key == "ShowDebugPathWindow" ) g_ShowDebugPathWindow = parseBool( val );
        else if( key == "ShowGoapieVisualizationWindow" ) g_ShowGoapieVisualizationWindow = parseBool( val );
        else if( key == "ShowWorldViewWindow" ) g_ShowWorldViewWindow = parseBool( val );
        else if( key == "ShowDebugMessagesWindow" ) g_ShowDebugMessagesWindow = parseBool( val );
        else if( key == "ShowSimulationArgumentsWindow" ) g_ShowSimulationArgumentsWindow = parseBool( val );
        else if( key == "ShowPlannerLogWindow" ) g_ShowPlannerLogWindow = parseBool( val );
        else if( key == "ShowBlackboardPropertiesWindow" ) g_ShowBlackboardPropertiesWindow = parseBool( val );
        else if( key == "ShowWaypointEditorWindow" ) g_ShowWaypointEditorWindow = parseBool( val );
    }
}
static void SaveWindowVisibilitySettings()
{
    std::ofstream out( kUiWindowsSettingsFile, std::ios::trunc );
    if( !out.good() ) return;
    out << "# GOAPie UI windows visibility\n";
    out << "ShowDebugPathWindow=" << ( g_ShowDebugPathWindow ? 1 : 0 ) << '\n';
    out << "ShowGoapieVisualizationWindow=" << ( g_ShowGoapieVisualizationWindow ? 1 : 0 ) << '\n';
    out << "ShowWorldViewWindow=" << ( g_ShowWorldViewWindow ? 1 : 0 ) << '\n';
    out << "ShowDebugMessagesWindow=" << ( g_ShowDebugMessagesWindow ? 1 : 0 ) << '\n';
    out << "ShowSimulationArgumentsWindow=" << ( g_ShowSimulationArgumentsWindow ? 1 : 0 ) << '\n';
    out << "ShowPlannerLogWindow=" << ( g_ShowPlannerLogWindow ? 1 : 0 ) << '\n';
    out << "ShowBlackboardPropertiesWindow=" << ( g_ShowBlackboardPropertiesWindow ? 1 : 0 ) << '\n';
    out << "ShowWaypointEditorWindow=" << ( g_ShowWaypointEditorWindow ? 1 : 0 ) << '\n';
}

void bind_framebuffer();
void create_framebuffer();
void drawAgentCrosshair( const gie::World& world, const gie::Planner& planner );
void drawBlackboardPropertiesWindow( const gie::Simulation* simulation );
void drawDebugPathWindow( ExampleParameters& params );
void drawHeistOverlays( const gie::World& world, const gie::Planner& planner );
void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params );
void drawSelectedSimulationPath( const gie::World& world, const gie::Planner& planner );
void drawSimulationTreeView( const gie::Planner& planner, const gie::Simulation* simulation );
void drawTrees( const gie::World& world, const gie::Planner& planner );
void drawWaypointGuidSuffixOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 windowPos, float windowWidth, float windowHeight );
void drawWaypointsAndLinks( const gie::World& world, const gie::Planner& planner );
void drawWorldViewWindow( gie::World& world, const gie::Planner& planner );
void framebuffer_size_callback( GLFWwindow* window, int width, int height );
void processInput( GLFWwindow* window );
void rescale_framebuffer( float width, float height );
void ShowExampleAppDockSpace( bool* p_open );
void unbind_framebuffer();
void updateDrawingBounds( const gie::World& world );
// NEW: draw discovered room walls and their names in World View
void drawDiscoveredRoomsWalls( const gie::World& world, const gie::Planner& planner );
void drawRoomNamesOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 pos, float windowWidth, float windowHeight );
// NEW: Waypoint editor window and interactions on World View
void drawWaypointEditorWindow( gie::World& world, gie::Planner& planner );
static void handleWaypointEditorOnWorldView( ImVec2 pos, float windowWidth, float windowHeight );
static void drawWaypointEditorOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight );

/** @file
 * Creates a window to draw GOAPie world context states
 */

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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable docking support
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL( window, true );
    ImGui_ImplOpenGL3_Init( "#version 130" );

    // Load persistent UI windows visibility
    LoadWindowVisibilitySettings();

    bool useHeuristics = true;

    // Expose world/planner to editor interactions inside World View
    g_WorldPtr = &world;
    g_PlannerPtr = &planner;

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
        updateDrawingBounds( world );
        drawDiscoveredRoomsWalls( world, planner ); // NEW: draw discovered rooms first
        drawWaypointsAndLinks( world, planner );
        drawHeistOverlays( world, planner );
        drawTrees( world, planner );
        drawSelectedSimulationPath( world, planner );
        drawAgentCrosshair( world, planner );

        // and unbind it again
        unbind_framebuffer();

        glfwSwapBuffers( window );
        glfwPollEvents();
    }

    // Save UI windows visibility
    SaveWindowVisibilitySettings();

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

void drawWorldViewWindow( gie::World& world, const gie::Planner& planner )
{
    if( !g_ShowWorldViewWindow ) return;

    if( ImGui::Begin( "World View", &g_ShowWorldViewWindow ) )
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

        // NEW: Top-left overlay: Update Bounds button OR Bounds editor panel
        ImGui::SetCursorScreenPos( ImVec2( pos.x + 8.0f, pos.y + 8.0f ) );
        static float s_SaveMsgTimer = 0.0f;
        static float s_LoadMsgTimer = 0.0f;
        if( !g_BoundsEditorVisible )
        {
            if( ImGui::Button( "Update Bounds" ) )
            {
                // Ensure we have initial bounds on first open
                if( !g_DrawingLimitsInitialized )
                {
                    updateDrawingBounds( world );
                }
                // Initialize inputs from current limits
                g_BoundsInputX[ 0 ] = g_DrawingLimits.minBounds.x;
                g_BoundsInputX[ 1 ] = g_DrawingLimits.maxBounds.x;
                g_BoundsInputY[ 0 ] = g_DrawingLimits.minBounds.y;
                g_BoundsInputY[ 1 ] = g_DrawingLimits.maxBounds.y;
                g_BoundsEditorVisible = true;
            }
            ImGui::SameLine();
            if( ImGui::Button( "Save" ) )
            {
                // Save world.json next to executable
                if( gie::persistency::SaveWorldToJson( world, "world.json" ) )
                {
                    s_SaveMsgTimer = 2.0f; // show feedback for 2 seconds
                }
            }
            ImGui::SameLine();
            if( ImGui::Button( "Load" ) )
            {
                if( gie::persistency::LoadWorldFromJson( world, "world.json" ) )
                {
                    // Reset bounds so they recompute with new content
                    g_DrawingLimitsInitialized = false;
                    s_LoadMsgTimer = 2.0f;
                }
            }
            if( s_SaveMsgTimer > 0.0f )
            {
                ImGui::SameLine();
                ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "Saved" );
                s_SaveMsgTimer -= ImGui::GetIO().DeltaTime;
            }
            if( s_LoadMsgTimer > 0.0f )
            {
                ImGui::SameLine();
                ImGui::TextColored( ImVec4( 0.2f, 0.6f, 1.0f, 1.0f ), "Loaded" );
                s_LoadMsgTimer -= ImGui::GetIO().DeltaTime;
            }
        }
        else
        {
            ImGui::PushStyleVar( ImGuiStyleVar_Alpha, 0.95f );
            ImGui::BeginChild( "##BoundsPanel", ImVec2( 300, 0 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
            ImGui::TextUnformatted( "Bounds" );
            ImGui::Separator();
            ImGui::InputFloat2( "X Min/Max", g_BoundsInputX, "%.3f" );
            ImGui::InputFloat2( "Y Min/Max", g_BoundsInputY, "%.3f" );

            // determine if inputs differ from current used bounds
            bool differs = false;
            if( g_DrawingLimitsInitialized )
            {
                auto nearly = []( float a, float b )
                {
                    return std::fabs( a - b ) <= 1e-5f;
                };
                differs = !nearly( g_BoundsInputX[ 0 ], g_DrawingLimits.minBounds.x ) ||
                          !nearly( g_BoundsInputX[ 1 ], g_DrawingLimits.maxBounds.x ) ||
                          !nearly( g_BoundsInputY[ 0 ], g_DrawingLimits.minBounds.y ) ||
                          !nearly( g_BoundsInputY[ 1 ], g_DrawingLimits.maxBounds.y );
            }

            // Apply and Cancel buttons enabled only when values differ
            ImGui::BeginDisabled( !differs );
            if( ImGui::Button( "Apply" ) )
            {
                // enforce min <= max by swapping if needed
                if( g_BoundsInputX[ 0 ] > g_BoundsInputX[ 1 ] ) std::swap( g_BoundsInputX[ 0 ], g_BoundsInputX[ 1 ] );
                if( g_BoundsInputY[ 0 ] > g_BoundsInputY[ 1 ] ) std::swap( g_BoundsInputY[ 0 ], g_BoundsInputY[ 1 ] );

                // apply to drawing limits (preserve Z bounds)
                g_DrawingLimits.minBounds.x = g_BoundsInputX[ 0 ];
                g_DrawingLimits.maxBounds.x = g_BoundsInputX[ 1 ];
                g_DrawingLimits.minBounds.y = g_BoundsInputY[ 0 ];
                g_DrawingLimits.maxBounds.y = g_BoundsInputY[ 1 ];

                // recompute derived values
                g_DrawingLimits.range = g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds;
                g_DrawingLimits.center = ( g_DrawingLimits.maxBounds + g_DrawingLimits.minBounds ) * 0.5f;
                g_DrawingLimits.scale = 2.0f / ( g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds );
                g_DrawingLimits.scale.z = 1.0f;

                // lock remains enabled
                g_DrawingLimitsInitialized = true;

                // hide panel
                g_BoundsEditorVisible = false;
            }
			ImGui::EndDisabled();
            ImGui::SameLine();
            if( ImGui::Button( "Cancel" ) )
            {
                // revert input fields to current used bounds and hide panel
                if( g_DrawingLimitsInitialized )
                {
                    g_BoundsInputX[ 0 ] = g_DrawingLimits.minBounds.x;
                    g_BoundsInputX[ 1 ] = g_DrawingLimits.maxBounds.x;
                    g_BoundsInputY[ 0 ] = g_DrawingLimits.minBounds.y;
                    g_BoundsInputY[ 1 ] = g_DrawingLimits.maxBounds.y;
                }
                g_BoundsEditorVisible = false;
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        // Draw waypoint id suffix overlay (extracted)
        drawWaypointGuidSuffixOverlay( world, planner, pos, windowWidth, windowHeight );
        // NEW: room names overlay
        drawRoomNamesOverlay( world, planner, pos, windowWidth, windowHeight );
        // NEW: waypoint editor interactions and overlay (active only if tool window is visible)
        if( g_ShowWaypointEditorWindow )
        {
            handleWaypointEditorOnWorldView( pos, windowWidth, windowHeight );
            drawWaypointEditorOverlayOnWorldView( pos, windowWidth, windowHeight );
        }
    }
    ImGui::End();
}

void drawWaypointGuidSuffixOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 pos, float windowWidth, float windowHeight )
{
    if( !g_ShowWaypointGuidSuffix ) return;

    const gie::Blackboard* worldContext = &world.context();
    if( const auto* sim = planner.simulation( selectedSimulationGuid ) )
    {
        worldContext = &sim->context();
    }
    const auto* waypointSet = worldContext->entityTagRegister().tagSet( { "Waypoint" } );

    if( waypointSet && !waypointSet->empty() )
    {
        // Use global drawing limits
        glm::vec3 minB = g_DrawingLimits.minBounds;
        glm::vec3 maxB = g_DrawingLimits.maxBounds;
        glm::vec3 offset = -g_DrawingLimits.center;
        glm::vec3 scale = g_DrawingLimits.scale;

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
                    ImGui::GetWindowDrawList()->AddText( ImVec2( suffixPos.x + 1, suffixPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), buf );
                    ImGui::GetWindowDrawList()->AddText( suffixPos, IM_COL32( 255, 255, 0, 255 ), buf );

                    // Derive waypoint index from entity name, expected to be "waypoint<number>"
                    std::string idxLabel = "wp?";
                    auto nameHash = e->nameHash();
                    if( nameHash != gie::InvalidStringHash )
                    {
                        std::string name( gie::stringRegister().get( nameHash ) );
                        size_t j = name.size();
                        if( name.find( "waypoint" ) == 0 )
                        {
						while( j > 0 && std::isdigit( static_cast< unsigned char >( name[ j - 1 ] ) ) )
						{
							--j;
						}
						if( j < name.size() )
						{
							idxLabel = std::string( "wp" ) + name.substr( j );
						}
                        }
                        else
                        {
						idxLabel = name; // fallback to full name if not matching expected pattern
                        }
                        
                    }

                    // Position index label above the suffix and center align
                    ImVec2 idxSize = ImGui::CalcTextSize( idxLabel.c_str() );
                    ImVec2 idxPos{ pos.x + x_px - idxSize.x * 0.5f, suffixPos.y - ( idxSize.y + 2.0f ) };

                    // draw index label (white) with shadow
                    ImGui::GetWindowDrawList()->AddText( ImVec2( idxPos.x + 1, idxPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), idxLabel.c_str() );
                    ImGui::GetWindowDrawList()->AddText( idxPos, IM_COL32( 255, 255, 255, 255 ), idxLabel.c_str() );
                }
            }
        }
    }
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
    if( !g_ShowBlackboardPropertiesWindow ) return;
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

    if( ImGui::Begin( "Blackboard Properties", &g_ShowBlackboardPropertiesWindow ) )
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
    if( !g_ShowGoapieVisualizationWindow ) return;

    gie::World& world = params.world;
    gie::Planner& planner = params.planner;
	static int simultationDepth = 10;

    if( ImGui::Begin( "GOAPie Visualization", &g_ShowGoapieVisualizationWindow ) )
    {
        ImGui::Checkbox( "Use Heuristics", &useHeuristics );
        ImGui::Checkbox( "Log Plan", &planner.logStepsMutator() );
		ImGui::SliderInt( "Depth Limit", &simultationDepth, 10, 50 );
        planner.depthLimitMutator() = static_cast< size_t >( simultationDepth );

        ImGui::Separator();
        if( ImGui::Button( g_ShowMorePlannerOptions ? "Less" : "More" ) )
        {
            g_ShowMorePlannerOptions = !g_ShowMorePlannerOptions;
        }
        if( g_ShowMorePlannerOptions )
        {
            ImGui::Indent();
            ImGui::Checkbox( "Step Plan", &planner.stepMutator() );
            ImGui::Checkbox( "Show Waypoint IDs (last 4)", &g_ShowWaypointGuidSuffix );
            ImGui::Checkbox( "Show Waypoint Arrows", &g_ShowWaypointArrows );
            ImGui::Unindent();
        }
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

            // separate window, toggled via Tools menu
            drawBlackboardPropertiesWindow( selectedSim );
        }
    }
    ImGui::End();
}

void drawDebugMessagesWindow( ExampleParameters& params )
{
    if( !g_ShowDebugMessagesWindow ) return;

    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation )
    {
        if( ImGui::Begin( "Debug Messages", &g_ShowDebugMessagesWindow ) )
        {
            ImGui::TextUnformatted( "No simulation selected." );
        }
        ImGui::End();
        return;
    }

    const auto& debugMessages = selectedSimulation->debugMessages();

    if( ImGui::Begin( "Debug Messages", &g_ShowDebugMessagesWindow ) )
    {
        if( !debugMessages.messages() || debugMessages.messages()->empty() )
        {
            ImGui::Text( "No debug messages available." );
        }
        else
        {
            for( const auto& message : *debugMessages.messages() )
            {
				const bool hasScope = message.find( "::" ) != std::string::npos;
				if( hasScope )
                {
					ImGui::TextColored( ImColor{ 1.0f, 0.6f, 0.0f, 1.0f }, "* %s", message.c_str() );
				}
                else
                {
					ImGui::TextUnformatted( message.c_str() );
                }
				
            }
        }
    }
    ImGui::End();
}

void drawSimulationArgumentsWindow( ExampleParameters& params )
{  
    if( !g_ShowSimulationArgumentsWindow ) return;

    const gie::Simulation* selectedSimulation = params.planner.simulation( selectedSimulationGuid );

    if( !selectedSimulation )
    {
        if( ImGui::Begin( "Simulation Arguments", &g_ShowSimulationArgumentsWindow ) )
        {
            ImGui::TextUnformatted( "No simulation selected." );
        }
        ImGui::End();
        return;
    }

    const auto& arguments = selectedSimulation->arguments();

    if( ImGui::Begin( "Simulation Arguments", &g_ShowSimulationArgumentsWindow ) )
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
    if( !g_ShowPlannerLogWindow ) return;

    gie::Planner& planner = params.planner;

    if( ImGui::Begin( "Planner Log", &g_ShowPlannerLogWindow ) )
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

void drawDebugPathWindow( ExampleParameters& params )
{
    if( !g_ShowDebugPathWindow ) return;

    gie::Planner& planner = params.planner;
    const gie::Simulation* selectedSim = planner.simulation( selectedSimulationGuid );

    if( ImGui::Begin( "Debug Path", &g_ShowDebugPathWindow ) )
    {
        if( !selectedSim )
        {
            ImGui::TextUnformatted( "No simulation selected. Select a node in 'Simulation Nodes'." );
        }
        else
        {
            const auto* openedOffsetsArg = selectedSim->arguments().get( "PF_OpenedOffsets" );
            const auto* visitedOffsetsArg = selectedSim->arguments().get( "PF_VisitedOffsets" );
            const auto* backtrackOffsetsArg = selectedSim->arguments().get( "PF_BacktrackOffsets" );
            if( openedOffsetsArg && visitedOffsetsArg && backtrackOffsetsArg )
            {
                const auto& openedOffsets = std::get< gie::Property::IntegerVector >( *openedOffsetsArg );
                int statesCount = static_cast<int>( openedOffsets.size() > 0 ? openedOffsets.size() - 1 : 0 );

                ImGui::Text( "Path Steps: %d", statesCount );
                if( statesCount > 0 )
                {
                    if( ImGui::Button( "Step Path" ) )
                    {
                        if( !g_PathStepMode || g_PathStepSimGuid != selectedSim->guid() )
                        {
                            // start stepping for this simulation
                            g_PathStepMode = true;
                            g_PathStepIndex = 0;
                            g_PathStepSimGuid = selectedSim->guid();
                        }
                        else
                        {
                            // advance or reset
                            if( g_PathStepIndex + 1 < statesCount )
                            {
                                g_PathStepIndex++;
                            }
                            else
                            {
                                // reached last state -> reset/turn off
                                g_PathStepMode = false;
                                g_PathStepIndex = 0;
                                g_PathStepSimGuid = gie::NullGuid;
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text( "State: %d / %d", g_PathStepMode && g_PathStepSimGuid == selectedSim->guid() ? ( g_PathStepIndex + 1 ) : 0, statesCount );
                }
                else
                {
                    ImGui::TextUnformatted( "No step states available." );
                }
            }
            else
            {
                ImGui::TextUnformatted( "Selected simulation has no path step data." );
            }
        }
    }
    ImGui::End();
}

void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params )
{
    // Detect closing of Waypoint Editor window and reset state
    static bool s_prevShowWaypointEditorWindow = false;
    if( s_prevShowWaypointEditorWindow && !g_ShowWaypointEditorWindow )
    {
        ResetWaypointEditorState();
    }
    s_prevShowWaypointEditorWindow = g_ShowWaypointEditorWindow;

    drawGoapieVisualizationWindow( useHeuristics, params );
    drawWorldViewWindow( params.world, params.planner );
    drawDebugMessagesWindow( params );
    drawSimulationArgumentsWindow( params );
    drawPlannerLogWindow( params );
    drawDebugPathWindow( params );
    // NEW: Waypoint editor tool window
    drawWaypointEditorWindow( params.world, params.planner );
}

void drawLinks(
    const gie::Entity* waypointEntity,
    const gie::World& world,
    const glm::vec3& offset,
    const glm::vec3& scale,
    const glm::vec3& scaledLocation,
    bool isSelectedWaypoint )
{
    // Draw links if they exist
    if( auto linksPpt = waypointEntity->property( "Links" ) )
    {
        auto linkedGuids = linksPpt->getGuidArray();
        if( linkedGuids )
        {
            // brighter color for selected waypoint links
            if( isSelectedWaypoint ) glColor3f( 0.85f, 0.85f, 0.85f );
            else                     glColor3f( 0.5f,  0.5f,  0.5f  );
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

                    // Show arrowheads if globally enabled OR if this waypoint is selected
                    if( g_ShowWaypointArrows || isSelectedWaypoint )
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

                            // Arrow at the linked end (pointing from start->end)
                            glm::vec2 base = end2 - dir * arrowLength; // base of the arrow near the tip
                            glm::vec2 left = base + perp * ( arrowWidth * 0.5f );
                            glm::vec2 right = base - perp * ( arrowWidth * 0.5f );
                            float zTip = scaledLinkedLocation.z;
                            glBegin( GL_TRIANGLES );
                            glVertex3f( end2.x, end2.y, zTip );
                            glVertex3f( left.x, left.y, zTip );
                            glVertex3f( right.x, right.y, zTip );
                            glEnd();

                            // If selected waypoint has a bidirectional link with this neighbor, draw reverse arrow at start
                            if( isSelectedWaypoint )
                            {
                                bool hasReverse = false;
                                if( auto neighborLinksPpt = linkedEntity->property( "Links" ) )
                                {
                                    if( auto neighborLinks = neighborLinksPpt->getGuidArray() )
                                    {
                                        hasReverse = std::find( neighborLinks->begin(), neighborLinks->end(), waypointEntity->guid() ) != neighborLinks->end();
                                    }
                                }
                                if( hasReverse )
                                {
                                    // Reverse direction arrow near the start (pointing to start)
                                    glm::vec2 dirRev = -dir;
                                    glm::vec2 baseS = start2 - dirRev * arrowLength; // base near the selected endpoint
                                    glm::vec2 leftS = baseS + perp * ( arrowWidth * 0.5f );
                                    glm::vec2 rightS = baseS - perp * ( arrowWidth * 0.5f );
                                    float zStart = scaledLocation.z;
                                    glBegin( GL_TRIANGLES );
                                    glVertex3f( start2.x, start2.y, zStart );
                                    glVertex3f( leftS.x, leftS.y, zStart );
                                    glVertex3f( rightS.x, rightS.y, zStart );
                                    glEnd();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void drawWaypointsAndLinks( const gie::World& world, const gie::Planner& planner )
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
        return;
    }

    // Use global drawing limits for transform
    glm::vec3 minBounds = g_DrawingLimits.minBounds;
    glm::vec3 maxBounds = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

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

            bool isSelected = ( g_WaypointEditSelectedGuid != gie::NullGuid && waypointGuid == g_WaypointEditSelectedGuid );
            drawLinks( waypointEntity, world, offset, scale, scaledLocation, isSelected );
        }
    }
}

static bool isRoomName( std::string_view name )
{
    return name == "Garage" || name == "Kitchen" || name == "Corridor" || name == "LivingRoom" || name == "Bathroom" || name == "BedroomA" || name == "BedroomB";
}

void drawHeistOverlays( const gie::World& world, const gie::Planner& planner )
{
    const gie::Blackboard* context = &world.context();
    const gie::Simulation* selectedSimulation = planner.simulation( selectedSimulationGuid );
    if( selectedSimulation ) context = &selectedSimulation->context();

    // Transform matching the one used for waypoints using global limits
    glm::vec3 minBounds = g_DrawingLimits.minBounds;
    glm::vec3 maxBounds = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

    // Draw simple room rectangles around room waypoints
    const auto* waypointSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
    if( waypointSet && !waypointSet->empty() )
    {
        // choose room rectangle size in world units
        const float halfW = 4.0f, halfH = 3.0f;
        glm::vec3 halfWorld{ halfW, halfH, 0.0f };
        glm::vec3 half = halfWorld * scale;
        glColor3f( 0.2f, 0.7f, 0.9f );
        for( auto guid : *waypointSet )
        {
            auto e = world.entity( guid ); if( !e ) continue;
            auto nm = gie::stringRegister().get( e->nameHash() );
            if( !isRoomName( nm ) ) continue;
            auto loc = e->property( "Location" ); if( !loc ) continue;
            glm::vec3 c = ( *loc->getVec3() + offset ) * scale;
            glBegin( GL_LINE_LOOP );
            glVertex3f( c.x - half.x, c.y - half.y, c.z );
            glVertex3f( c.x + half.x, c.y - half.y, c.z );
            glVertex3f( c.x + half.x, c.y + half.y, c.z );
            glVertex3f( c.x - half.x, c.y + half.y, c.z );
            glEnd();
        }
    }

    // Draw connectors as colored lines between From-To and marker at connector location
    const auto* connectorSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Connector" ) } );
    if( connectorSet && !connectorSet->empty() )
    {
        // read alarm armed flag
        bool alarmArmed = false;
        for( const auto& [ eg, ent ] : context->entities() )
        {
            if( gie::stringRegister().get( ent.nameHash() ) == std::string_view( "AlarmSystem" ) )
            {
                if( auto a = context->entity( eg ) )
                {
                    if( auto p = a->property( "Armed" ) ) alarmArmed = *p->getBool();
                }
                break;
            }
        }

        glLineWidth( 2.0f );
        for( auto guid : *connectorSet )
        {
            auto c = context->entity( guid ); if( !c ) continue;
            bool locked = *c->property( "Locked" )->getBool();
            bool blocked = *c->property( "Blocked" )->getBool();
            bool barred = *c->property( "Barred" )->getBool();
            bool alarmed = *c->property( "Alarmed" )->getBool();
            bool blockedAny = locked || blocked || barred;

            if( blockedAny ) glColor3f( 1.0f, 0.2f, 0.2f );            // red
            else if( alarmArmed && alarmed ) glColor3f( 1.0f, 1.0f, 0.2f ); // yellow
            else glColor3f( 0.2f, 1.0f, 0.2f );                            // green

            auto fromG = *c->property( "From" )->getGuid();
            auto toG   = *c->property( "To" )->getGuid();
            auto fromE = context->entity( fromG ); if( !fromE ) fromE = world.entity( fromG );
            auto toE   = context->entity( toG );   if( !toE )   toE   = world.entity( toG );
            if( fromE && toE )
            {
                auto l0 = fromE->property( "Location" );
                auto l1 = toE->property( "Location" );
                if( l0 && l1 )
                {
                    glm::vec3 a = ( *l0->getVec3() + offset ) * scale;
                    glm::vec3 b = ( *l1->getVec3() + offset ) * scale;
                    glBegin( GL_LINES );
                    glVertex3f( a.x, a.y, a.z );
                    glVertex3f( b.x, b.y, b.z );
                    glEnd();
                }
            }

            // draw connector location marker as a small cross
            if( auto loc = c->property( "Location" ) )
            {
                glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
                const float s = 0.02f;
                glBegin( GL_LINES );
                glVertex3f( p.x - s, p.y, p.z ); glVertex3f( p.x + s, p.y, p.z );
                glVertex3f( p.x, p.y - s, p.z ); glVertex3f( p.x, p.y + s, p.z );
                glEnd();
            }
        }
        glLineWidth( 1.0f );
    }

    // Draw POIs: AlarmPanel, FuseBox, Safe
    gie::Entity const* alarmPanel = nullptr;
    gie::Entity const* fuseBox = nullptr;
    gie::Entity const* safe = nullptr;
    for( const auto& [ eg, ent ] : context->entities() )
    {
        auto name = gie::stringRegister().get( ent.nameHash() );
        if( name == "AlarmPanelEntity" ) alarmPanel = context->entity( eg );
        else if( name == "FuseBoxEntity" ) fuseBox = context->entity( eg );
        else if( name == "Safe" ) safe = context->entity( eg );
    }

    auto drawFilledQuad = []( const glm::vec3& c, float s )
    {
        glBegin( GL_QUADS );
        glVertex3f( c.x - s, c.y - s, c.z );
        glVertex3f( c.x + s, c.y - s, c.z );
        glVertex3f( c.x + s, c.y + s, c.z );
        glVertex3f( c.x - s, c.y + s, c.z );
        glEnd();
    };
    auto drawDiamond = []( const glm::vec3& c, float s )
    {
        glBegin( GL_TRIANGLES );
        glVertex3f( c.x, c.y - s, c.z ); glVertex3f( c.x - s, c.y, c.z ); glVertex3f( c.x + s, c.y, c.z );
        glVertex3f( c.x, c.y + s, c.z ); glVertex3f( c.x - s, c.y, c.z ); glVertex3f( c.x + s, c.y, c.z );
        glEnd();
    };

    const float poiSize = 0.02f;

    if( alarmPanel )
    {
        if( auto loc = alarmPanel->property( "Location" ) )
        {
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glColor3f( 0.2f, 0.6f, 1.0f );
            drawFilledQuad( p, poiSize );
        }
    }
    if( fuseBox )
    {
        if( auto loc = fuseBox->property( "Location" ) )
        {
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glColor3f( 1.0f, 0.6f, 0.2f );
            drawFilledQuad( p, poiSize );
        }
    }
    if( safe )
    {
        auto roomG = *const_cast< gie::Entity* >( safe )->property( "InRoom" )->getGuid();
        const auto* room = context->entity( roomG ); if( !room ) room = world.entity( roomG );
        if( room )
        {
            if( auto loc = room->property( "Location" ) )
            {
                glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
                glColor3f( 1.0f, 0.2f, 1.0f );
                drawDiamond( p, poiSize * 1.2f );
            }
        }
    }
}

void drawTrees( const gie::World& world, const gie::Planner& planner )
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

    // Use global drawing limits for transform
    glm::vec3 minBounds = g_DrawingLimits.minBounds;
    glm::vec3 maxBounds = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

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
        // reset path step mode when selecting a different simulation
        g_PathStepMode = false;
        g_PathStepIndex = 0;
        g_PathStepSimGuid = gie::NullGuid;
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

    ImGui::PopStyleVar( 2 );
}

// here we create our framebuffer and our renderbuffer
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

void updateDrawingBounds( const gie::World& world )
{
    // Avoid updating automatically once initialized
    if( g_DrawingLimitsInitialized ) return;

    const gie::Blackboard* worldContext = &world.context();

    glm::vec3 minBounds( std::numeric_limits< float >::max() );
    glm::vec3 maxBounds( std::numeric_limits< float >::lowest() );

    const auto* drawTagSet = worldContext->entityTagRegister().tagSet( { "Draw" } );
    const auto* waypointsTagSet = worldContext->entityTagRegister().tagSet( { "Waypoint" } );

    const std::set< gie::Guid >* setToUse = drawTagSet && !drawTagSet->empty() ? drawTagSet
                                             : ( waypointsTagSet && !waypointsTagSet->empty() ? waypointsTagSet : nullptr );
    if( !setToUse )
    {
        return; // no data to compute bounds
    }

    // calculating min/max bounds
    for( auto guid : *setToUse )
    {
        if( const auto* e = world.entity( guid ) )
        {
            // First check for Location property (existing behavior)
            if( const auto* loc = e->property( "Location" ) )
            {
                glm::vec3 p = *loc->getVec3();
                minBounds = glm::min( minBounds, p );
                maxBounds = glm::max( maxBounds, p );
            }
            
            // NEW: Also check for Vertices property to include room boundaries
            if( const auto* verticesPpt = e->property( "Vertices" ) )
            {
                const auto* verts = std::get_if< gie::Property::Vec3Vector >( &verticesPpt->value );
                if( verts && !verts->empty() )
                {
                    for( const auto& vertex : *verts )
                    {
                        minBounds = glm::min( minBounds, vertex );
                        maxBounds = glm::max( maxBounds, vertex );
                    }
                }
            }
        }
    }

    glm::vec3 range = maxBounds - minBounds;
    glm::vec3 center = ( maxBounds + minBounds ) * 0.5f;

    // add some margin
    minBounds = minBounds - range * g_DrawingLimits.margin;
    maxBounds = maxBounds + range * g_DrawingLimits.margin;

    // Calculate scale and ensure z-component is 1.0f
    glm::vec3 scale = 2.0f / ( maxBounds - minBounds );
    scale.z = 1.0f;

    g_DrawingLimits.minBounds = minBounds;
    g_DrawingLimits.maxBounds = maxBounds;
    g_DrawingLimits.center = center;
    g_DrawingLimits.range = range;
    g_DrawingLimits.scale = scale;

    // Lock further automatic updates
    g_DrawingLimitsInitialized = true;
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

void ShowExampleAppDockSpace( bool* p_open )
{
    static bool opt_fullscreen = true;
    static bool opt_padding = false;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if( opt_fullscreen )
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos( viewport->WorkPos );
        ImGui::SetNextWindowSize( viewport->WorkSize );
        ImGui::SetNextWindowViewport( viewport->ID );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }
    else
    {
        dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
    }

    if( dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode )
        window_flags |= ImGuiWindowFlags_NoBackground;

    if( !opt_padding )
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
    ImGui::Begin( "DockSpace Demo", p_open, window_flags );
    if( !opt_padding )
        ImGui::PopStyleVar();

    if( opt_fullscreen )
        ImGui::PopStyleVar( 2 );

    ImGuiIO& io = ImGui::GetIO();
    if( io.ConfigFlags & ImGuiConfigFlags_DockingEnable )
    {
        ImGuiID dockspace_id = ImGui::GetID( "MyDockSpace" );
        ImGui::DockSpace( dockspace_id, ImVec2( 0.0f, 0.0f ), dockspace_flags );
    }

    if( ImGui::BeginMenuBar() )
    {
        if( ImGui::BeginMenu( "Options" ) )
        {
            ImGui::MenuItem( "Fullscreen", NULL, &opt_fullscreen );
            ImGui::MenuItem( "Padding", NULL, &opt_padding );
            ImGui::Separator();

            if( ImGui::MenuItem( "Flag: NoDockingOverCentralNode", "", ( dockspace_flags & ImGuiDockNodeFlags_NoDockingOverCentralNode ) != 0 ) ) { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingOverCentralNode; }
            if( ImGui::MenuItem( "Flag: NoDockingSplit",         "", ( dockspace_flags & ImGuiDockNodeFlags_NoDockingSplit ) != 0 ) )             { dockspace_flags ^= ImGuiDockNodeFlags_NoDockingSplit; }
            if( ImGui::MenuItem( "Flag: NoUndocking",            "", ( dockspace_flags & ImGuiDockNodeFlags_NoUndocking ) != 0 ) )                { dockspace_flags ^= ImGuiDockNodeFlags_NoUndocking; }
            if( ImGui::MenuItem( "Flag: NoResize",               "", ( dockspace_flags & ImGuiDockNodeFlags_NoResize ) != 0 ) )                   { dockspace_flags ^= ImGuiDockNodeFlags_NoResize; }
            if( ImGui::MenuItem( "Flag: AutoHideTabBar",         "", ( dockspace_flags & ImGuiDockNodeFlags_AutoHideTabBar ) != 0 ) )             { dockspace_flags ^= ImGuiDockNodeFlags_AutoHideTabBar; }
            if( ImGui::MenuItem( "Flag: PassthruCentralNode",    "", ( dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode ) != 0, opt_fullscreen ) ) { dockspace_flags ^= ImGuiDockNodeFlags_PassthruCentralNode; }
            ImGui::Separator();

            if( ImGui::MenuItem( "Close", NULL, false, p_open != NULL ) )
                *p_open = false;
            ImGui::EndMenu();
        }

        if( ImGui::BeginMenu( "Tools" ) )
        {
            ImGui::MenuItem( "GOAPie Visualization", NULL, &g_ShowGoapieVisualizationWindow );
            ImGui::MenuItem( "World View", NULL, &g_ShowWorldViewWindow );
            ImGui::MenuItem( "Debug Messages", NULL, &g_ShowDebugMessagesWindow );
            ImGui::MenuItem( "Simulation Arguments", NULL, &g_ShowSimulationArgumentsWindow );
            ImGui::MenuItem( "Planner Log", NULL, &g_ShowPlannerLogWindow );
            ImGui::MenuItem( "Blackboard Properties", NULL, &g_ShowBlackboardPropertiesWindow );
            ImGui::Separator();
            ImGui::MenuItem( "Debug Path", NULL, &g_ShowDebugPathWindow );
            ImGui::Separator();
            // NEW: Waypoint Editor tool window toggler
            ImGui::MenuItem( "Waypoint Editor", NULL, &g_ShowWaypointEditorWindow );
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

    const gie::Blackboard* contextBB = &selectedSimulation->context();

    // Use global drawing limits
    glm::vec3 minB = g_DrawingLimits.minBounds;
    glm::vec3 maxB = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = 2.0f / ( maxB - minB );

    // Path steps available?
    const auto* openedArg = selectedSimulation->arguments().get( "PF_Opened" );
    const auto* visitedArg = selectedSimulation->arguments().get( "PF_Visited" );
    const auto* backsArg = selectedSimulation->arguments().get( "PF_Backtracks" );
    const auto* openedOffsetsArg = selectedSimulation->arguments().get( "PF_OpenedOffsets" );
    const auto* visitedOffsetsArg = selectedSimulation->arguments().get( "PF_VisitedOffsets" );
    const auto* backOffsetsArg = selectedSimulation->arguments().get( "PF_BacktrackOffsets" );

    bool hasStepData = openedArg && visitedArg && backsArg && openedOffsetsArg && visitedOffsetsArg && backOffsetsArg;

    // if stepping is enabled and data exists and bound to this sim, render step state
    if( hasStepData && g_PathStepMode && g_PathStepSimGuid == selectedSimulation->guid() )
    {
        const auto& openedAll = std::get< gie::Property::GuidVector >( *openedArg );
        const auto& visitedAll = std::get< gie::Property::GuidVector >( *visitedArg );
        const auto& backAll = std::get< gie::Property::GuidVector >( *backsArg );
        const auto& openedOff = std::get< gie::Property::IntegerVector >( *openedOffsetsArg );
        const auto& visitedOff = std::get< gie::Property::IntegerVector >( *visitedOffsetsArg );
        const auto& backOff = std::get< gie::Property::IntegerVector >( *backOffsetsArg );
        int statesCount = static_cast<int>( openedOff.size() > 0 ? openedOff.size() - 1 : 0 );
        if( statesCount <= 0 ) return;
        int s = std::min( std::max( g_PathStepIndex, 0 ), statesCount - 1 );

        int o0 = openedOff[ s ];
        int o1 = openedOff[ s + 1 ];
        int v0 = visitedOff[ s ];
        int v1 = visitedOff[ s + 1 ];
        int b0 = backOff[ s ];
        int b1 = backOff[ s + 1 ];

        // draw opened nodes (yellow)
        glPointSize( 9.0f );
        glColor3f( 1.0f, 1.0f, 0.0f );
        glBegin( GL_POINTS );
        for( int i = o0; i < o1; ++i )
        {
            auto e = world.entity( openedAll[ i ] );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
        glEnd();

        // draw visited nodes (cyan)
        glPointSize( 11.0f );
        glColor3f( 0.0f, 1.0f, 1.0f );
        glBegin( GL_POINTS );
        for( int i = v0; i < v1; ++i )
        {
            auto e = world.entity( visitedAll[ i ] );
            if( !e ) continue;
            auto loc = e->property( "Location" );
            if( !loc ) continue;
            glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
            glVertex3f( p.x, p.y, p.z );
        }
        glEnd();

        auto drawArrow = []( const glm::vec3& a, const glm::vec3& b )
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
            const float arrowWidth = 0.02f;
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

        // draw backtrack arrows (white) for every pair (node, backtrack)
        glColor3f( 1.0f, 1.0f, 1.0f );
        for( int i = b0; i + 1 < b1; i += 2 )
        {
            auto node = world.entity( backAll[ i ] );
            auto prev = world.entity( backAll[ i + 1 ] );
            if( !node || !prev ) continue;
            auto nLoc = node->property( "Location" );
            auto pLoc = prev->property( "Location" );
            if( !nLoc || !pLoc ) continue;
            glm::vec3 a = ( *pLoc->getVec3() + offset ) * scale; // arrow from prev -> node
            glm::vec3 b = ( *nLoc->getVec3() + offset ) * scale;
            glBegin( GL_LINES );
            glVertex3f( a.x, a.y, a.z );
            glVertex3f( b.x, b.y, b.z );
            glEnd();
            drawArrow( a, b );
        }

        // done with step view
        return;
    }

    // otherwise: draw final computed path if available
    const auto* pathArg = selectedSimulation->arguments().get( "PathToTarget" );
    if( !pathArg ) return;

    const auto& pathGuids = std::get< std::vector< gie::Guid > >( *pathArg );
    if( pathGuids.size() <= 1 ) return;

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
    const auto agentStartLocationArgument = selectedSimulation->arguments().get( "AgentStartLocation" );
    if( !pathGuids.empty() && agentStartLocationArgument )
    {
        auto agentStartLocPpt = std::get< glm::vec3 >( *agentStartLocationArgument );
        auto firstWp = world.entity( pathGuids.front() );
        if( auto wpLoc = firstWp->property( "Location" ) )
        {
            glm::vec3 a = ( agentStartLocPpt + offset ) * scale;
            glm::vec3 b = ( *wpLoc->getVec3() + offset ) * scale;
            glBegin( GL_LINES );
            glVertex3f( a.x, a.y, a.z );
            glVertex3f( b.x, b.y, b.z );
            glEnd();

            // arrow for this segment
            drawArrowAtMid( a, b );
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

    // Use global drawing limits transform
    glm::vec3 minB = g_DrawingLimits.minBounds;
    glm::vec3 maxB = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
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

// NEW: Draw walls for discovered rooms using their Vertices
void drawDiscoveredRoomsWalls( const gie::World& world, const gie::Planner& planner )
{
    const gie::Blackboard* context = &world.context();
    if( const auto* sim = planner.simulation( selectedSimulationGuid ) ) context = &sim->context();

    const auto* roomSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Room" ) } );
    if( !roomSet || roomSet->empty() ) return;

    glm::vec3 minB = g_DrawingLimits.minBounds;
    glm::vec3 maxB = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = 2.0f / ( maxB - minB );
	scale.z = 1.0f; // no Z scaling

    glColor3f( 0.85f, 0.85f, 0.85f );
    glLineWidth( 2.0f );

    for( auto guid : *roomSet )
    {
        const auto* e = context->entity( guid ); if( !e ) { e = world.entity( guid ); if( !e ) continue; }
        // Only draw discovered rooms
        auto disc = e->property( "Discovered" );
        if( !disc || !*disc->getBool() ) continue;

        auto verticesPpt = e->property( "Vertices" );
        if( !verticesPpt ) continue;
        const auto* verts = std::get_if< gie::Property::Vec3Vector >( &verticesPpt->value );
        if( !verts || verts->size() < 3 ) continue;

        // Draw lines connecting each vertex to the next, including last to first
        glBegin( GL_LINES );
        for( size_t i = 0; i < verts->size(); ++i )
        {
            // Current vertex
            const auto& currentVertex = (*verts)[i];
            glm::vec3 currentPos = ( currentVertex + offset ) * scale;
            
            // Next vertex (wrap around to first vertex when at the end)
            size_t nextIndex = ( i + 1 ) % verts->size();
            const auto& nextVertex = (*verts)[nextIndex];
            glm::vec3 nextPos = ( nextVertex + offset ) * scale;
            
            // Draw line from current vertex to next vertex
            glVertex3f( currentPos.x, currentPos.y, currentPos.z );
            glVertex3f( nextPos.x, nextPos.y, nextPos.z );
        }
        glEnd();
    }

    glLineWidth( 1.0f );
}

// NEW: Overlay room names at top-left inside each discovered room (if DisplayName is true)
void drawRoomNamesOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 pos, float windowWidth, float windowHeight )
{
    const gie::Blackboard* context = &world.context();
    if( const auto* sim = planner.simulation( selectedSimulationGuid ) ) context = &sim->context();

    const auto* roomSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Room" ) } );
    if( !roomSet || roomSet->empty() ) return;

    glm::vec3 minB = g_DrawingLimits.minBounds;
    glm::vec3 maxB = g_DrawingLimits.maxBounds;
    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = 2.0f / ( maxB - minB );

    ImDrawList* dl = ImGui::GetWindowDrawList();

    for( auto guid : *roomSet )
    {
        const auto* e = context->entity( guid ); if( !e ) { e = world.entity( guid ); if( !e ) continue; }
        // Only discovered rooms
        auto disc = e->property( "Discovered" );
        if( !disc || !*disc->getBool() ) continue;
        // Check DisplayName flag
        auto disp = e->property( "DisplayName" );
        if( !disp || !*disp->getBool() ) continue;

        auto verticesPpt = e->property( "Vertices" ); if( !verticesPpt ) continue;
        const auto* verts = std::get_if< gie::Property::Vec3Vector >( &verticesPpt->value );
        if( !verts || verts->size() < 3 ) continue;

        // Compute top-left pixel position from transformed vertices
        float minXpx = std::numeric_limits<float>::max(), minYpx = std::numeric_limits<float>::max(); // min x and min y in pixel space (min y => top)
        for( const auto& v : *verts )
        {
            glm::vec3 p = ( v + offset ) * scale; // NDC
            float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
            float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;
            if( x_px < minXpx ) minXpx = x_px;
            if( y_px < minYpx ) minYpx = y_px;
        }

        // Room name
        std::string name = "";
        if( e->nameHash() != gie::InvalidStringHash ) name = std::string( gie::stringRegister().get( e->nameHash() ) );
        if( name.empty() ) continue;

        ImVec2 textPos{ pos.x + minXpx + 4.0f, pos.y + minYpx + 4.0f }; // small padding inside
        // shadow
        dl->AddText( ImVec2( textPos.x + 1, textPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), name.c_str() );
        dl->AddText( textPos, IM_COL32( 255, 255, 255, 255 ), name.c_str() );
    }
}

// NEW: Waypoint Editor tool window (toggle + simple controls)
void drawWaypointEditorWindow( gie::World& world, gie::Planner& planner )
{
    if( !g_ShowWaypointEditorWindow ) return;

    if( ImGui::Begin( "Waypoint Editor", &g_ShowWaypointEditorWindow ) )
    {
        ImGui::TextUnformatted( "Single-click a waypoint to select it." );
        ImGui::TextUnformatted( "Double-click a waypoint to arm repositioning, then click to place." );
        ImGui::SliderFloat( "Pick Radius (px)", &g_WaypointPickRadiusPx, 4.0f, 30.0f );
        if( g_WaypointEditSelectedGuid != gie::NullGuid )
        {
            auto e = world.entity( g_WaypointEditSelectedGuid );
            std::string name = e && e->nameHash() != gie::InvalidStringHash ? std::string( gie::stringRegister().get( e->nameHash() ) ) : std::string( "<unknown>" );
            ImGui::Text( "Selected: %s (%llu)", name.c_str(), static_cast< unsigned long long >( g_WaypointEditSelectedGuid ) );
            // Show current location
            if( e )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    glm::vec3 p = *loc->getVec3();
                    ImGui::Text( "Location: (%.2f, %.2f, %.2f)", p.x, p.y, p.z );
                }
            }
            // If move armed, show target preview location
            if( g_WaypointEditPlaceArmed )
            {
                ImGui::Text( "Move armed: Yes" );
                if( g_WaypointEditHasTargetWorldPos )
                {
                    ImGui::Text( "Target: (%.2f, %.2f, %.2f)", g_WaypointEditTargetWorldPos.x, g_WaypointEditTargetWorldPos.y, g_WaypointEditTargetWorldPos.z );
                }
                else
                {
                    ImGui::TextUnformatted( "Target: <move mouse over World View>" );
                }
            }
            else
            {
                ImGui::Text( "Move armed: No" );
            }
            if( ImGui::Button( "Clear Selection" ) )
            {
                ResetWaypointEditorState();
            }

            // Links listing with direction toggles
            auto linksPpt = e->property( "Links" );
            if( !linksPpt )
            {
                linksPpt = e->createProperty( "Links", gie::Property::GuidVector{} );
            }
            auto selOutgoing = linksPpt->getGuidArray();

            // gather incoming neighbors by scanning waypoints
            std::set< gie::Guid > unionNeighbors;
            // Add outgoing neighbors first
            if( selOutgoing )
            {
                for( auto g : *selOutgoing ) if( g != g_WaypointEditSelectedGuid ) unionNeighbors.insert( g );
            }
            // All waypoints
            const auto* set = world.context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
            if( set )
            {
                for( auto g : *set )
                {
                    if( g == g_WaypointEditSelectedGuid ) continue;
                    auto n = world.entity( g );
                    if( !n ) continue;
                    auto lp = n->property( "Links" );
                    if( !lp ) continue;
                    auto arr = lp->getGuidArray();
                    if( !arr ) continue;
                    if( std::find( arr->begin(), arr->end(), g_WaypointEditSelectedGuid ) != arr->end() )
                    {
                        unionNeighbors.insert( g );
                    }
                }
            }

            // Bulk clear buttons row: > (outgoing), - (bidirectional), < (incoming)
            // Determine presence of each type
            bool anyIncoming = false, anyOutgoing = false, anyBidirectional = false;
            for( auto neighborGuid : unionNeighbors )
            {
                auto neighbor = world.entity( neighborGuid ); if( !neighbor ) continue;
                auto neighborLinksPpt = neighbor->property( "Links" );
                if( !neighborLinksPpt ) neighborLinksPpt = neighbor->createProperty( "Links", gie::Property::GuidVector{} );
                auto neighborLinks = neighborLinksPpt->getGuidArray();
                bool hasOut = selOutgoing && std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) != selOutgoing->end();
                bool hasIn = neighborLinks && std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) != neighborLinks->end();
                anyOutgoing |= hasOut;
                anyIncoming |= hasIn;
                anyBidirectional |= ( hasOut && hasIn );
            }
            auto buttonAlpha = [&]( bool enabled ) { ImGui::PushStyleVar( ImGuiStyleVar_Alpha, enabled ? 1.0f : 0.4f ); };

            // Clear/Add all outgoing ">"
            buttonAlpha( anyOutgoing );
            if( ImGui::Button( ">##clear_all_out" ) )
            {
                if( anyOutgoing )
                {
                    if( selOutgoing ) selOutgoing->clear();
                }
                else
                {
                    // Add outgoing link to all neighbors
                    if( selOutgoing )
                    {
                        for( auto neighborGuid : unionNeighbors )
                        {
                            if( neighborGuid == g_WaypointEditSelectedGuid ) continue;
                            if( std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) == selOutgoing->end() )
                            {
                                selOutgoing->push_back( neighborGuid );
                            }
                        }
                    }
                }
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();

            // Clear/Add all bidirectional "-"
            buttonAlpha( anyBidirectional );
            if( ImGui::Button( "-##clear_all_bi" ) )
            {
                for( auto neighborGuid : unionNeighbors )
                {
                    auto neighbor = world.entity( neighborGuid ); if( !neighbor ) continue;
                    auto neighborLinksPpt = neighbor->property( "Links" );
                    if( !neighborLinksPpt ) neighborLinksPpt = neighbor->createProperty( "Links", gie::Property::GuidVector{} );
                    auto neighborLinks = neighborLinksPpt->getGuidArray();
                    bool hasOut = selOutgoing && std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) != selOutgoing->end();
                    bool hasIn = neighborLinks && std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) != neighborLinks->end();
                    if( anyBidirectional )
                    {
                        if( hasOut && selOutgoing )
                        {
                            selOutgoing->erase( std::remove( selOutgoing->begin(), selOutgoing->end(), neighborGuid ), selOutgoing->end() );
                        }
                        if( hasIn && neighborLinks )
                        {
                            neighborLinks->erase( std::remove( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ), neighborLinks->end() );
                        }
                    }
                    else
                    {
                        if( selOutgoing )
                        {
                            if( std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) == selOutgoing->end() )
                                selOutgoing->push_back( neighborGuid );
                        }
                        if( neighborLinks )
                        {
                            if( std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) == neighborLinks->end() )
                                neighborLinks->push_back( g_WaypointEditSelectedGuid );
                        }
                    }
                }
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();

            // Clear/Add all incoming "<"
            buttonAlpha( anyIncoming );
            if( ImGui::Button( "<##clear_all_in" ) )
            {
                for( auto neighborGuid : unionNeighbors )
                {
                    auto neighbor = world.entity( neighborGuid ); if( !neighbor ) continue;
                    auto neighborLinksPpt = neighbor->property( "Links" );
                    if( !neighborLinksPpt ) neighborLinksPpt = neighbor->createProperty( "Links", gie::Property::GuidVector{} );
                    auto neighborLinks = neighborLinksPpt->getGuidArray();
                    if( anyIncoming )
                    {
                        if( neighborLinks )
                        {
                            neighborLinks->erase( std::remove( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ), neighborLinks->end() );
                        }
                    }
                    else
                    {
                        if( neighborLinks )
                        {
                            if( std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) == neighborLinks->end() )
                                neighborLinks->push_back( g_WaypointEditSelectedGuid );
                        }
                    }
                }
            }
            ImGui::PopStyleVar();

            // Links list per neighbor
            if( unionNeighbors.empty() )
            {
                ImGui::TextUnformatted( "No links." );
            }
            else
            {
                ImGui::Separator();
                ImGui::TextUnformatted( "Links:" );
                for( auto neighborGuid : unionNeighbors )
                {
                    auto neighbor = world.entity( neighborGuid );
                    if( !neighbor ) continue;
                    auto neighborLinksPpt = neighbor->property( "Links" );
                    if( !neighborLinksPpt ) neighborLinksPpt = neighbor->createProperty( "Links", gie::Property::GuidVector{} );
                    auto neighborLinks = neighborLinksPpt->getGuidArray();

                    bool hasOut = selOutgoing && std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) != selOutgoing->end();
                    bool hasIn = neighborLinks && std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) != neighborLinks->end();

                    auto buttonAlphaLocal = [&]( bool enabled )
                    {
                        ImGui::PushStyleVar( ImGuiStyleVar_Alpha, enabled ? 1.0f : 0.4f );
                    };

                    // Incoming button: "<"
                    buttonAlphaLocal( hasIn );
                    std::string inLbl = std::string( "<##in_" ) + std::to_string( static_cast< int >( neighborGuid ) );
                    if( ImGui::Button( inLbl.c_str() ) )
                    {
                        if( neighborLinks )
                        {
                            auto it = std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid );
                            if( it != neighborLinks->end() )
                            {
                                neighborLinks->erase( it );
                                hasIn = false;
                            }
                            else
                            {
                                neighborLinks->push_back( g_WaypointEditSelectedGuid );
                                hasIn = true;
                            }
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::SameLine();

                    // Both directions button: "-"
                    bool both = hasIn && hasOut;
                    buttonAlphaLocal( both );
                    std::string biLbl = std::string( "-##bi_" ) + std::to_string( static_cast< int >( neighborGuid ) );
                    if( ImGui::Button( biLbl.c_str() ) )
                    {
                        if( both )
                        {
                            // remove both
                            if( neighborLinks )
                            {
                                neighborLinks->erase( std::remove( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ), neighborLinks->end() );
                            }
                            if( selOutgoing )
                            {
                                selOutgoing->erase( std::remove( selOutgoing->begin(), selOutgoing->end(), neighborGuid ), selOutgoing->end() );
                            }
                            hasIn = false; hasOut = false;
                        }
                        else
                        {
                            // add both
                            if( neighborLinks )
                            {
                                if( std::find( neighborLinks->begin(), neighborLinks->end(), g_WaypointEditSelectedGuid ) == neighborLinks->end() )
                                    neighborLinks->push_back( g_WaypointEditSelectedGuid );
                            }
                            if( selOutgoing )
                            {
                                if( std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid ) == selOutgoing->end() )
                                    selOutgoing->push_back( neighborGuid );
                            }
                            hasIn = true; hasOut = true;
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::SameLine();

                    // Outgoing button: ">"
                    buttonAlphaLocal( hasOut );
                    std::string outLbl = std::string( ">##out_" ) + std::to_string( static_cast< int >( neighborGuid ) );
                    if( ImGui::Button( outLbl.c_str() ) )
                    {
                        if( selOutgoing )
                        {
                            auto it = std::find( selOutgoing->begin(), selOutgoing->end(), neighborGuid );
                            if( it != selOutgoing->end() )
                            {
                                selOutgoing->erase( it );
                                hasOut = false;
                            }
                            else
                            {
                                selOutgoing->push_back( neighborGuid );
                                hasOut = true;
                            }
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::SameLine( 0.0f, 8.0f );

                    // Neighbor label
                    std::string nName = neighbor->nameHash() != gie::InvalidStringHash ? std::string( gie::stringRegister().get( neighbor->nameHash() ) ) : std::string( "<unnamed>" );
                    ImGui::Text( "%s (%llu)", nName.c_str(), static_cast< unsigned long long >( neighborGuid ) );
                }
            }
        }
        else
        {
            ImGui::TextUnformatted( "Selected: <none>" );
        }
        ImGui::Separator();
        ImGui::TextUnformatted( g_ShowWorldViewWindow ? "World View is open." : "Open World View to interact." );
    }
    ImGui::End();
}

// NEW: Handle waypoint editor interactions on World View (internal/static)
static void handleWaypointEditorOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
    if( !g_ShowWaypointEditorWindow ) return;
    if( !g_WorldPtr ) return;

    ImGuiIO& io = ImGui::GetIO();
    // Local mouse position within the World View image
    const float localX = io.MousePos.x - pos.x;
    const float localY = io.MousePos.y - pos.y;
    const bool mouseOverWindow = ( localX >= 0.0f && localX <= windowWidth && localY >= 0.0f && localY <= windowHeight );

    // Utility: map local pixel coords to world coords (inverse of (world+offset)*scale)
    auto MouseToWorld = [&]( float lx, float ly ) -> glm::vec3
    {
        float u = lx / windowWidth;  // [0,1]
        float v = ly / windowHeight; // [0,1]
        float ndcX = u * 2.0f - 1.0f;
        float ndcY = ( 1.0f - v ) * 2.0f - 1.0f; // flip Y
        glm::vec3 ndc{ ndcX, ndcY, 0.0f };
        // inverse transform: world = ndc / scale + center
        return ndc / g_DrawingLimits.scale + g_DrawingLimits.center;
    };

    // Utility: find nearest waypoint within pixel radius from current mouse
    auto FindNearestWaypointUnderMouse = [&]() -> gie::Guid
    {
        const auto* set = g_WorldPtr->context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
        if( !set || set->empty() ) return gie::NullGuid;

        float bestDist2 = g_WaypointPickRadiusPx * g_WaypointPickRadiusPx;
        gie::Guid best = gie::NullGuid;
        glm::vec3 offset = -g_DrawingLimits.center;
        glm::vec3 scale = g_DrawingLimits.scale;
        for( auto guid : *set )
        {
            if( const auto* e = g_WorldPtr->entity( guid ) )
            {
                if( const auto* loc = e->property( "Location" ) )
                {
                    glm::vec3 p = ( *loc->getVec3() + offset ) * scale; // NDC
                    float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
                    float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;
                    float dx = x_px - localX;
                    float dy = y_px - localY;
                    float d2 = dx * dx + dy * dy;
                    if( d2 <= bestDist2 )
                    {
                        bestDist2 = d2;
                        best = guid;
                    }
                }
            }
        }
        return best;
    };

    if( !mouseOverWindow )
    {
        // Stop dragging if mouse leaves the area while LMB is not down
        if( !ImGui::IsMouseDown( 0 ) )
        {
            g_WaypointDragActive = false;
            g_WaypointDragMoving = false;
        }
        return;
    }

    // Left-click: select and arm dragging but only start moving after threshold exceeded
    if( ImGui::IsMouseClicked( 0 ) )
    {
        gie::Guid nearest = FindNearestWaypointUnderMouse();
        if( nearest != gie::NullGuid )
        {
            g_WaypointEditSelectedGuid = nearest;
            g_WaypointEditPlaceArmed = false; // prefer drag move now
            // arm dragging, remember Z and starting mouse position
            if( auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid ) )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    g_WaypointDragZ = loc->getVec3()->z;
                    g_WaypointDragActive = true;
                    g_WaypointDragMoving = false;
                    g_WaypointDragStartLocalX = localX;
                    g_WaypointDragStartLocalY = localY;
                }
            }
        }
    }

    // While LMB is down, check threshold and move if exceeded
    if( g_WaypointDragActive && ImGui::IsMouseDown( 0 ) && g_WaypointEditSelectedGuid != gie::NullGuid )
    {
        if( !g_WaypointDragMoving )
        {
            float dx = localX - g_WaypointDragStartLocalX;
            float dy = localY - g_WaypointDragStartLocalY;
            float dist2 = dx * dx + dy * dy;
            if( dist2 >= g_WaypointPickRadiusPx * g_WaypointPickRadiusPx )
            {
                g_WaypointDragMoving = true;
            }
        }
        if( g_WaypointDragMoving )
        {
            glm::vec3 worldPos = MouseToWorld( localX, localY );
            if( auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid ) )
            {
                if( auto loc = e->property( "Location" ) )
                {
                    loc->value = glm::vec3{ worldPos.x, worldPos.y, g_WaypointDragZ };
                }
            }
        }
    }

    // End dragging on LMB release
    if( g_WaypointDragActive && ImGui::IsMouseReleased( 0 ) )
    {
        g_WaypointDragActive = false;
        g_WaypointDragMoving = false;
    }

    // Double-click: select and arm repositioning (optional legacy behavior)
    if( ImGui::IsMouseDoubleClicked( 0 ) )
    {
        gie::Guid nearest = FindNearestWaypointUnderMouse();
        if( nearest != gie::NullGuid )
        {
            g_WaypointEditSelectedGuid = nearest;
            g_WaypointEditPlaceArmed = true;
        }
    }

    // If move is armed, update target preview while moving mouse
    if( g_WaypointEditPlaceArmed )
    {
        glm::vec3 wp = MouseToWorld( localX, localY );
        g_WaypointEditTargetWorldPos = wp;
        g_WaypointEditHasTargetWorldPos = true;
    }

    // Right-click: if a waypoint is selected and the clicked waypoint is an outgoing neighbor, remove that outgoing link
    if( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) && g_WaypointEditSelectedGuid != gie::NullGuid )
    {
        gie::Guid hit = FindNearestWaypointUnderMouse();
        if( hit != gie::NullGuid && hit != g_WaypointEditSelectedGuid )
        {
            if( auto selectedE = g_WorldPtr->entity( g_WaypointEditSelectedGuid ) )
            {
                auto linksPpt = selectedE->property( "Links" );
                if( linksPpt )
                {
                    if( auto arr = linksPpt->getGuidArray() )
                    {
                        auto it = std::find( arr->begin(), arr->end(), hit );
                        if( it != arr->end() )
                        {
                            arr->erase( it ); // remove outgoing link only
                        }
                    }
                }
            }
        }
    }
}

// NEW: draw selection marker for waypoint editor on top of World View
static void drawWaypointEditorOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
    if( !g_ShowWaypointEditorWindow ) return;
    if( !g_WorldPtr ) return;
    if( g_WaypointEditSelectedGuid == gie::NullGuid ) return;

    auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid );
    if( !e ) return;
    auto loc = e->property( "Location" );
    if( !loc ) return;

    glm::vec3 offset = -g_DrawingLimits.center;
    glm::vec3 scale = g_DrawingLimits.scale;

    glm::vec3 p = ( *loc->getVec3() + offset ) * scale; // NDC
    float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
    float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c{ pos.x + x_px, pos.y + y_px };
    ImU32 col = IM_COL32( 255, 255, 0, 220 );
    ImU32 colBg = IM_COL32( 0, 0, 0, 120 );
    float r = g_WaypointPickRadiusPx;
    dl->AddCircleFilled( c, r + 2.0f, colBg, 24 );
    dl->AddCircle( c, r, col, 24, 2.0f );

    // optional: show armed indicator
    if( g_WaypointEditPlaceArmed )
    {
        dl->AddCircle( c, r * 0.6f, IM_COL32( 255, 120, 0, 220 ), 24, 2.0f );
    }
}
