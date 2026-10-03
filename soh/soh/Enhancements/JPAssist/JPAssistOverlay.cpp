#include "JPAssistOverlay.h"
#include "JPAssistOverlayLayout.h"
#include "JPAssistNativeHighlight.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
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
constexpr const char* kAGlyph = "JPAssist.DialogueGlyph.A";
constexpr const char* kBGlyph = "JPAssist.DialogueGlyph.B";
constexpr const char* kCGlyph = "JPAssist.DialogueGlyph.C";
constexpr const char* kLGlyph = "JPAssist.DialogueGlyph.L";
constexpr const char* kRGlyph = "JPAssist.DialogueGlyph.R";
constexpr const char* kZGlyph = "JPAssist.DialogueGlyph.Z";
constexpr const char* kCUpGlyph = "JPAssist.DialogueGlyph.CUp";
constexpr const char* kCDownGlyph = "JPAssist.DialogueGlyph.CDown";
constexpr const char* kCLeftGlyph = "JPAssist.DialogueGlyph.CLeft";
constexpr const char* kCRightGlyph = "JPAssist.DialogueGlyph.CRight";
constexpr const char* kZTargetGlyph = "JPAssist.DialogueGlyph.ZTarget";
constexpr const char* kControlStickGlyph = "JPAssist.DialogueGlyph.ControlStick";

class JPAssistOverlayWindow final : public Ship::GuiWindow {
  public:
    JPAssistOverlayWindow()
        : GuiWindow("", true, "JP Assist Overlay", ImVec2(-1, -1),
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse) {
    }

