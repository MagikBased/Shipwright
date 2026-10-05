#include "JPAssistHistory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <memory>
#include <string>

#include <spdlog/fmt/fmt.h>
#include <imgui.h>

#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "StudyPersistence.h"
#include "StudyRepository.h"
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"

namespace JPAssist {
namespace {

std::shared_ptr<Ship::GuiWindow> sWindow;
std::array<char, 256> sSearch{};

std::string AsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool MatchesSearch(const HistoryEntry& entry, const StudyPage* page) {
    const std::string query = AsciiLower(sSearch.data());
    if (query.empty()) {
        return true;
    }
    const std::string id = fmt::format("{:#06x}", entry.textId);
    if (AsciiLower(id).find(query) != std::string::npos ||
        AsciiLower(entry.englishText).find(query) != std::string::npos) {
        return true;
    }
    return page != nullptr && AsciiLower(page->japanese).find(query) != std::string::npos;
}

std::string FormatTimestamp(int64_t unixTime) {
    if (unixTime <= 0) {
        return "Unknown time";
    }
    std::time_t value = static_cast<std::time_t>(unixTime);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::array<char, 32> result{};
    if (std::strftime(result.data(), result.size(), "%Y-%m-%d %H:%M", &local) == 0) {
        return "Unknown time";
    }
    return result.data();
}

class HistoryWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void UpdateElement() override {
    }

    void DrawElement() override {
        ImFont* japaneseFont = OTRGlobals::Instance != nullptr ? OTRGlobals::Instance->fontJapanese : nullptr;
        if (japaneseFont != nullptr) {
            ImGui::PushFont(japaneseFont);
        }

        ImGui::TextUnformatted("Dialogue History");
        ImGui::TextDisabled("The 20 most recently opened dialogue lines, newest first.");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##JPAssistHistorySearch", "Search Japanese, English, or text ID...", sSearch.data(),
                                 sSearch.size());
        ImGui::Separator();

        const auto& history = StudyPersistence_GetHistory();
        int matchCount = 0;
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            const StudyPage* page = StudyRepository_FindPage(it->textId, 0);
            if (!MatchesSearch(*it, page)) {
                continue;
            }
            matchCount++;
            ImGui::PushID(static_cast<int>(history.rend() - it));
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "%#06x", it->textId);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", FormatTimestamp(it->unixTime).c_str());
            if (page != nullptr && !page->japanese.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(page->japanese.c_str());
                ImGui::PopTextWrapPos();
            }
            if (!it->englishText.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(ImVec4(0.76f, 0.79f, 0.84f, 1.0f), "%s", it->englishText.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::Separator();
            ImGui::PopID();
        }

        if (history.empty()) {
            ImGui::TextDisabled("No dialogue has been recorded yet.");
        } else if (matchCount == 0) {
            ImGui::TextDisabled("No history entries match this search.");
        } else {
            ImGui::TextDisabled("%d of %zu entries", matchCount, history.size());
        }

        if (japaneseFont != nullptr) {
            ImGui::PopFont();
        }
    }
};

} // namespace

void JPAssistHistory_Register() {
    if (sWindow != nullptr) {
        return;
    }
    sWindow = std::make_shared<HistoryWindow>(CVAR_WINDOW("JPAssistHistory"), "JP Assist Dialogue History",
                                               ImVec2(680, 620));
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}

} // namespace JPAssist
