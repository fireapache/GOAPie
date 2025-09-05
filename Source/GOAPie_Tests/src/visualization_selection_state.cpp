#include "visualization.h"
#include <unordered_map>
#include <set>

// Definitions for rectangular selection and multi-drag shared state
bool g_RectSelectionActive = false;
ImVec2 g_RectSelectionStartLocal{ 0.0f, 0.0f };
ImVec2 g_RectSelectionEndLocal{ 0.0f, 0.0f };
std::set<gie::Guid> g_MultiSelectedGuids{};

bool g_MultiDragActive = false;
glm::vec3 g_MultiDragMouseStartWorld{ 0.0f, 0.0f, 0.0f };
std::unordered_map<gie::Guid, glm::vec3> g_MultiDragInitialPositions{};
