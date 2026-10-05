#include "StudyModApiInternal.h"

#include <cfloat>
#include <memory>

#include <fast/Fast3dGui.h>
#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "assets/soh_assets.h"

extern "C" {
#include "textures/kanji/kanji.h"
}

namespace {

Fast::Fast3dGui* sFast3dGui = nullptr;
constexpr const char* kDPadGlyph = "StudyMod.DialogueGlyph.DPad";
constexpr const char* kAGlyph = "StudyMod.DialogueGlyph.A";
constexpr const char* kBGlyph = "StudyMod.DialogueGlyph.B";
constexpr const char* kLGlyph = "StudyMod.DialogueGlyph.L";
constexpr const char* kRGlyph = "StudyMod.DialogueGlyph.R";
constexpr const char* kZGlyph = "StudyMod.DialogueGlyph.Z";
constexpr const char* kCUpGlyph = "StudyMod.DialogueGlyph.CUp";
constexpr const char* kCDownGlyph = "StudyMod.DialogueGlyph.CDown";
constexpr const char* kCLeftGlyph = "StudyMod.DialogueGlyph.CLeft";
constexpr const char* kCRightGlyph = "StudyMod.DialogueGlyph.CRight";

ImU32 ToImColor(uint32_t rgba) {
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}

ImFont* SelectFont(uint32_t flags) {
    if ((flags & STUDY_MOD_TEXT_JAPANESE) != 0 && OTRGlobals::Instance != nullptr &&
        OTRGlobals::Instance->fontJapanese != nullptr) {
        return OTRGlobals::Instance->fontJapanese;
    }
    return ImGui::GetFont();
}

void DrawRectFilled(void* context, StudyModRect rect, uint32_t rgba, float rounding) {
    static_cast<ImDrawList*>(context)->AddRectFilled({ rect.x, rect.y }, { rect.x + rect.width, rect.y + rect.height },
                                                     ToImColor(rgba), rounding);
}

void DrawRect(void* context, StudyModRect rect, uint32_t rgba, float rounding, float thickness) {
    static_cast<ImDrawList*>(context)->AddRect({ rect.x, rect.y }, { rect.x + rect.width, rect.y + rect.height },
                                               ToImColor(rgba), rounding, 0, thickness);
}

void DrawLine(void* context, StudyModVec2 from, StudyModVec2 to, uint32_t rgba, float thickness) {
    static_cast<ImDrawList*>(context)->AddLine({ from.x, from.y }, { to.x, to.y }, ToImColor(rgba), thickness);
}

void DrawText(void* context, StudyModVec2 position, float size, uint32_t rgba, const char* utf8, float wrapWidth,
              uint32_t flags) {
    if (utf8 == nullptr || *utf8 == '\0') {
        return;
    }
    ImFont* font = SelectFont(flags);
    const float actualSize = size > 0.0f ? size : font->FontSize;
    static_cast<ImDrawList*>(context)->AddText(font, actualSize, { position.x, position.y }, ToImColor(rgba), utf8,
                                              nullptr, wrapWidth > 0.0f ? wrapWidth : 0.0f);
}

StudyModVec2 MeasureText(void*, float size, const char* utf8, float wrapWidth, uint32_t flags) {
    if (utf8 == nullptr || *utf8 == '\0') {
        return {};
    }
    ImFont* font = SelectFont(flags);
    const ImVec2 measured = font->CalcTextSizeA(size > 0.0f ? size : font->FontSize, FLT_MAX,
                                               wrapWidth > 0.0f ? wrapWidth : 0.0f, utf8);
    return { measured.x, measured.y };
}

void PushClipRect(void* context, StudyModRect rect, int32_t intersectWithCurrent) {
    static_cast<ImDrawList*>(context)->PushClipRect({ rect.x, rect.y }, { rect.x + rect.width, rect.y + rect.height },
                                                    intersectWithCurrent != 0);
}

void PopClipRect(void* context) {
    static_cast<ImDrawList*>(context)->PopClipRect();
}

int32_t DrawButtonGlyph(void* context, uint64_t button, StudyModRect bounds, uint32_t rgba) {
    if (sFast3dGui == nullptr) {
        return 0;
    }
    const char* name = nullptr;
    switch (button) {
        case STUDY_MOD_INPUT_PRIMARY: name = kAGlyph; break;
        case STUDY_MOD_INPUT_SECONDARY: name = kBGlyph; break;
        case STUDY_MOD_INPUT_LEFT_SHOULDER: name = kLGlyph; break;
        case STUDY_MOD_INPUT_RIGHT_SHOULDER: name = kRGlyph; break;
        case STUDY_MOD_INPUT_LEFT_TRIGGER: name = kZGlyph; break;
        case STUDY_MOD_INPUT_FACE_UP: name = kCUpGlyph; break;
        case STUDY_MOD_INPUT_FACE_DOWN: name = kCDownGlyph; break;
        case STUDY_MOD_INPUT_FACE_LEFT: name = kCLeftGlyph; break;
        case STUDY_MOD_INPUT_FACE_RIGHT: name = kCRightGlyph; break;
        case STUDY_MOD_INPUT_NAV_UP:
        case STUDY_MOD_INPUT_NAV_DOWN:
        case STUDY_MOD_INPUT_NAV_LEFT:
        case STUDY_MOD_INPUT_NAV_RIGHT: name = kDPadGlyph; break;
        default: return 0;
    }
    ImTextureID texture = sFast3dGui->GetTextureByName(name);
    if (texture == nullptr) {
        return 0;
    }
    static_cast<ImDrawList*>(context)->AddImage(texture, { bounds.x, bounds.y },
                                                { bounds.x + bounds.width, bounds.y + bounds.height }, { 0, 0 },
                                                { 1, 1 }, ToImColor(rgba));
    return 1;
}

uint32_t MouseMask(bool pressed) {
    uint32_t result = 0;
    if (pressed ? ImGui::IsMouseClicked(ImGuiMouseButton_Left) : ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        result |= STUDY_MOD_MOUSE_LEFT;
    }
    if (pressed ? ImGui::IsMouseClicked(ImGuiMouseButton_Right) : ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        result |= STUDY_MOD_MOUSE_RIGHT;
    }
    if (pressed ? ImGui::IsMouseClicked(ImGuiMouseButton_Middle) : ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        result |= STUDY_MOD_MOUSE_MIDDLE;
    }
    return result;
}

class StudyModOverlayWindow final : public Ship::GuiWindow {
  public:
    StudyModOverlayWindow() : GuiWindow("", true, "Study Mod Overlay", ImVec2(-1, -1), ImGuiWindowFlags_NoInputs) {
    }

