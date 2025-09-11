#include "visualization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "persistency.h"

// Forward declarations for internal helpers
static void handleWaypointEditorOnWorldView( ImVec2 pos, float windowWidth, float windowHeight );
static void drawWaypointEditorOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight );

// New: overlays for rectangle and multi-selection highlight
static void drawRectSelectionOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight );

// Helper: translate mouse local coords to world coordinates
static glm::vec3 MouseToWorld( float lx, float ly, float windowWidth, float windowHeight )
{
	float u = lx / windowWidth;
	float v = ly / windowHeight;
	float ndcX = u * 2.0f - 1.0f;
	float ndcY = ( 1.0f - v ) * 2.0f - 1.0f;
	glm::vec3 ndc{ ndcX, ndcY, 0.0f };
	return ndc / g_DrawingLimits.scale + g_DrawingLimits.center;
}

// Internal state for rectangle activation threshold
static bool s_RectPrimed = false;		 // true after mouse down until either drag threshold or release
static ImVec2 s_ClickStartLocal{ 0, 0 }; // cached click start

// New: generic entity selection from World View (agnostic to tools)
static void handleEntitySelectionOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
	if( !g_WorldPtr )
		return;

	ImGuiIO& io = ImGui::GetIO();
	const float localX = io.MousePos.x - pos.x;
	const float localY = io.MousePos.y - pos.y;
	const bool mouseOverWindow = ( localX >= 0.0f && localX <= windowWidth && localY >= 0.0f && localY <= windowHeight );

	// Allow canceling archetype placement with Escape or Right Mouse Button while hovering World View
	if( mouseOverWindow && g_SelectedArchetypeGuid != gie::NullGuid )
	{
#if defined( IMGUI_VERSION_NUM ) && IMGUI_VERSION_NUM >= 18700
		bool esc = ImGui::IsKeyPressed( ImGuiKey_Escape );
#else
		bool esc = ImGui::IsKeyPressed( ImGui::GetKeyIndex( ImGuiKey_Escape ) );
#endif
		if( esc || ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
		{
			CancelEntityFactory();
			return;
		}
	}

	if( !mouseOverWindow )
	{
		// finalize drags if mouse leaves window and button released
		if( !ImGui::IsMouseDown( 0 ) )
		{
			g_MultiDragActive = false;
			g_RectSelectionActive = false;
			s_RectPrimed = false;
		}
		return;
	}

	// Helper lambdas
	auto getDrawSet = [ & ]() -> const std::set< gie::Guid >*
	{
		return g_WorldPtr->context().entityTagRegister().tagSet( { gie::stringHasher( "Draw" ) } );
	};

	auto nearestDrawEntityUnderMouse = [ & ]() -> gie::Guid
	{
		const auto* drawSet = getDrawSet();
		if( !drawSet || drawSet->empty() )
			return gie::NullGuid;
		float bestDist2 = g_WaypointPickRadiusPx * g_WaypointPickRadiusPx; // reuse pick radius
		gie::Guid best = gie::NullGuid;
		glm::vec3 offset = -g_DrawingLimits.center;
		glm::vec3 scale = g_DrawingLimits.scale;
		for( auto guid : *drawSet )
		{
			const auto* e = g_WorldPtr->entity( guid );
			if( !e )
				continue;
			const auto* loc = e->property( "Location" );
			if( !loc || !loc->getVec3() )
				continue;

			glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
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
		return best;
	};

	// If an archetype is selected, left-click creates an entity instance at the clicked position
	if( g_SelectedArchetypeGuid != gie::NullGuid && ImGui::IsMouseClicked( 0 ) )
	{
		// Instantiate from archetype
		const gie::Archetype* arch = g_WorldPtr->archetype( g_SelectedArchetypeGuid );
		if( arch )
		{
			gie::Entity* e = arch->instantiate( *g_WorldPtr );
			if( e )
			{
				// Set default Location property if available or create it
				glm::vec3 wp = MouseToWorld( localX, localY, windowWidth, windowHeight );
				if( auto* loc = e->property( "Location" ) )
				{
					loc->value = wp;
				}
				else
				{
					e->createProperty( "Location", wp );
				}

				// Tag as Draw by convenience so it shows up
				std::vector< gie::Tag > tags{ gie::stringHasher( "Draw" ) };
				g_WorldPtr->context().entityTagRegister().tag( e, tags );

				g_selectedEntityGuids.clear();
				g_selectedEntityGuids.insert( e->guid() );
			}
		}
		return; // avoid also doing selection
	}

	// CTRL-click: toggle entity in multi-selection
	if( ImGui::IsMouseClicked( 0 ) && ( ImGui::GetIO().KeyCtrl ) )
	{
		gie::Guid nearGuid = nearestDrawEntityUnderMouse();
		if( nearGuid != gie::NullGuid )
		{
			// No selection yet: select this as single
			if( g_selectedEntityGuids.empty() )
			{
				g_selectedEntityGuids.insert( nearGuid );
			}
			else if( g_selectedEntityGuids.size() == 1 )
			{
				// size==1: either toggle off if same, or start multi by inserting
				if( g_selectedEntityGuids.find( nearGuid ) != g_selectedEntityGuids.end() )
				{
					g_selectedEntityGuids.clear();
				}
				else
				{
					g_selectedEntityGuids.insert( nearGuid );
				}
			}
			else
			{
				// Already multi-selection: toggle membership
				auto it = g_selectedEntityGuids.find( nearGuid );
				if( it != g_selectedEntityGuids.end() )
				{
					g_selectedEntityGuids.erase( it );
				}
				else
				{
					g_selectedEntityGuids.insert( nearGuid );
				}
			}
		}
		return;
	}

	// Mouse press: checking if clicking on an entity for single selection (no Ctrl, not in multi mode)
	if( ImGui::IsMouseClicked( 0 ) && !ImGui::GetIO().KeyCtrl && g_selectedEntityGuids.size() <= 1 )
	{
		// Single-click select the entity under the mouse
		gie::Guid nearGuid = nearestDrawEntityUnderMouse();
		if( nearGuid != gie::NullGuid )
		{
			g_selectedEntityGuids.clear();
			g_selectedEntityGuids.insert( nearGuid );
			return;
		}
	}

	// Mouse press: decide between multi-drag or priming rectangle
	if( ImGui::IsMouseClicked( 0 ) )
	{
		s_ClickStartLocal = ImVec2( localX, localY );
		s_RectPrimed = true;
		g_RectSelectionStartLocal = s_ClickStartLocal;
		g_RectSelectionEndLocal = s_ClickStartLocal;

		// If clicking on an already selected entity that's part of a multi-selection, start multi-drag
		gie::Guid nearGuid = nearestDrawEntityUnderMouse();
		if( nearGuid != gie::NullGuid && g_selectedEntityGuids.find( nearGuid ) != g_selectedEntityGuids.end()
			&& g_selectedEntityGuids.size() > 1 )
		{
			// Initialize multi-drag state
			g_MultiDragActive = true;
			g_MultiDragInitialPositions.clear();
			g_MultiDragMouseStartWorld = MouseToWorld( localX, localY, windowWidth, windowHeight );
			for( auto guid : g_selectedEntityGuids )
			{
				if( auto* e = g_WorldPtr->entity( guid ) )
				{
					if( auto* loc = e->property( "Location" ) )
					{
						if( auto* v = loc->getVec3() )
						{
							g_MultiDragInitialPositions[ guid ] = *v;
						}
					}
				}
			}
			// Not a rectangle selection in this case
			s_RectPrimed = false;
			g_RectSelectionActive = false;
			return;
		}

		// Clicking elsewhere: we will either do a rectangle selection on drag, or a single-click select if released quickly
		g_MultiDragActive = false;
	}

	// While holding left button
	if( ImGui::IsMouseDown( 0 ) )
	{
		if( g_MultiDragActive )
		{
			// Move all selected entities by mouse delta (world space)
			glm::vec3 mouseWorld = MouseToWorld( localX, localY, windowWidth, windowHeight );
			glm::vec3 delta = mouseWorld - g_MultiDragMouseStartWorld;
			for( const auto& kv : g_MultiDragInitialPositions )
			{
				if( auto* e = g_WorldPtr->entity( kv.first ) )
				{
					if( auto* loc = e->property( "Location" ) )
					{
						glm::vec3 base = kv.second;
						loc->value = glm::vec3{ base.x + delta.x, base.y + delta.y, base.z };
					}
				}
			}
			return;
		}

		if( s_RectPrimed )
		{
			// Detect drag threshold to begin rectangle selection
			float dx = localX - s_ClickStartLocal.x;
			float dy = localY - s_ClickStartLocal.y;
			float dist2 = dx * dx + dy * dy;
			const float threshold2 = 3.0f * 3.0f; // a few pixels
			if( dist2 >= threshold2 )
			{
				// Begin rectangular selection; cancel previous selections
				g_RectSelectionActive = true;
				g_selectedEntityGuids.clear();
				g_WaypointEditSelectedGuid = gie::NullGuid;
			}
		}

		if( g_RectSelectionActive )
		{
			// Update rectangle end and recompute selection preview
			g_RectSelectionEndLocal = ImVec2( localX, localY );

			const float x0 = std::min( g_RectSelectionStartLocal.x, g_RectSelectionEndLocal.x );
			const float x1 = std::max( g_RectSelectionStartLocal.x, g_RectSelectionEndLocal.x );
			const float y0 = std::min( g_RectSelectionStartLocal.y, g_RectSelectionEndLocal.y );
			const float y1 = std::max( g_RectSelectionStartLocal.y, g_RectSelectionEndLocal.y );

			const auto* drawSet = getDrawSet();
			if( drawSet && !drawSet->empty() )
			{
				g_selectedEntityGuids.clear();
				glm::vec3 offset = -g_DrawingLimits.center;
				glm::vec3 scale = g_DrawingLimits.scale;
				for( auto guid : *drawSet )
				{
					const auto* e = g_WorldPtr->entity( guid );
					if( !e )
						continue;
					const auto* loc = e->property( "Location" );
					if( !loc || !loc->getVec3() )
						continue;

					glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
					float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
					float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

					if( x_px >= x0 && x_px <= x1 && y_px >= y0 && y_px <= y1 )
					{
						g_selectedEntityGuids.insert( guid );
					}
				}
			}
			return;
		}
	}

	// Mouse release finalizes operations
	if( ImGui::IsMouseReleased( 0 ) )
	{
		if( g_MultiDragActive )
		{
			g_MultiDragActive = false;
			g_MultiDragInitialPositions.clear();
			return;
		}

		if( g_RectSelectionActive )
		{
			// Finalize rectangle selection. If only one entity selected, set it as single selection
			s_RectPrimed = false;
			g_RectSelectionActive = false;
			// unified set already represents single vs multi (size==1 => single)
			return;
		}

		// No drag happened: treat as simple click selection (nearest entity)
		if( s_RectPrimed )
		{
			s_RectPrimed = false;
			// Pick nearest entity among those tagged for drawing and having a Location
			const auto* drawSet = getDrawSet();
			if( !drawSet || drawSet->empty() )
				return;

			float bestDist2 = g_WaypointPickRadiusPx * g_WaypointPickRadiusPx; // reuse pick radius
			gie::Guid best = gie::NullGuid;

			glm::vec3 offset = -g_DrawingLimits.center;
			glm::vec3 scale = g_DrawingLimits.scale;

			for( auto guid : *drawSet )
			{
				const auto* e = g_WorldPtr->entity( guid );
				if( !e )
					continue;
				const auto* loc = e->property( "Location" );
				if( !loc || !loc->getVec3() )
					continue;

				glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
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

			g_selectedEntityGuids.clear();
			if( best != gie::NullGuid )
			{
				g_selectedEntityGuids.insert( best );
				// Sync waypoint editor selection if it's a waypoint when tool opens later
				const auto* waypointSet = g_WorldPtr->context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
				if( waypointSet && waypointSet->find( best ) != waypointSet->end() )
				{
					g_WaypointEditSelectedGuid = best;
				}
			}
			return;
		}
	}
}

static std::string exampleNameOrDefault()
{
    extern std::string g_exampleName;
    return g_exampleName.empty() ? std::string( "unamed_example" ) : g_exampleName;
}

static std::string relativeExamplesRootDir()
{
	return std::string{ "examples" };
}

static std::string relativeExampleRootDir()
{
    return gie::persistency::joinPath( relativeExamplesRootDir(), exampleNameOrDefault() );
}

static std::string relativeWorldStateFilePath()
{
    return gie::persistency::joinPath( relativeExampleRootDir(), "world.json" );
}

void drawWorldViewWindow( gie::World& world, const gie::Planner& planner )
{
	if( !g_ShowWorldViewWindow )
		return;

	if( ImGui::Begin( "World View", &g_ShowWorldViewWindow ) )
	{
		// If loading, show overlay and skip content interactions
		if( g_IsLoading )
		{
			DrawWindowLoadingOverlay();
			ImGui::End();
			return;
		}

		const float windowWidth = ImGui::GetContentRegionAvail().x;
		const float windowHeight = ImGui::GetContentRegionAvail().y;

		rescale_framebuffer( static_cast< GLsizei >( windowWidth ), static_cast< GLsizei >( windowHeight ) );
		glViewport( 0, 0, static_cast< GLsizei >( windowWidth ), static_cast< GLsizei >( windowHeight ) );

		ImVec2 pos = ImGui::GetCursorScreenPos();

		ImGui::GetWindowDrawList()->AddImage(
			texture_id,
			ImVec2( pos.x, pos.y ),
			ImVec2( pos.x + windowWidth, pos.y + windowHeight ),
			ImVec2( 0, 1 ),
			ImVec2( 1, 0 ) );

		ImGui::SetCursorScreenPos( ImVec2( pos.x + 8.0f, pos.y + 8.0f ) );
		static float s_SaveMsgTimer = 0.0f;
		static float s_LoadMsgTimer = 0.0f;
		// New: error message timers and texts
		static float s_SaveErrTimer = 0.0f;
		static float s_LoadErrTimer = 0.0f;
		static std::string s_SaveErrText;
		static std::string s_LoadErrText;

		if( !g_BoundsEditorVisible )
		{
			if( ImGui::Button( "Update Bounds" ) )
			{
				if( !g_DrawingLimitsInitialized )
				{
					updateDrawingBounds( world );
				}
				g_BoundsInputX[ 0 ] = g_DrawingLimits.minBounds.x;
				g_BoundsInputX[ 1 ] = g_DrawingLimits.maxBounds.x;
				g_BoundsInputY[ 0 ] = g_DrawingLimits.minBounds.y;
				g_BoundsInputY[ 1 ] = g_DrawingLimits.maxBounds.y;
				g_BoundsEditorVisible = true;
			}
			ImGui::SameLine();
			if( ImGui::Button( "Save" ) )
			{
				// Use a local filename to avoid mutating g_exampleName
				if( gie::persistency::SaveWorldToJson( world, relativeWorldStateFilePath() ) )
				{
					s_SaveMsgTimer = 2.0f;
				}
				else
				{
					s_SaveErrText = std::string( "Save failed!" );
					s_SaveErrTimer = 4.0f;
				}
			}
			ImGui::SameLine();
			if( ImGui::Button( "Load" ) )
			{
				// Try new path first, then legacy path
				g_IsLoading = true;
				bool loaded = false;
				
				// Try new path
				if( gie::persistency::LoadWorldFromJson( world, relativeWorldStateFilePath() ) )
				{
					loaded = true;
				}
				
				if( loaded )
				{
					g_DrawingLimitsInitialized = false;
					s_LoadMsgTimer = 2.0f;
				}
				else
				{
					s_LoadErrText = std::string( "Load failed: " ) + relativeWorldStateFilePath();
					s_LoadErrTimer = 4.0f;
				}
				g_IsLoading = false;
			}
			if( s_SaveMsgTimer > 0.0f )
			{
				ImGui::SameLine();
				ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "Saved at %s", relativeWorldStateFilePath().c_str() );
				s_SaveMsgTimer -= ImGui::GetIO().DeltaTime;
			}
			if( s_LoadMsgTimer > 0.0f )
			{
				ImGui::SameLine();
				ImGui::TextColored( ImVec4( 0.2f, 0.6f, 1.0f, 1.0f ), "Loaded" );
				s_LoadMsgTimer -= ImGui::GetIO().DeltaTime;
			}
			// New: show transient error messages
			if( s_SaveErrTimer > 0.0f )
			{
				ImGui::SameLine();
				ImGui::TextColored(
					ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ),
					"%s",
					s_SaveErrText.empty() ? "Save failed" : s_SaveErrText.c_str() );
				s_SaveErrTimer -= ImGui::GetIO().DeltaTime;
			}
			if( s_LoadErrTimer > 0.0f )
			{
				ImGui::SameLine();
				ImGui::TextColored(
					ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ),
					"%s",
					s_LoadErrText.empty() ? "Load failed" : s_LoadErrText.c_str() );
				s_LoadErrTimer -= ImGui::GetIO().DeltaTime;
			}
		}
		else
		{
			ImGui::PushStyleVar( ImGuiStyleVar_Alpha, 0.95f );
			ImGui::BeginChild(
				"##BoundsPanel",
				ImVec2( 300, 0 ),
				true,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
			ImGui::TextUnformatted( "Bounds" );
			ImGui::Separator();
			ImGui::InputFloat2( "X Min/Max", g_BoundsInputX, "%.3f" );
			ImGui::InputFloat2( "Y Min/Max", g_BoundsInputY, "%.3f" );

			bool differs = false;
			if( g_DrawingLimitsInitialized )
			{
				auto nearly = []( float a, float b )
				{
					return std::fabs( a - b ) <= 1e-5f;
				};
				differs = !nearly( g_BoundsInputX[ 0 ], g_DrawingLimits.minBounds.x )
						  || !nearly( g_BoundsInputX[ 1 ], g_DrawingLimits.maxBounds.x )
						  || !nearly( g_BoundsInputY[ 0 ], g_DrawingLimits.minBounds.y )
						  || !nearly( g_BoundsInputY[ 1 ], g_DrawingLimits.maxBounds.y );
			}

			ImGui::BeginDisabled( !differs );
			if( ImGui::Button( "Apply" ) )
			{
				if( g_BoundsInputX[ 0 ] > g_BoundsInputX[ 1 ] )
					std::swap( g_BoundsInputX[ 0 ], g_BoundsInputX[ 1 ] );
				if( g_BoundsInputY[ 0 ] > g_BoundsInputY[ 1 ] )
					std::swap( g_BoundsInputY[ 0 ], g_BoundsInputY[ 1 ] );

				g_DrawingLimits.minBounds.x = g_BoundsInputX[ 0 ];
				g_DrawingLimits.maxBounds.x = g_BoundsInputX[ 1 ];
				g_DrawingLimits.minBounds.y = g_BoundsInputY[ 0 ];
				g_DrawingLimits.maxBounds.y = g_BoundsInputY[ 1 ];

				g_DrawingLimits.range = g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds;
				g_DrawingLimits.center = ( g_DrawingLimits.maxBounds + g_DrawingLimits.minBounds ) * 0.5f;
				g_DrawingLimits.scale = 2.0f / ( g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds );
				g_DrawingLimits.scale.z = 1.0f;

				g_DrawingLimitsInitialized = true;
				g_BoundsEditorVisible = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if( ImGui::Button( "Cancel" ) )
			{
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

		// overlays and interactions
		// Determine hover and whether ImGui wants to capture mouse (when interacting with other UI)
		ImGuiIO& io = ImGui::GetIO();
		bool worldHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_None );
		g_WorldViewWindowHovered = worldHovered;
		bool allowWorldInput = worldHovered;

		drawWaypointGuidSuffixOverlay( world, planner, pos, windowWidth, windowHeight );
		drawRoomNamesOverlay( world, planner, pos, windowWidth, windowHeight );

		// New: rectangle visualization + highlight of multi-selected entities
		drawRectSelectionOverlayOnWorldView( pos, windowWidth, windowHeight );

		// New: generic selection and archetype placement + rectangle and multi-drag
		if( allowWorldInput )
			handleEntitySelectionOnWorldView( pos, windowWidth, windowHeight );

		if( g_ShowWaypointEditorWindow )
		{
			if( allowWorldInput )
				handleWaypointEditorOnWorldView( pos, windowWidth, windowHeight );
			drawWaypointEditorOverlayOnWorldView( pos, windowWidth, windowHeight );
		}
	}
	ImGui::End();
}

void drawWaypointGuidSuffixOverlay(
	const gie::World& world,
	const gie::Planner& planner,
	ImVec2 pos,
	float windowWidth,
	float windowHeight )
{
	const gie::Blackboard* worldContext = &world.context();
	if( const auto* sim = planner.simulation( selectedSimulationGuid ) )
	{
		worldContext = &sim->context();
	}
	const auto* waypointSet = worldContext->entityTagRegister().tagSet( { "Waypoint" } );

	if( waypointSet && !waypointSet->empty() )
	{
		glm::vec3 offset = -g_DrawingLimits.center;
		glm::vec3 scale = g_DrawingLimits.scale;

		ImDrawList* dl = ImGui::GetWindowDrawList();

		for( auto waypointGuid : *waypointSet )
		{
			const bool isSelectedWaypoint = waypointGuid == g_WaypointEditSelectedGuid;

			if( !g_ShowWaypointGuidSuffix && !isSelectedWaypoint )
				continue;

			const auto* e = world.entity( waypointGuid );
			if( !e )
				continue;
			const auto* loc = e->property( "Location" );
			if( !loc )
				continue;

			glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
			float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
			float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

			ImVec2 suffixPos{ pos.x + x_px, pos.y + y_px - 12.0f };

			unsigned long long id = static_cast< unsigned long long >( waypointGuid );
			unsigned int last4 = static_cast< unsigned int >( id % 10000ULL );
			char buf[ 8 ];
			snprintf( buf, sizeof( buf ), "%04u", last4 );

			const ImVec2 suffixSize = ImGui::CalcTextSize( buf );
			suffixPos.x -= suffixSize.x * 0.5f;
			suffixPos.y += isSelectedWaypoint ? 30.0f : 20.0f;

			ImGui::GetWindowDrawList()->AddText( ImVec2( suffixPos.x + 1, suffixPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), buf );
			ImGui::GetWindowDrawList()->AddText( suffixPos, IM_COL32( 255, 255, 0, 255 ), buf );

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
					idxLabel = name;
				}
			}

			const ImVec2 idxSize = ImGui::CalcTextSize( idxLabel.c_str() );
			const float idxPosYOffset = isSelectedWaypoint ? 35.0f : 15.0f;
			const ImVec2 idxPos{ pos.x + x_px - idxSize.x * 0.5f, suffixPos.y - ( idxSize.y + idxPosYOffset ) };

			ImGui::GetWindowDrawList()->AddText(
				ImVec2( idxPos.x + 1, idxPos.y + 1 ),
				IM_COL32( 0, 0, 0, 200 ),
				idxLabel.c_str() );
			ImGui::GetWindowDrawList()->AddText( idxPos, IM_COL32( 255, 255, 255, 255 ), idxLabel.c_str() );
		}
	}
}