    void InitElement() override {
        // The GUI owns this window during normal operation. Do not retain the
        // GUI through this process-lifetime overlay handle: doing so delays
        // Gui destruction until static teardown, after the logger is gone.
        mFast3dGui = dynamic_cast<Fast::Fast3dGui*>(Ship::Context::GetRawInstance()->GetWindow()->GetGui().get());
        if (mFast3dGui == nullptr) {
            return;
        }

        // Cache game-native glyph art under overlay-specific names so it can
        // be tinted like the corresponding controller buttons.
        // The Japanese archive stops just before the dialogue font's western
        // D-pad character, so use Shipwright's native HUD D-pad art for this
        // one control instead of trying to load an absent resource.
        LoadGlyph(kDPadGlyph, gDPadTex, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
        LoadGlyph(kAGlyph, gMsgKanji839FButtonATex, ImVec4(0.35f, 0.72f, 1.0f, 1.0f));
        LoadGlyph(kBGlyph, gMsgKanji83A0ButtonBTex, ImVec4(0.35f, 0.90f, 0.42f, 1.0f));
        LoadGlyph(kCGlyph, gMsgKanji83A1ButtonCTex, ImVec4(1.0f, 0.82f, 0.22f, 1.0f));
        LoadGlyph(kLGlyph, gMsgKanji83A2ButtonLTex, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
        LoadGlyph(kRGlyph, gMsgKanji83A3ButtonRTex, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
        LoadGlyph(kZGlyph, gMsgKanji83A4ButtonZTex, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
        LoadGlyph(kCUpGlyph, gMsgKanji83A5ButtonCUpTex, ImVec4(1.0f, 0.82f, 0.22f, 1.0f));
        LoadGlyph(kCDownGlyph, gMsgKanji83A6ButtonCDownTex, ImVec4(1.0f, 0.82f, 0.22f, 1.0f));
        LoadGlyph(kCLeftGlyph, gMsgKanji83A7ButtonCLeftTex, ImVec4(1.0f, 0.82f, 0.22f, 1.0f));
        LoadGlyph(kCRightGlyph, gMsgKanji83A8ButtonCRightTex, ImVec4(1.0f, 0.82f, 0.22f, 1.0f));
        LoadGlyph(kZTargetGlyph, gMsgKanji83A9ZTargetSignTex, ImVec4(0.45f, 0.68f, 1.0f, 1.0f));
        LoadGlyph(kControlStickGlyph, gMsgKanji83AAControlStickTex, ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
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
        JPAssistNativeTextboxBounds nativeBounds = {};
        const bool hasNativeBounds = JPAssist_GetNativeTextboxBounds(&nativeBounds) &&
                                     nativeBounds.logicalScreenHeight > 0 && nativeBounds.height > 0;
        const float nativeHeight = static_cast<float>(nativeBounds.logicalScreenHeight);
        const float nativeTop = hasNativeBounds ? static_cast<float>(nativeBounds.y) / nativeHeight : 0.0f;
        const float nativeBottom = hasNativeBounds
                                       ? static_cast<float>(nativeBounds.y + nativeBounds.height) / nativeHeight
                                       : 0.0f;
        const OverlayLayout layout = JPAssistOverlay_ComputeAdaptiveLayout(
            viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y,
            CVarGetFloat(CVAR_ENHANCEMENT("JPAssist.CardScale"), 1.0f), hasNativeBounds, nativeTop, nativeBottom);
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
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float sectionHeight = std::max(available.y, 1.0f);
        const float separatorGutter = std::max(ImGui::GetStyle().ItemSpacing.x, 6.0f * mFrameScale);
        const float sectionWidth = std::max(available.x - separatorGutter * 2.0f, 3.0f);
        constexpr float totalWeight = 0.80f + 0.40f + 1.00f;
        const float englishWidth = sectionWidth * 0.80f / totalWeight;
        const float wordWidth = sectionWidth * 0.40f / totalWeight;
        const float definitionWidth = sectionWidth - englishWidth - wordWidth;

        ImGui::BeginChild("JPAssistEnglishSection", ImVec2(englishWidth, sectionHeight), false,
                          ImGuiWindowFlags_NoScrollWithMouse);
        const float englishColumnLeft = ImGui::GetCursorScreenPos().x;
        const float englishColumnRight = englishColumnLeft + ImGui::GetContentRegionAvail().x;
        const float englishTextBottom = DrawEnglishText(
            mFrameState.studyPage.english.empty() ? "Translation unavailable" : mFrameState.studyPage.english,
            englishColumnRight - englishColumnLeft);
        DrawControlHints(englishColumnLeft, englishColumnRight, englishTextBottom);
        ApplyPendingStudyScroll();
        ImGui::EndChild();

        DrawSectionSeparator(sectionHeight, separatorGutter);
        ImGui::BeginChild("JPAssistWordSection", ImVec2(wordWidth, sectionHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawWordBlock(token);
        ImGui::EndChild();

        DrawSectionSeparator(sectionHeight, separatorGutter);
        ImGui::BeginChild("JPAssistDefinitionSection", ImVec2(definitionWidth, sectionHeight), false,
                          ImGuiWindowFlags_NoScrollWithMouse);
        const std::string metadata = BuildMetadataLabel(token);
        if (!metadata.empty()) {
            const float headerLeft = ImGui::GetCursorScreenPos().x;
            const float headerRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            const float metadataWidth = ImGui::CalcTextSize(metadata.c_str()).x;
            if (metadataWidth <= headerRight - headerLeft) {
                ImGui::SetCursorScreenPos(ImVec2(headerRight - metadataWidth, ImGui::GetCursorScreenPos().y));
            }
            ImGui::PushTextWrapPos(headerRight);
            ImGui::TextDisabled("%s", metadata.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(token.meaning.empty() ? "Definition pending review" : token.meaning.c_str());
        ImGui::PopTextWrapPos();
        ApplyPendingStudyScroll();
        ImGui::EndChild();
        restoreFont();
    }

  private:
    static std::string BuildMetadataLabel(const StudyToken& token) {
        std::string label = token.partOfSpeech;
        if (!label.empty() && static_cast<unsigned char>(label.front()) < 0x80) {
            label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
        }
        if (!token.note.empty()) {
            if (!label.empty()) {
                label += " - ";
            }
            label += token.note;
        }
        return label;
    }

    void DrawSectionSeparator(float height, float gutter) const {
        ImGui::SameLine(0.0f, 0.0f);
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float x = position.x + gutter * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, position.y), ImVec2(x, position.y + height),
                                            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy(ImVec2(gutter, height));
        ImGui::SameLine(0.0f, 0.0f);
    }

    void ApplyPendingStudyScroll() const {
        const float scrollMax = ImGui::GetScrollMaxY();
        if (mFrameState.pendingStudyScroll != 0.0f && scrollMax > 0.0f) {
            ImGui::SetScrollY(
                std::clamp(ImGui::GetScrollY() + mFrameState.pendingStudyScroll, 0.0f, scrollMax));
        }
    }

    void LoadGlyph(const char* cacheName, const char* resourcePath, const ImVec4& tint) {
        if (!mFast3dGui->HasTextureByName(cacheName)) {
            mFast3dGui->LoadGuiTexture(cacheName, resourcePath, "", tint);
        }
    }

    void DrawWordBlock(const StudyToken& token) const {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImFont* font = ImGui::GetFont();
        const float baseFontSize = ImGui::GetFontSize();
        const float horizontalPadding = 4.0f * mFrameScale;
        const float usableWidth = std::max(available.x - horizontalPadding * 2.0f, 1.0f);
        const bool showFurigana = !token.reading.empty() && token.reading != token.surface;

        const ImVec2 baseSurfaceSize = ImGui::CalcTextSize(token.surface.c_str());
        float surfaceScale = 2.15f;
        if (baseSurfaceSize.x > 0.0f) {
            surfaceScale = std::min(surfaceScale, usableWidth / baseSurfaceSize.x);
        }
        surfaceScale = std::max(surfaceScale, 0.85f);
        const float surfaceFontSize = baseFontSize * surfaceScale;
        const ImVec2 surfaceSize(baseSurfaceSize.x * surfaceScale, baseSurfaceSize.y * surfaceScale);

        ImVec2 readingSize(0.0f, 0.0f);
        if (showFurigana) {
            readingSize = font->CalcTextSizeA(baseFontSize, FLT_MAX, usableWidth, token.reading.c_str());
        }

        const float furiganaGap = showFurigana ? 2.0f * mFrameScale : 0.0f;
        const float contentHeight = readingSize.y + furiganaGap + surfaceSize.y;
        const float y = start.y + std::max((available.y - contentHeight) * 0.5f, 0.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float surfaceY = y;
        if (showFurigana) {
            const float readingX = start.x + horizontalPadding + (usableWidth - readingSize.x) * 0.5f;
            drawList->AddText(font, baseFontSize, ImVec2(readingX, y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                              token.reading.c_str(), nullptr, usableWidth);
            surfaceY += readingSize.y + furiganaGap;
        }

        const float surfaceX = start.x + (available.x - surfaceSize.x) * 0.5f;
        drawList->AddText(font, surfaceFontSize, ImVec2(surfaceX, surfaceY),
                          ImGui::GetColorU32(ImVec4(1.0f, 0.82f, 0.25f, 1.0f)), token.surface.c_str());

        // Advance by the content's real height even though the custom-size
        // text was drawn directly. Reserving the full remaining child height
        // here makes the table's own padding create a small, false overflow
        // and an otherwise-useless scrollbar.
        ImGui::Dummy(ImVec2(available.x, contentHeight));
    }

    float DrawEnglishText(const std::string& text, float availableWidth) const {
        struct GlyphMarker {
            const char* marker;
            const char* texture;
        };
        // Put the longer C-button names before the generic [C] marker.
        constexpr GlyphMarker markers[] = {
            { "[Control Stick]", kControlStickGlyph },
            { "[C-Right]", kCRightGlyph },
            { "[C-Down]", kCDownGlyph },
            { "[C-Left]", kCLeftGlyph },
            { "[Z-target]", kZTargetGlyph },
            { "[C-Up]", kCUpGlyph },
            { "[A]", kAGlyph },
            { "[B]", kBGlyph },
            { "[C]", kCGlyph },
            { "[L]", kLGlyph },
            { "[R]", kRGlyph },
            { "[Z]", kZGlyph },
        };

        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float right = start.x + std::max(availableWidth, 1.0f);
        const float glyphSize = 16.0f * mFrameScale;
        const float lineHeight = std::max(ImGui::GetTextLineHeight(), glyphSize);
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float x = start.x;
        float y = start.y;

        const auto nextLine = [&]() {
            x = start.x;
            y += lineHeight;
        };
        const auto findMarker = [&](size_t offset) -> const GlyphMarker* {
            for (const GlyphMarker& marker : markers) {
                const size_t length = std::char_traits<char>::length(marker.marker);
                if (text.compare(offset, length, marker.marker) == 0) {
                    return &marker;
                }
            }
            return nullptr;
        };

        for (size_t offset = 0; offset < text.size();) {
            if (text[offset] == '\n') {
                nextLine();
                ++offset;
                continue;
            }

            if (text[offset] == ' ' || text[offset] == '\t') {
                const float spaceWidth = ImGui::CalcTextSize(text[offset] == '\t' ? "    " : " ").x;
                if (x + spaceWidth > right && x > start.x) {
                    nextLine();
                } else {
                    x += spaceWidth;
                }
                ++offset;
                continue;
            }

            if (const GlyphMarker* marker = findMarker(offset); marker != nullptr) {
                if (x + glyphSize > right && x > start.x) {
                    nextLine();
                }
                ImTextureID texture =
                    mFast3dGui != nullptr ? mFast3dGui->GetTextureByName(marker->texture) : nullptr;
                if (texture != nullptr) {
                    const float glyphY = y + (lineHeight - glyphSize) * 0.5f;
                    drawList->AddImage(texture, ImVec2(x, glyphY), ImVec2(x + glyphSize, glyphY + glyphSize));
                    x += glyphSize;
                } else {
                    drawList->AddText(ImVec2(x, y), textColor, marker->marker);
                    x += ImGui::CalcTextSize(marker->marker).x;
                }
                offset += std::char_traits<char>::length(marker->marker);
                continue;
            }

            size_t end = offset + 1;
            while (end < text.size() && text[end] != '\n' && text[end] != ' ' && text[end] != '\t' &&
                   findMarker(end) == nullptr) {
                ++end;
            }
            const char* wordBegin = text.data() + offset;
            const char* wordEnd = text.data() + end;
            const float wordWidth = ImGui::CalcTextSize(wordBegin, wordEnd).x;
            if (x + wordWidth > right && x > start.x) {
                nextLine();
            }
            drawList->AddText(ImVec2(x, y), textColor, wordBegin, wordEnd);
            x += wordWidth;
            offset = end;
        }

        ImGui::Dummy(ImVec2(availableWidth, lineHeight + y - start.y));
        return ImGui::GetItemRectMax().y;
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
    Fast::Fast3dGui* mFast3dGui = nullptr;
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
    if (sState.mode != OverlayMode::Study || sState.selectedTokenIndex != selectedTokenIndex ||
        sState.studyPage.japanese != page.japanese) {
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