    void InitElement() override {
        sFast3dGui = dynamic_cast<Fast::Fast3dGui*>(Ship::Context::GetRawInstance()->GetWindow()->GetGui().get());
        if (sFast3dGui == nullptr) {
            return;
        }
        const auto load = [](const char* name, const char* resource, const ImVec4& tint) {
            if (!sFast3dGui->HasTextureByName(name)) {
                sFast3dGui->LoadGuiTexture(name, resource, "", tint);
            }
        };
        load(kDPadGlyph, gDPadTex, { 0.86f, 0.88f, 0.92f, 1.0f });
        load(kAGlyph, gMsgKanji839FButtonATex, { 0.35f, 0.72f, 1.0f, 1.0f });
        load(kBGlyph, gMsgKanji83A0ButtonBTex, { 0.35f, 0.90f, 0.42f, 1.0f });
        load(kLGlyph, gMsgKanji83A2ButtonLTex, { 0.86f, 0.88f, 0.92f, 1.0f });
        load(kRGlyph, gMsgKanji83A3ButtonRTex, { 0.86f, 0.88f, 0.92f, 1.0f });
        load(kZGlyph, gMsgKanji83A4ButtonZTex, { 0.86f, 0.88f, 0.92f, 1.0f });
        load(kCUpGlyph, gMsgKanji83A5ButtonCUpTex, { 1.0f, 0.82f, 0.22f, 1.0f });
        load(kCDownGlyph, gMsgKanji83A6ButtonCDownTex, { 1.0f, 0.82f, 0.22f, 1.0f });
        load(kCLeftGlyph, gMsgKanji83A7ButtonCLeftTex, { 1.0f, 0.82f, 0.22f, 1.0f });
        load(kCRightGlyph, gMsgKanji83A8ButtonCRightTex, { 1.0f, 0.82f, 0.22f, 1.0f });
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
    }

    void Draw() override {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiIO& io = ImGui::GetIO();
        uint32_t modifiers = 0;
        if (io.KeyShift) modifiers |= STUDY_MOD_MODIFIER_SHIFT;
        if (io.KeyCtrl) modifiers |= STUDY_MOD_MODIFIER_CONTROL;
        if (io.KeyAlt) modifiers |= STUDY_MOD_MODIFIER_ALT;
        const StudyModOverlayFrame frame{
            sizeof(StudyModOverlayFrame),
            { viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y,
              viewport->WorkSize.x, viewport->WorkSize.y },
            io.DeltaTime,
            io.MousePos.x,
            io.MousePos.y,
            io.MouseWheel,
            MouseMask(true),
            MouseMask(false),
            modifiers,
        };
        ImDrawList* list = ImGui::GetForegroundDrawList(viewport);
        const StudyModOverlayDrawApi draw{ sizeof(StudyModOverlayDrawApi), list, DrawRectFilled, DrawRect, DrawLine,
                                           DrawText, MeasureText, PushClipRect, PopClipRect, DrawButtonGlyph };
        StudyModApi::DispatchOverlay(frame, draw);
    }
};

std::shared_ptr<StudyModOverlayWindow> sWindow;

void RegisterStudyModOverlay() {
    sWindow = std::make_shared<StudyModOverlayWindow>();
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->AddGuiWindow(sWindow);
    sWindow->Show();
}

static RegisterShipInitFunc sRegisterStudyModOverlay(RegisterStudyModOverlay);

} // namespace
