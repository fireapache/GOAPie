#pragma once

#include <string>

namespace project {

// Project state management
extern std::string g_projectRootPath;

// Project state helpers
bool HasProjectRoot();
void SetProjectRoot(const std::string& path);
std::string GetProjectWorldFile();
std::string GetProjectPlannerFile();

// Project operations
void NewProject();
bool LoadProject(const std::string& jsonFilePath);
bool SaveProject();
bool SaveProjectAs(const std::string& folderPath);

// Dialog functions (Windows implementation)
bool ShowOpenFileDialog(std::string& outPath);
bool ShowSelectFolderDialog(std::string& outPath);

} // namespace project
