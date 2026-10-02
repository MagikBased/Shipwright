#include "JPAssistOverlay.h"
#include "JPAssistOverlayLayout.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <fast/Fast3dGui.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"
#include "assets/soh_assets.h"

extern "C" {
#include "textures/kanji/kanji.h"
}

namespace JPAssist {
namespace {

enum class OverlayMode { Hidden, Study };

struct OverlayState {
    OverlayMode mode = OverlayMode::Hidden;
    StudyPage studyPage;
    int selectedTokenIndex = 0;
    float pendingStudyScroll = 0.0f;
};

OverlayState sState;
std::mutex sStateMutex;

constexpr const char* kDPadGlyph = "JPAssist.DialogueGlyph.DPad";
constexpr const char* kCRightGlyph = "JPAssist.DialogueGlyph.CRight";
constexpr const char* kAGlyph = "JPAssist.DialogueGlyph.A";
constexpr const char* kRGlyph = "JPAssist.DialogueGlyph.R";

class JPAssistOverlayWindow final : public Ship::GuiWindow {
  public:
    JPAssistOverlayWindow()
        : GuiWindow("", true, "JP Assist Overlay", ImVec2(-1, -1),
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs) {
    }

    void InitElement() override {
        mFast3dGui = std::dynamic_pointer_cast<Fast::Fast3dGui>(
            Ship::Context::GetRawInstance()->GetWindow()->GetGui());
        if (mFast3dGui == nullptr) {
            return;
        }

        // Cache game-native glyph art under overlay-specific names so it can
        // be tinted like the corresponding controller buttons.
        // The Japanese archive stops just before the dialogue font's western
        // D-pad character, so use Shipwright's native HUD D-pad art for this
        // one control instead of trying to load an absent resource.
        LoadGlyph(kDPadGlyph, gDPadTex, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
        LoadGlyph(kCRightGlyph, gMsgKanji83A8ButtonCRightTex, ImVec4(1.0f, 0.82f, 0.22f, 1.0f));
        LoadGlyph(kAGlyph, gMsgKanji839FButtonATex, ImVec4(0.35f, 0.72f, 1.0f, 1.0f));
        LoadGlyph(kRGlyph, gMsgKanji83A3ButtonRTex, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
    }
    void UpdateElement() override {
    }

    void Draw() override {
        {
            std::lock_guard<std::mutex> lock(sStateMutex);
            mFrameState = sState;
            sState.pendingStudyScroll = 0.0f;
        }
        if (mFrameState.mode == OverlayMode::Hidden) {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const OverlayLayout layout = JPAssistOverlay_ComputeLayout(viewport->WorkPos.x, viewport->WorkPos.y,
                                                                   viewport->WorkSize.x, viewport->WorkSize.y,
                                                                   CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardScale"),
                                                                                1.0f));
        const float scale = layout.scale;
        mFrameScale = scale;

        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(ImVec2(layout.x, layout.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(layout.width, layout.height), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              ImVec4(0.035f, 0.045f, 0.065f,
                                     CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardOpacity"), 0.92f)));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.70f, 0.90f, 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * scale, 14.0f * scale));
        ImGui::SetNextWindowBgAlpha(1.0f);
        GuiWindow::Draw();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    void DrawElement() override {
        ImFont* japaneseFont = OTRGlobals::Instance != nullptr ? OTRGlobals::Instance->fontJapanese : nullptr;
        if (japaneseFont != nullptr) {
            ImGui::PushFont(japaneseFont);
        }
        const auto restoreFont = [japaneseFont]() {
            if (japaneseFont != nullptr) {
                ImGui::PopFont();
            }
        };

        ImGui::SetWindowFontScale(mFrameScale);
        if (mFrameState.studyPage.tokens.empty()) {
            ImGui::TextUnformatted("No reviewed token data is available for this page.");
            restoreFont();
            return;
        }

        const int selected = std::clamp(mFrameState.selectedTokenIndex, 0,
                                        static_cast<int>(mFrameState.studyPage.tokens.size()) - 1);
        const StudyToken& token = mFrameState.studyPage.tokens[selected];

        // The card deliberately contains only learning content. Controller
        // hints and status labels made this panel substantially taller than
        // the original translation overlay and duplicated stable controls.
        ImGui::BeginChild("JPAssistStudyContent", ImVec2(0.0f, 0.0f), false);
        if (ImGui::BeginTable("JPAssistStudyColumns", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("English", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("Word", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const float englishColumnLeft = ImGui::GetCursorScreenPos().x;
            const float englishColumnRight = englishColumnLeft + ImGui::GetContentRegionAvail().x;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(mFrameState.studyPage.english.empty() ? "Translation unavailable"
                                                                         : mFrameState.studyPage.english.c_str());
            ImGui::PopTextWrapPos();
            const float englishTextBottom = ImGui::GetItemRectMax().y;

            ImGui::TableSetColumnIndex(1);
            const float headerRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), "%s", token.surface.c_str());
            if (!token.reading.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", token.reading.c_str());
            }
            if (!token.partOfSpeech.empty()) {
                const float partOfSpeechWidth = ImGui::CalcTextSize(token.partOfSpeech.c_str()).x;
                const float partOfSpeechX = headerRight - partOfSpeechWidth;
                const float minimumGap = ImGui::GetStyle().ItemSpacing.x;
                if (partOfSpeechX >= ImGui::GetItemRectMax().x + minimumGap) {
                    const ImVec2 nextLineCursor = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(partOfSpeechX, ImGui::GetItemRectMin().y));
                    ImGui::TextDisabled("%s", token.partOfSpeech.c_str());
                    ImGui::SetCursorScreenPos(nextLineCursor);
                } else {
                    ImGui::TextDisabled("%s", token.partOfSpeech.c_str());
                }
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(token.meaning.empty() ? "Definition pending review" : token.meaning.c_str());
            if (!token.note.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.88f, 1.0f), "%s", token.note.c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTable();

            // Keep the definition column's full height. The compact glyph
            // rail occupies only otherwise-empty space below the translation
            // and disappears gracefully when unusually long English text
            // needs that space.
            DrawControlHints(englishColumnLeft, englishColumnRight, englishTextBottom);
        }
        if (mFrameState.pendingStudyScroll != 0.0f) {
            ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() + mFrameState.pendingStudyScroll, 0.0f,
                                         ImGui::GetScrollMaxY()));
        }
        ImGui::EndChild();
        restoreFont();
    }

  private:
    void LoadGlyph(const char* cacheName, const char* resourcePath, const ImVec4& tint) {
        if (!mFast3dGui->HasTextureByName(cacheName)) {
            mFast3dGui->LoadGuiTexture(cacheName, resourcePath, "", tint);
        }
    }

    void DrawControlHints(float left, float right, float textBottom) const {
        if (mFast3dGui == nullptr) {
            return;
        }

        struct Hint {
            const char* texture;
            const char* label;
        };
        constexpr Hint hints[] = {
            { kDPadGlyph, "move / scroll" },
            { kCRightGlyph, "save" },
            { kAGlyph, "next" },
            { kRGlyph, "close" },
        };

        const float glyphSize = 16.0f * mFrameScale;
        const float iconLabelGap = 3.0f * mFrameScale;
        const float groupGap = 10.0f * mFrameScale;
        const float hintY = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y -
                            ImGui::GetStyle().WindowPadding.y - glyphSize;
        if (hintY < textBottom + 4.0f * mFrameScale) {
            return;
        }

        float totalWidth = 0.0f;
        for (size_t i = 0; i < std::size(hints); ++i) {
            totalWidth += glyphSize + iconLabelGap + ImGui::CalcTextSize(hints[i].label).x;
            if (i + 1 < std::size(hints)) {
                totalWidth += groupGap;
            }
        }
        if (totalWidth > right - left) {
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        float x = left;
        for (size_t i = 0; i < std::size(hints); ++i) {
            ImTextureID texture = mFast3dGui->GetTextureByName(hints[i].texture);
            if (texture != nullptr) {
                drawList->AddImage(texture, ImVec2(x, hintY), ImVec2(x + glyphSize, hintY + glyphSize));
            }
            x += glyphSize + iconLabelGap;
            const ImVec2 labelSize = ImGui::CalcTextSize(hints[i].label);
            drawList->AddText(ImVec2(x, hintY + (glyphSize - labelSize.y) * 0.5f), labelColor, hints[i].label);
            x += labelSize.x;
            if (i + 1 < std::size(hints)) {
                x += groupGap;
            }
        }
    }

    OverlayState mFrameState;
    float mFrameScale = 1.0f;
    std::shared_ptr<Fast::Fast3dGui> mFast3dGui;
};

std::shared_ptr<JPAssistOverlayWindow> sWindow;

} // namespace

void JPAssistOverlay_Register() {
    if (sWindow != nullptr) {
        return;
    }
    sWindow = std::make_shared<JPAssistOverlayWindow>();
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->AddGuiWindow(sWindow);
    sWindow->Show();
}

bool JPAssistOverlay_HasJapaneseFont() {
    return OTRGlobals::Instance != nullptr && OTRGlobals::Instance->fontJapanese != nullptr;
}

void JPAssistOverlay_ShowStudy(const StudyPage& page, int selectedTokenIndex) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    if (sState.mode != OverlayMode::Study) {
        sState.pendingStudyScroll = -100000.0f;
    }
    sState.mode = OverlayMode::Study;
    sState.studyPage = page;
    sState.selectedTokenIndex = selectedTokenIndex;
}

void JPAssistOverlay_ScrollStudy(float pixels) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    if (sState.mode == OverlayMode::Study) {
        sState.pendingStudyScroll += pixels;
    }
}

void JPAssistOverlay_Hide() {
    std::lock_guard<std::mutex> lock(sStateMutex);
    sState = OverlayState{};
}

} // namespace JPAssist
