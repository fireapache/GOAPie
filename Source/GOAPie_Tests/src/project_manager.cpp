#include "project_manager.h"
#include "visualization.h"
#include <goapie.h>
#include <persistency.h>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace project {

// Project state
std::string g_projectRootPath;

bool HasProjectRoot() {
    return !g_projectRootPath.empty();
}

void SetProjectRoot(const std::string& path) {
    g_projectRootPath = path;
}

std::string GetProjectWorldFile() {
    if (!HasProjectRoot()) return "";
    return g_projectRootPath + "/world.json";
}

std::string GetProjectPlannerFile() {
    if (!HasProjectRoot()) return "";
    return g_projectRootPath + "/planner.json";
}

void NewProject() {
    if (!g_WorldPtr || !g_PlannerPtr) return;
    
    // Reset World state
    g_WorldPtr->context().eraseAll();
    g_WorldPtr->context().properties().clear();
    g_WorldPtr->context().entityTagRegister().clear();
    
    // Reset Planner state
    g_PlannerPtr->clearSimulations();
    
    // Reset Goal state (clear targets)
    // Note: Goal doesn't have a clear API, but we can reset its targets
    // by creating a new goal with the same world
    // For now, we'll leave the goal as-is since it's passed by reference to examples
    
    // Clear project root
    g_projectRootPath.clear();
    
    // Reset UI selection state
    g_selectedEntityGuids.clear();
    selectedSimulationGuid = gie::NullGuid;
    g_WaypointEditSelectedGuid = gie::NullGuid;
    g_SelectedArchetypeGuid = gie::NullGuid;
}

bool LoadProject(const std::string& jsonFilePath) {
    if (!g_WorldPtr || !g_PlannerPtr) return false;
    
    std::filesystem::path filePath(jsonFilePath);
    std::string projectRoot = filePath.parent_path().string();
    
    // Try to load world.json from the project folder
    std::string worldFile = projectRoot + "/world.json";
    if (!std::filesystem::exists(worldFile)) {
        std::cout << "Error: world.json not found in project folder: " << projectRoot << std::endl;
        return false;
    }
    
    // Load the world
    bool worldLoaded = gie::persistency::LoadWorldFromJson(*g_WorldPtr, worldFile);
    if (!worldLoaded) {
        std::cout << "Error: Failed to load world from: " << worldFile << std::endl;
        return false;
    }
    
    // TODO: Load planner.json when planner serialization is implemented
    // For now, we'll just clear the planner state
    g_PlannerPtr->clearSimulations();
    
    // Set project root
    SetProjectRoot(projectRoot);
    
    std::cout << "Project loaded successfully from: " << projectRoot << std::endl;
    return true;
}

bool SaveProject() {
    if (!HasProjectRoot()) {
        // No project root set, trigger Save As dialog
        std::string folderPath;
        if (!ShowSelectFolderDialog(folderPath)) {
            return false; // User cancelled
        }
        return SaveProjectAs(folderPath);
    }
    
    if (!g_WorldPtr || !g_PlannerPtr) return false;
    
    // Create project directory if it doesn't exist
    std::filesystem::create_directories(g_projectRootPath);
    
    // Save world.json
    std::string worldFile = GetProjectWorldFile();
    bool worldSaved = gie::persistency::SaveWorldToJson(*g_WorldPtr, worldFile);
    if (!worldSaved) {
        std::cout << "Error: Failed to save world to: " << worldFile << std::endl;
        return false;
    }
    
    // TODO: Save planner.json when planner serialization is implemented
    
    std::cout << "Project saved successfully to: " << g_projectRootPath << std::endl;
    return true;
}

bool SaveProjectAs(const std::string& folderPath) {
    SetProjectRoot(folderPath);
    return SaveProject();
}

#ifdef _WIN32
bool ShowOpenFileDialog(std::string& outPath) {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileNameA(&ofn) == TRUE) {
        outPath = szFile;
        return true;
    }
    return false;
}

bool ShowSelectFolderDialog(std::string& outPath) {
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = "Select Project Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            outPath = path;
            IMalloc* imalloc = 0;
            if (SUCCEEDED(SHGetMalloc(&imalloc))) {
                imalloc->Free(pidl);
                imalloc->Release();
            }
            return true;
        }
        IMalloc* imalloc = 0;
        if (SUCCEEDED(SHGetMalloc(&imalloc))) {
            imalloc->Free(pidl);
            imalloc->Release();
        }
    }
    return false;
}
#else
// Non-Windows implementations would go here
bool ShowOpenFileDialog(std::string& outPath) {
    std::cout << "File dialogs not implemented for this platform" << std::endl;
    return false;
}

bool ShowSelectFolderDialog(std::string& outPath) {
    std::cout << "Folder dialogs not implemented for this platform" << std::endl;
    return false;
}
#endif

} // namespace project
