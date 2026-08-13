#include "JPAssistOverlay.h"

#include <algorithm>
#include <memory>

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
};

OverlayState sState;

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
        if (sState.mode == OverlayMode::Hidden) {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float scale = CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardScale"), 1.0f);
        const float margin = 24.0f * scale;
        const bool study = sState.mode == OverlayMode::Study;
        const float width = (study ? 420.0f : 760.0f) * scale;
        const float height = (study ? 460.0f : 120.0f) * scale;
        const ImVec2 position = study
                                    ? ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - margin,
                                             viewport->WorkPos.y + margin)
                                    : ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - width) * 0.5f,
                                             viewport->WorkPos.y + viewport->WorkSize.y - height - margin);

        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
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

        ImGui::SetWindowFontScale(CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardScale"), 1.0f));
        if (sState.mode == OverlayMode::Dialogue) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "%s", sState.languageLabel.c_str());
            ImGui::Separator();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(sState.dialogueText.c_str());
            ImGui::PopTextWrapPos();
            restoreFont();
            return;
        }

        if (sState.studyPage.tokens.empty()) {
            ImGui::TextUnformatted("No reviewed token data is available for this page.");
            restoreFont();
            return;
        }

        const int selected = std::clamp(sState.selectedTokenIndex, 0,
                                        static_cast<int>(sState.studyPage.tokens.size()) - 1);
        const StudyToken& token = sState.studyPage.tokens[selected];

        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "STUDY MODE  %d/%d", selected + 1,
                           static_cast<int>(sState.studyPage.tokens.size()));
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        const std::string& sentence = sState.showEnglishSentence ? sState.studyPage.english : sState.studyPage.japanese;
        ImGui::TextUnformatted(sentence.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        for (size_t i = 0; i < sState.studyPage.tokens.size(); ++i) {
            if (i > 0) {
                ImGui::SameLine();
            }
            const ImVec4 color = static_cast<int>(i) == selected ? ImVec4(1.0f, 0.82f, 0.25f, 1.0f)
                                                                  : ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
            ImGui::TextColored(color, "%s", sState.studyPage.tokens[i].surface.c_str());
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
        ImGui::TextDisabled("%s  Seen %d time%s", sState.saved ? "Saved to study list" : "C-Right: save word",
                            sState.encounterCount, sState.encounterCount == 1 ? "" : "s");
        ImGui::TextDisabled("D-Left/Right: word   L/Z: language   R/B: close");
        restoreFont();
    }
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
    sState.mode = OverlayMode::Dialogue;
    sState.languageLabel = languageLabel;
    sState.dialogueText = text;
}

void JPAssistOverlay_ShowStudy(const StudyPage& page, int selectedTokenIndex, bool saved, int encounterCount,
                               bool showEnglishSentence) {
    sState.mode = OverlayMode::Study;
    sState.studyPage = page;
    sState.selectedTokenIndex = selectedTokenIndex;
    sState.saved = saved;
    sState.encounterCount = encounterCount;
    sState.showEnglishSentence = showEnglishSentence;
}

void JPAssistOverlay_Hide() {
    sState = OverlayState{};
}

} // namespace JPAssist
