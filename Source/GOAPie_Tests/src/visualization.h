#pragma once

// Centralized header for visualization modules
// Defines shared state, function declarations, and required includes.

// Enable ImGui math operators
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <imgui.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <goapie.h>
#include <persistency.h>

#include <glm/glm.hpp>
#include <set>
#include <string>
#include <limits>

#include "example.h"

// GL resources shared across modules
extern GLuint FBO;          // frame buffer object
extern GLuint RBO;          // rendering buffer object
extern GLuint texture_id;   // texture attached to framebuffer

// UI/state shared across modules
struct DrawingLimits
{
    glm::vec3 minBounds{ std::numeric_limits<float>::max() };
    glm::vec3 maxBounds{ std::numeric_limits<float>::lowest() };
    glm::vec3 range{ 0.f };
    glm::vec3 center{ 0.f };
    glm::vec3 scale{ 1.f };
    const float margin = 0.1f; // 10% margin
};

extern DrawingLimits g_DrawingLimits;
extern bool g_DrawingLimitsInitialized;
extern bool g_BoundsEditorVisible;
extern float g_BoundsInputX[2];
extern float g_BoundsInputY[2];

extern gie::Guid selectedSimulationGuid;
extern bool g_ShowWaypointGuidSuffix;
extern bool g_ShowWaypointArrows;

// Global loading state
extern bool g_IsLoading;
// Draw a semi-transparent overlay saying "Loading" over the current window bounds
void DrawWindowLoadingOverlay( const char* text = "Loading" );

// Path-stepping visualization state
extern bool g_PathStepMode;
extern int g_PathStepIndex;
extern gie::Guid g_PathStepSimGuid;

// Window visibility flags
extern bool g_ShowDebugPathWindow;
extern bool g_ShowGoapieVisualizationWindow;
extern bool g_ShowWorldViewWindow;
extern bool g_ShowDebugMessagesWindow;
extern bool g_ShowSimulationArgumentsWindow;
extern bool g_ShowPlannerLogWindow;
extern bool g_ShowBlackboardPropertiesWindow;
extern bool g_ShowMorePlannerOptions;
extern bool g_ShowWaypointEditorWindow;
extern bool g_ShowEntityOutlinerWindow; // New: Entity Outliner visibility
extern bool g_ShowDetailsPanelWindow;   // New: Details Panel visibility
extern bool g_ShowWorldSettingsWindow;  // New: World Settings visibility

// Global selection shared across tools
extern gie::Guid g_SelectedEntityGuid; // New: selected entity shared state

// Waypoint editor state
extern gie::Guid g_WaypointEditSelectedGuid;
extern float g_WaypointPickRadiusPx;
extern gie::World* g_WorldPtr;
extern gie::Planner* g_PlannerPtr;
extern bool g_WaypointEditPlaceArmed;
extern glm::vec3 g_WaypointEditTargetWorldPos;
extern bool g_WaypointEditHasTargetWorldPos;
extern bool g_WaypointDragActive;
extern float g_WaypointDragZ;
extern bool g_WaypointDragMoving;
extern float g_WaypointDragStartLocalX;
extern float g_WaypointDragStartLocalY;

// Lifecycle
int visualization( ExampleParameters& params );

// GL/Window helpers
void processInput( GLFWwindow* window );
void framebuffer_size_callback( GLFWwindow* window, int width, int height );
void create_framebuffer();
void bind_framebuffer();
void unbind_framebuffer();
void rescale_framebuffer( float width, float height );

// Docking
void ShowExampleAppDockSpace( bool* p_open );

// UI windows
void drawImGuiWindows( bool& useHeuristics, ExampleParameters& params );
void drawGoapieVisualizationWindow( bool& useHeuristics, ExampleParameters& params );
void drawDebugMessagesWindow( ExampleParameters& params );
void drawSimulationArgumentsWindow( ExampleParameters& params );
void drawPlannerLogWindow( ExampleParameters& params );
void drawDebugPathWindow( ExampleParameters& params );
void drawSimulationTreeView( const gie::Planner& planner, const gie::Simulation* simulation );
void drawBlackboardPropertiesWindow( const gie::Simulation* simulation );
void drawEntityOutlinerWindow( gie::World& world ); // New: Entity Outliner
void drawDetailsPanelWindow( gie::World& world );   // New: Details Panel
void drawWorldSettingsWindow( ExampleParameters& params ); // New: World Settings

// World View and overlays
void drawWorldViewWindow( gie::World& world, const gie::Planner& planner );
void drawWaypointGuidSuffixOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 windowPos, float windowWidth, float windowHeight );
void drawRoomNamesOverlay( const gie::World& world, const gie::Planner& planner, ImVec2 pos, float windowWidth, float windowHeight );

// Draw world content
void updateDrawingBounds( const gie::World& world );
void drawWaypointsAndLinks( const gie::World& world, const gie::Planner& planner );
void drawHeistOverlays( const gie::World& world, const gie::Planner& planner );
void drawTrees( const gie::World& world, const gie::Planner& planner );
void drawSelectedSimulationPath( const gie::World& world, const gie::Planner& planner );
void drawAgentCrosshair( const gie::World& world, const gie::Planner& planner );
void drawDiscoveredRoomsWalls( const gie::World& world, const gie::Planner& planner );

// Waypoint editor
void drawWaypointEditorWindow( gie::World& world, gie::Planner& planner );
void ResetWaypointEditorState();
// Cancel only active waypoint-edit transient operations (move/drag), keep selection
bool CancelWaypointEditorOngoingOperation();

// Entity outliner helpers
void ResetEntityOutlinerState();
// Cancel only active outliner transient operations (name dialogs)
bool CancelEntityOutlinerOngoingOperation();

// Details panel helpers
// Cancel only active details transient operations (add/edit/tag dialogs)
bool CancelDetailsPanelOngoingOperation();