void drawRoomNamesOverlay(
	const gie::World& world,
	const gie::Planner& planner,
	ImVec2 pos,
	float windowWidth,
	float windowHeight )
{
	const gie::Blackboard* context = &world.context();
	if( const auto* sim = planner.simulation( selectedSimulationGuid ) )
		context = &sim->context();

	const auto* roomSet = context->entityTagRegister().tagSet( { gie::stringHasher( "Room" ) } );
	if( !roomSet || roomSet->empty() )
		return;

	glm::vec3 offset = -g_DrawingLimits.center;
	glm::vec3 scale = 2.0f / ( g_DrawingLimits.maxBounds - g_DrawingLimits.minBounds );

	ImDrawList* dl = ImGui::GetWindowDrawList();

	for( auto guid : *roomSet )
	{
		const auto* e = context->entity( guid );
		if( !e )
		{
			e = world.entity( guid );
			if( !e )
				continue;
		}
		auto disc = e->property( "Discovered" );
		if( !disc || !*disc->getBool() )
			continue;
		auto disp = e->property( "DisplayName" );
		if( !disp || !*disp->getBool() )
			continue;

		auto verticesPpt = e->property( "Vertices" );
		if( !verticesPpt )
			continue;
		const auto* verts = std::get_if< gie::Property::Vec3Vector >( &verticesPpt->value );
		if( !verts || verts->size() < 3 )
			continue;

		float minXpx = std::numeric_limits< float >::max(), minYpx = std::numeric_limits< float >::max();
		for( const auto& v : *verts )
		{
			glm::vec3 p = ( v + offset ) * scale;
			float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
			float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;
			if( x_px < minXpx )
				minXpx = x_px;
			if( y_px < minYpx )
				minYpx = y_px;
		}

		std::string name = "";
		if( e->nameHash() != gie::InvalidStringHash )
			name = std::string( gie::stringRegister().get( e->nameHash() ) );
		if( name.empty() )
			continue;

		ImVec2 textPos{ pos.x + minXpx + 4.0f, pos.y + minYpx + 4.0f };
		dl->AddText( ImVec2( textPos.x + 1, textPos.y + 1 ), IM_COL32( 0, 0, 0, 200 ), name.c_str() );
		dl->AddText( textPos, IM_COL32( 255, 255, 255, 255 ), name.c_str() );
	}
}

