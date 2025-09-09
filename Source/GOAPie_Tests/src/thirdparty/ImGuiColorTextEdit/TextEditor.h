// Minimal stub of ImGuiColorTextEdit API used by the project.
// This is a lightweight implementation (MIT-compatible) that provides
// a basic multiline editor with a placeholder for language definitions.
// It does not implement full syntax highlighting, but exposes the same
// surface used in the planner setup so the project builds and can be
// upgraded to a full editor later.

#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

class TextEditor
{
public:
    struct LanguageDefinition
    {
        std::string mName;
        std::unordered_map<std::string, bool> mKeywords;
        static LanguageDefinition Lua();
    };

    TextEditor();
    ~TextEditor();

    void SetLanguageDefinition(const LanguageDefinition& lang);
    void SetPalette(const std::vector<ImU32>& palette);
    void SetText(const std::string& txt);
    const std::string& GetText() const;
    bool IsTextChanged() const;
    void ClearChangedFlag();

    // Render the editor. This minimal implementation wraps ImGui::InputTextMultiline.
    // Parameters mirror the full library render signature sufficiently for usage here.
    void Render(const char* aId, const ImVec2& aSize = ImVec2(-1, -1), bool aBorder = false);

    // Helpers for caller convenience
    static std::vector<ImU32> GetDarkPalette();

private:
    std::string mText;
    bool mChanged;
    LanguageDefinition mLang;
    std::vector<ImU32> mPalette;
};
