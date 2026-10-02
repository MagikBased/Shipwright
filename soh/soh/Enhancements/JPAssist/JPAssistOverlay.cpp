#include "JPAssistOverlay.h"
#include "JPAssistOverlayLayout.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"

namespace JPAssist {
namespace {

enum class OverlayMode { Hidden, Dialogue, Study };

struct OverlayState {
    OverlayMode mode = OverlayMode::Hidden;
    std::string languageLabel;
    std::string dialogueText;
    StudyPage studyPage;
    int selectedTokenIndex = 0;
    bool saved = false;
    int encounterCount = 0;
    bool showEnglishSentence = false;
    float pendingStudyScroll = 0.0f;
};

OverlayState sState;
std::mutex sStateMutex;

class JPAssistOverlayWindow final : public Ship::GuiWindow {
  public:
    JPAssistOverlayWindow()
        : GuiWindow("", true, "JP Assist Overlay", ImVec2(-1, -1),
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs) {
    }

    void InitElement() override {
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
        const bool study = mFrameState.mode == OverlayMode::Study;
        const OverlayLayout layout = JPAssistOverlay_ComputeLayout(viewport->WorkPos.x, viewport->WorkPos.y,
                                                                   viewport->WorkSize.x, viewport->WorkSize.y,
                                                                   CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardScale"),
                                                                                1.0f),
                                                                   study);
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
        if (mFrameState.mode == OverlayMode::Dialogue) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "%s", mFrameState.languageLabel.c_str());
            ImGui::Separator();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(mFrameState.dialogueText.c_str());
            ImGui::PopTextWrapPos();
            restoreFont();
            return;
        }

        if (mFrameState.studyPage.tokens.empty()) {
            ImGui::TextUnformatted("No reviewed token data is available for this page.");
            restoreFont();
            return;
        }

        const int selected = std::clamp(mFrameState.selectedTokenIndex, 0,
                                        static_cast<int>(mFrameState.studyPage.tokens.size()) - 1);
        const StudyToken& token = mFrameState.studyPage.tokens[selected];

        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "STUDY MODE  %d/%d", selected + 1,
                           static_cast<int>(mFrameState.studyPage.tokens.size()));
        ImGui::Separator();

        // Keep the controls visible while allowing long sentences, token
        // rows, definitions, and notes to scroll within the fixed-size card.
        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("JPAssistStudyContent", ImVec2(0.0f, -footerHeight), false);
        ImGui::PushTextWrapPos(0.0f);
        const std::string& sentence =
            mFrameState.showEnglishSentence ? mFrameState.studyPage.english : mFrameState.studyPage.japanese;
        ImGui::TextUnformatted(sentence.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const float contentRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        for (size_t i = 0; i < mFrameState.studyPage.tokens.size(); ++i) {
            const float tokenWidth = ImGui::CalcTextSize(mFrameState.studyPage.tokens[i].surface.c_str()).x;
            // TextColored advances to the next line. Move back beside the
            // previous token only when this token still fits in the card.
            if (i > 0 && ImGui::GetItemRectMax().x + tokenWidth <= contentRight) {
                ImGui::SameLine(0.0f, 0.0f);
            }
            const ImVec4 color = static_cast<int>(i) == selected ? ImVec4(1.0f, 0.82f, 0.25f, 1.0f)
                                                                  : ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
            ImGui::TextColored(color, "%s", mFrameState.studyPage.tokens[i].surface.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), "%s", token.surface.c_str());
        if (!token.reading.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", token.reading.c_str());
        }
        ImGui::TextDisabled("%s", token.partOfSpeech.c_str());
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(token.meaning.empty() ? "Definition pending review" : token.meaning.c_str());
        if (!token.note.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.88f, 1.0f), "%s", token.note.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (mFrameState.pendingStudyScroll != 0.0f) {
            ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() + mFrameState.pendingStudyScroll, 0.0f,
                                         ImGui::GetScrollMaxY()));
        }
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::TextDisabled("%s  Seen %d time%s",
                            mFrameState.saved ? "Saved to study list" : "C-Right: save word",
                            mFrameState.encounterCount, mFrameState.encounterCount == 1 ? "" : "s");
        ImGui::TextDisabled("D-L/R: word  D-U/D: scroll  L/Z: language  R/B: close");
        restoreFont();
    }

  private:
    OverlayState mFrameState;
    float mFrameScale = 1.0f;
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

void JPAssistOverlay_ShowDialogue(const std::string& languageLabel, const std::string& text) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    sState.mode = OverlayMode::Dialogue;
    sState.languageLabel = languageLabel;
    sState.dialogueText = text;
}

void JPAssistOverlay_ShowStudy(const StudyPage& page, int selectedTokenIndex, bool saved, int encounterCount,
                               bool showEnglishSentence) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    if (sState.mode != OverlayMode::Study) {
        sState.pendingStudyScroll = -100000.0f;
    }
    sState.mode = OverlayMode::Study;
    sState.studyPage = page;
    sState.selectedTokenIndex = selectedTokenIndex;
    sState.saved = saved;
    sState.encounterCount = encounterCount;
    sState.showEnglishSentence = showEnglishSentence;
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