// Internal helpers copied from original file
static void handleWaypointEditorOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
	if( !g_ShowWaypointEditorWindow )
		return;
	if( !g_WorldPtr )
		return;

	// If group multi-drag or rectangle selection is active, let it handle input exclusively
	if( g_MultiDragActive || g_RectSelectionActive )
		return;

	ImGuiIO& io = ImGui::GetIO();
	const float localX = io.MousePos.x - pos.x;
	const float localY = io.MousePos.y - pos.y;
	const bool mouseOverWindow = ( localX >= 0.0f && localX <= windowWidth && localY >= 0.0f && localY <= windowHeight );

	auto FindNearestWaypointUnderMouse = [ & ]() -> gie::Guid
	{
		const auto* set = g_WorldPtr->context().entityTagRegister().tagSet( { gie::stringHasher( "Waypoint" ) } );
		if( !set || set->empty() )
			return gie::NullGuid;

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
					glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
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
		if( !ImGui::IsMouseDown( 0 ) )
		{
			g_WaypointDragActive = false;
			g_WaypointDragMoving = false;
		}
		return;
	}

	if( ImGui::IsMouseClicked( 0 ) )
	{
		gie::Guid nearest = FindNearestWaypointUnderMouse();
		if( nearest != gie::NullGuid )
		{
			g_WaypointEditSelectedGuid = nearest;
			// sync into unified selection set
			g_selectedEntityGuids.clear();
			g_selectedEntityGuids.insert( nearest );
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

	// New: Right-click to link selected waypoint A to waypoint B under mouse (bidirectional)
	if( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
	{
		if( g_WaypointEditSelectedGuid != gie::NullGuid )
		{
			gie::Guid target = FindNearestWaypointUnderMouse();
			if( target != gie::NullGuid && target != g_WaypointEditSelectedGuid )
			{
				auto a = g_WorldPtr->entity( g_WaypointEditSelectedGuid );
				auto b = g_WorldPtr->entity( target );
				if( a && b )
				{
					// Check if bidirectional link already exists
					auto aLinks = a->property( "Links" );
					auto bLinks = b->property( "Links" );
					bool hasAtoB = false;
					bool hasBtoA = false;
					if( aLinks )
					{
						auto arr = aLinks->getGuidArray();
						if( arr && std::find( arr->begin(), arr->end(), target ) != arr->end() )
							hasAtoB = true;
					}
					if( bLinks )
					{
						auto arr = bLinks->getGuidArray();
						if( arr && std::find( arr->begin(), arr->end(), g_WaypointEditSelectedGuid ) != arr->end() )
							hasBtoA = true;
					}
					bool bidirectionalExists = hasAtoB && hasBtoA;

					if( bidirectionalExists )
					{
						// Remove bidirectional link
						if( aLinks )
						{
							auto arr = aLinks->getGuidArray();
							if( arr )
							{
								arr->erase( std::remove( arr->begin(), arr->end(), target ), arr->end() );
							}
						}
						if( bLinks )
						{
							auto arr = bLinks->getGuidArray();
							if( arr )
							{
								arr->erase( std::remove( arr->begin(), arr->end(), g_WaypointEditSelectedGuid ), arr->end() );
							}
						}
					}
					else
					{
						// Add bidirectional link
						if( !aLinks )
							aLinks = a->createProperty( "Links", gie::Property::GuidVector{} );
						if( aLinks )
						{
							auto arr = aLinks->getGuidArray();
							if( arr && std::find( arr->begin(), arr->end(), target ) == arr->end() )
							{
								arr->push_back( target );
							}
						}
						if( !bLinks )
							bLinks = b->createProperty( "Links", gie::Property::GuidVector{} );
						if( bLinks )
						{
							auto arr = bLinks->getGuidArray();
							if( arr && std::find( arr->begin(), arr->end(), g_WaypointEditSelectedGuid ) == arr->end() )
							{
								arr->push_back( g_WaypointEditSelectedGuid );
							}
						}
					}
				}
			}
		}
	}

	// Middle-click to add/remove outgoing link from selected to target waypoint
	if( ImGui::IsMouseClicked( ImGuiMouseButton_Middle ) )
	{
		if( g_WaypointEditSelectedGuid != gie::NullGuid )
		{
			gie::Guid target = FindNearestWaypointUnderMouse();
			if( target != gie::NullGuid && target != g_WaypointEditSelectedGuid )
			{
				auto a = g_WorldPtr->entity( g_WaypointEditSelectedGuid );
				if( a )
				{
					auto aLinks = a->property( "Links" );
					bool hasAtoB = false;
					if( aLinks )
					{
						auto arr = aLinks->getGuidArray();
						if( arr && std::find( arr->begin(), arr->end(), target ) != arr->end() )
							hasAtoB = true;
					}

					if( hasAtoB )
					{
						// Remove outgoing link
						if( aLinks )
						{
							auto arr = aLinks->getGuidArray();
							if( arr )
							{
								arr->erase( std::remove( arr->begin(), arr->end(), target ), arr->end() );
							}
						}
					}
					else
					{
						// Add outgoing link
						if( !aLinks )
							aLinks = a->createProperty( "Links", gie::Property::GuidVector{} );
						if( aLinks )
						{
							auto arr = aLinks->getGuidArray();
							if( arr && std::find( arr->begin(), arr->end(), target ) == arr->end() )
							{
								arr->push_back( target );
							}
						}
					}
				}
			}
		}
	}

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
			glm::vec3 worldPos = MouseToWorld( localX, localY, windowWidth, windowHeight );
			if( auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid ) )
			{
				if( auto loc = e->property( "Location" ) )
				{
					loc->value = glm::vec3{ worldPos.x, worldPos.y, g_WaypointDragZ };
				}
			}
		}
	}

	if( g_WaypointDragActive && ImGui::IsMouseReleased( 0 ) )
	{
		g_WaypointDragActive = false;
		g_WaypointDragMoving = false;
	}
}

