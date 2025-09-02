#include "visualization.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

// our window dimensions
static const GLuint WIDTH = 1280;
static const GLint HEIGHT = 920;

// GL resources
GLuint FBO = 0;
GLuint RBO = 0;
GLuint texture_id = 0;

// Global UI/state
DrawingLimits g_DrawingLimits;
bool g_DrawingLimitsInitialized = false;
bool g_BoundsEditorVisible = false;
float g_BoundsInputX[2] = { 0.0f, 0.0f };
float g_BoundsInputY[2] = { 0.0f, 0.0f };

gie::Guid selectedSimulationGuid = gie::NullGuid;
bool g_ShowWaypointGuidSuffix = false;
bool g_ShowWaypointArrows = false;

// Path step state
bool g_PathStepMode = false;
int g_PathStepIndex = 0;
gie::Guid g_PathStepSimGuid = gie::NullGuid;

// Window visibility toggles
bool g_ShowDebugPathWindow = false;
bool g_ShowGoapieVisualizationWindow = false;
bool g_ShowWorldViewWindow = false;
bool g_ShowDebugMessagesWindow = false;
bool g_ShowSimulationArgumentsWindow = false;
bool g_ShowPlannerLogWindow = false;
bool g_ShowBlackboardPropertiesWindow = false;
bool g_ShowMorePlannerOptions = false;
bool g_ShowWaypointEditorWindow = false;
bool g_ShowEntityOutlinerWindow = false; // New: Entity Outliner visibility
bool g_ShowDetailsPanelWindow = false;   // New: Details Panel visibility

// Shared selection
gie::Guid g_SelectedEntityGuid = gie::NullGuid; // New: selected entity shared state

// Waypoint editor globals
// bool g_ShowWaypointEditorWindow = false; // moved above with other flags
gie::Guid g_WaypointEditSelectedGuid = gie::NullGuid;
float g_WaypointPickRadiusPx = 14.0f;
gie::World* g_WorldPtr = nullptr;
gie::Planner* g_PlannerPtr = nullptr;
bool g_WaypointEditPlaceArmed = false;
glm::vec3 g_WaypointEditTargetWorldPos{ 0.f, 0.f, 0.f };
bool g_WaypointEditHasTargetWorldPos = false;
bool g_WaypointDragActive = false;
float g_WaypointDragZ = 0.0f;
bool g_WaypointDragMoving = false;
float g_WaypointDragStartLocalX = 0.0f;
float g_WaypointDragStartLocalY = 0.0f;

// UI settings persistence
static const char* kUiWindowsSettingsFile = "goapie_ui_windows.ini";
static void LoadWindowVisibilitySettings()
{
    std::ifstream in( kUiWindowsSettingsFile );
    if( !in.good() ) return;
    auto parseBool = []( const std::string& s ) -> bool
    {
        return s == "1" || s == "true" || s == "True" || s == "TRUE" || s == "yes" || s == "on";
    };
    std::string line;
    while( std::getline( in, line ) )
    {
        if( line.empty() || line[0] == '#' ) continue;
        size_t eq = line.find('=');
        if( eq == std::string::npos ) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if( key == "ShowDebugPathWindow" ) g_ShowDebugPathWindow = parseBool(val);
        else if( key == "ShowGoapieVisualizationWindow" ) g_ShowGoapieVisualizationWindow = parseBool(val);
        else if( key == "ShowWorldViewWindow" ) g_ShowWorldViewWindow = parseBool(val);
        else if( key == "ShowDebugMessagesWindow" ) g_ShowDebugMessagesWindow = parseBool(val);
        else if( key == "ShowSimulationArgumentsWindow" ) g_ShowSimulationArgumentsWindow = parseBool(val);
        else if( key == "ShowPlannerLogWindow" ) g_ShowPlannerLogWindow = parseBool(val);
        else if( key == "ShowBlackboardPropertiesWindow" ) g_ShowBlackboardPropertiesWindow = parseBool(val);
        else if( key == "ShowWaypointEditorWindow" ) g_ShowWaypointEditorWindow = parseBool(val);
        else if( key == "ShowEntityOutlinerWindow" ) g_ShowEntityOutlinerWindow = parseBool(val); // New
        else if( key == "ShowDetailsPanelWindow" ) g_ShowDetailsPanelWindow = parseBool(val);     // New
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
    out << "ShowEntityOutlinerWindow=" << ( g_ShowEntityOutlinerWindow ? 1 : 0 ) << '\n'; // New
    out << "ShowDetailsPanelWindow=" << ( g_ShowDetailsPanelWindow ? 1 : 0 ) << '\n';     // New
}

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
        drawDiscoveredRoomsWalls( world, planner );
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
            ImGui::MenuItem( "Waypoint Editor", NULL, &g_ShowWaypointEditorWindow );
            ImGui::MenuItem( "Entity Outliner", NULL, &g_ShowEntityOutlinerWindow ); // New
            ImGui::MenuItem( "Details Panel", NULL, &g_ShowDetailsPanelWindow );     // New
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    ImGui::End();
}
