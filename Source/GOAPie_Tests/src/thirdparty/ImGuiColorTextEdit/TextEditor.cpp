// Minimal implementation for the TextEditor stub used by the project.
// Provides a simple wrapper around ImGui::InputTextMultiline to allow editing text.
// This is intentionally lightweight and not a full syntax-highlighting editor.

#include "TextEditor.h"

#include <cstring>
#include <algorithm>
#include <imgui.h>

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::Lua()
{
    LanguageDefinition def;
    def.mName = "Lua";
    // Minimal keyword set for convenience (not used by rendering here).
    def.mKeywords = {
        { "and", true },{ "break", true },{ "do", true },{ "else", true },{ "elseif", true },
        { "end", true },{ "false", true },{ "for", true },{ "function", true },{ "if", true },
        { "in", true },{ "local", true },{ "nil", true },{ "not", true },{ "or", true },
        { "repeat", true },{ "return", true },{ "then", true },{ "true", true },{ "until", true },
        { "while", true }
    };
    return def;
}

TextEditor::TextEditor()
    : mText()
    , mChanged(false)
    , mLang()
    , mPalette()
{
}

TextEditor::~TextEditor()
{
}

void TextEditor::SetLanguageDefinition(const LanguageDefinition& lang)
{
    mLang = lang;
}

void TextEditor::SetPalette(const std::vector<ImU32>& palette)
{
    mPalette = palette;
}

void TextEditor::SetText(const std::string& txt)
{
    if (mText != txt)
    {
        mText = txt;
        mChanged = true;
    }
}

const std::string& TextEditor::GetText() const
{
    return mText;
}

bool TextEditor::IsTextChanged() const
{
    return mChanged;
}

void TextEditor::ClearChangedFlag()
{
    mChanged = false;
}

 // Render the editor using ImGui::InputTextMultiline.
 // Notes:
 // - Use std::string as backing buffer and provide a resize callback required by ImGui
 //   when ImGuiInputTextFlags_CallbackResize is set. This avoids modifying the ImGui library.
static int TextEditor_InputTextCallback(ImGuiInputTextCallbackData* data)
{
	if( data->EventFlag == ImGuiInputTextFlags_CallbackResize )
    {
        // Resize std::string and update pointer
        std::string* str = reinterpret_cast<std::string*>(data->UserData);
        if (str)
        {
            str->resize(static_cast<size_t>(data->BufTextLen));
            data->Buf = const_cast<char*>(str->c_str());
        }
    }
    return 0;
}

void TextEditor::Render(const char* aId, const ImVec2& aSize, bool aBorder)
{
    (void)aBorder;

    // Ensure there is at least one char so &mText[0] is valid.
    if (mText.empty())
        mText.push_back('\0');

    // Reserve some extra capacity for editing.
    const size_t desired = std::max<size_t>(4096, mText.size() + 256);
    if (mText.capacity() < desired)
        mText.reserve(desired);

    // Make sure buffer is null-terminated and has space.
    mText.resize(mText.size()); // no-op but ensures size() is valid
    char* buf = const_cast<char*>(mText.c_str());
    size_t bufSize = mText.capacity() + 1; // allow ImGui to use capacity; callback will resize if needed

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize;

    // Render using ImGui and supply the resize callback and the std::string as user data.
    bool changed = ImGui::InputTextMultiline(aId, buf, bufSize, aSize, flags, TextEditor_InputTextCallback, reinterpret_cast<void*>(&mText));
    if (changed)
    {
        mChanged = true;
    }
}

std::vector<ImU32> TextEditor::GetDarkPalette()
{
    // Minimal palette: a few colors that can be used by a richer editor
    return std::vector<ImU32>{
        IM_COL32(0,0,0,255),
        IM_COL32(128,128,128,255),
        IM_COL32(255,128,0,255),
        IM_COL32(255,255,255,255),
        IM_COL32(180,180,180,255),
    };
}