static void drawWaypointEditorOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
	if( !g_ShowWaypointEditorWindow )
		return;
	if( !g_WorldPtr )
		return;
	// Selection is now drawn globally in drawRectSelectionOverlayOnWorldView.
	// If a unified single selection exists, skip drawing here to avoid duplicate markers.
	if( g_selectedEntityGuids.size() == 1 )
		return;
	if( g_WaypointEditSelectedGuid == gie::NullGuid )
		return;

	auto e = g_WorldPtr->entity( g_WaypointEditSelectedGuid );
	if( !e )
		return;
	auto loc = e->property( "Location" );
	if( !loc )
		return;

	glm::vec3 offset = -g_DrawingLimits.center;
	glm::vec3 scale = g_DrawingLimits.scale;

	glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
	float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
	float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 c{ pos.x + x_px, pos.y + y_px };
	ImU32 col = IM_COL32( 255, 255, 0, 220 );
	ImU32 colBg = IM_COL32( 0, 0, 0, 120 );
	float r = g_WaypointPickRadiusPx;
	dl->AddCircleFilled( c, r + 2.0f, colBg, 24 );
	dl->AddCircle( c, r, col, 24, 2.0f );
}

static void drawRectSelectionOverlayOnWorldView( ImVec2 pos, float windowWidth, float windowHeight )
{
	// Draw selection rectangle if active
	ImDrawList* dl = ImGui::GetWindowDrawList();
	if( g_RectSelectionActive )
	{
		const float x0 = std::min( g_RectSelectionStartLocal.x, g_RectSelectionEndLocal.x );
		const float x1 = std::max( g_RectSelectionStartLocal.x, g_RectSelectionEndLocal.x );
		const float y0 = std::min( g_RectSelectionStartLocal.y, g_RectSelectionEndLocal.y );
		const float y1 = std::max( g_RectSelectionStartLocal.y, g_RectSelectionEndLocal.y );

		ImVec2 a{ pos.x + x0, pos.y + y0 };
		ImVec2 b{ pos.x + x1, pos.y + y1 };
		ImU32 colFill = IM_COL32( 80, 160, 255, 40 );
		ImU32 colBorder = IM_COL32( 80, 160, 255, 160 );
		dl->AddRectFilled( a, b, colFill, 0.0f );
		dl->AddRect( a, b, colBorder, 0.0f, 0, 1.5f );
	}

	// Highlight multi-selected entities (unified set represents multi-selection when size>1)
	if( !g_selectedEntityGuids.empty() && g_WorldPtr )
	{
		glm::vec3 offset = -g_DrawingLimits.center;
		glm::vec3 scale = g_DrawingLimits.scale;

		// Single selection: draw yellow marker
		if( g_selectedEntityGuids.size() == 1 )
		{
			auto guid = *g_selectedEntityGuids.begin();
			const auto* e = g_WorldPtr->entity( guid );
			if( e )
			{
				const auto* loc = e->property( "Location" );
				if( loc && loc->getVec3() )
				{
					glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
					float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
					float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;
					ImVec2 c{ pos.x + x_px, pos.y + y_px };
					ImU32 col = IM_COL32( 255, 255, 0, 220 );
					ImU32 colBg = IM_COL32( 0, 0, 0, 120 );
					float r = g_WaypointPickRadiusPx * 0.9f;
					dl->AddCircleFilled( c, r + 2.0f, colBg, 24 );
					dl->AddCircle( c, r, col, 24, 2.0f );
				}
			}
		}
		else
		{
			// Multi-selection: draw blue markers for each selected entity
			for( auto guid : g_selectedEntityGuids )
			{
				const auto* e = g_WorldPtr->entity( guid );
				if( !e )
					continue;
				const auto* loc = e->property( "Location" );
				if( !loc || !loc->getVec3() )
					continue;

				glm::vec3 p = ( *loc->getVec3() + offset ) * scale;
				float x_px = ( p.x * 0.5f + 0.5f ) * windowWidth;
				float y_px = ( 1.0f - ( p.y * 0.5f + 0.5f ) ) * windowHeight;

				ImVec2 c{ pos.x + x_px, pos.y + y_px };
				ImU32 col = IM_COL32( 80, 200, 255, 230 );
				ImU32 colBg = IM_COL32( 0, 0, 0, 120 );
				float r = g_WaypointPickRadiusPx * 0.9f;
				dl->AddCircleFilled( c, r + 2.0f, colBg, 24 );
				dl->AddCircle( c, r, col, 24, 2.0f );
			}
		}
	}
}
