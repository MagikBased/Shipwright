#include "PluginRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

namespace JPAssistPlugin {

namespace {

constexpr const char* kCorpusResource = "jp_assist/runtime_data.json";
constexpr uint32_t kPanelRgb = 0x0B111B00;
constexpr uint32_t kBorderColor = 0x55C7E8FF;
constexpr uint32_t kPrimaryText = 0xF2F5F7FF;
constexpr uint32_t kMutedText = 0x9AA7B0FF;
constexpr uint32_t kWordColor = 0xFFBF32FF;

uint32_t WithAlpha(uint32_t rgb, float opacity) {
    return (rgb & 0xFFFFFF00U) | static_cast<uint32_t>(std::clamp(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct GlyphMarker {
    const char* text;
    uint64_t button;
};

// Keep longer markers first so [C-Up] cannot be mistaken for a shorter token.
constexpr GlyphMarker kEnglishGlyphMarkers[] = {
    { "[C-Right]", STUDY_MOD_INPUT_FACE_RIGHT }, { "[C-Down]", STUDY_MOD_INPUT_FACE_DOWN },
    { "[C-Left]", STUDY_MOD_INPUT_FACE_LEFT },   { "[Z-target]", STUDY_MOD_INPUT_LEFT_TRIGGER },
    { "[C-Up]", STUDY_MOD_INPUT_FACE_UP },       { "[A]", STUDY_MOD_INPUT_PRIMARY },
    { "[B]", STUDY_MOD_INPUT_SECONDARY },        { "[L]", STUDY_MOD_INPUT_LEFT_SHOULDER },
    { "[R]", STUDY_MOD_INPUT_RIGHT_SHOULDER },   { "[Z]", STUDY_MOD_INPUT_LEFT_TRIGGER },
};

const GlyphMarker* FindEnglishGlyph(const std::string& text, size_t offset) {
    for (const auto& marker : kEnglishGlyphMarkers) {
        const size_t length = std::strlen(marker.text);
        if (text.compare(offset, length, marker.text) == 0) {
            return &marker;
        }
    }
    return nullptr;
}

float DrawEnglishWithGlyphs(const StudyModOverlayDrawApi& draw, StudyModVec2 start, float fontSize, uint32_t color,
                            const std::string& text, float width, bool supportsGlyph, bool render) {
    const float glyphSize = fontSize;
    const float lineHeight = std::max(fontSize * 1.15f, glyphSize);
    const float right = start.x + std::max(width, 1.0f);
    float x = start.x;
    float y = start.y;

    const auto measure = [&](const std::string& value) {
        return draw.measure_text == nullptr ? StudyModVec2{ fontSize * 0.5f * static_cast<float>(value.size()), fontSize }
                                            : draw.measure_text(draw.context, fontSize, value.c_str(), 0.0f, 0);
    };
    const auto nextLine = [&]() {
        x = start.x;
        y += lineHeight;
    };

    for (size_t offset = 0; offset < text.size();) {
        if (text[offset] == '\n') {
            nextLine();
            ++offset;
            continue;
        }
        if (text[offset] == ' ' || text[offset] == '\t') {
            const std::string whitespace = text[offset] == '\t' ? "    " : " ";
            const float spaceWidth = measure(whitespace).x;
            if (x + spaceWidth > right && x > start.x) {
                nextLine();
            } else {
                x += spaceWidth;
            }
            ++offset;
            continue;
        }
        if (const GlyphMarker* marker = FindEnglishGlyph(text, offset); marker != nullptr) {
            if (x + glyphSize > right && x > start.x) {
                nextLine();
            }
            const StudyModRect bounds{ x, y + (lineHeight - glyphSize) * 0.5f, glyphSize, glyphSize, 0.0f, 0.0f };
            if (!supportsGlyph || (render && draw.button_glyph(draw.context, marker->button, bounds, 0xFFFFFFFF) == 0)) {
                if (render) {
                    draw.text(draw.context, { x, y }, fontSize, color, marker->text, 0.0f, 0);
                }
                x += measure(marker->text).x;
            } else {
                x += glyphSize;
            }
            offset += std::strlen(marker->text);
            continue;
        }

        size_t end = offset + 1;
        while (end < text.size() && text[end] != '\n' && text[end] != ' ' && text[end] != '\t' &&
               FindEnglishGlyph(text, end) == nullptr) {
            ++end;
        }
        const std::string word = text.substr(offset, end - offset);
        const float wordWidth = measure(word).x;
        if (x + wordWidth > right && x > start.x) {
            nextLine();
        }
        if (render) {
            draw.text(draw.context, { x, y }, fontSize, color, word.c_str(), 0.0f, 0);
        }
        x += wordWidth;
        offset = end;
    }
    return y - start.y + lineHeight;
}

bool EnglishHasNativeGlyph(const std::string& text) {
    for (size_t offset = 0; offset < text.size(); ++offset) {
        if (FindEnglishGlyph(text, offset) != nullptr) {
            return true;
        }
    }
    return false;
}

JPAssist::StudyCommand CommandsFromInput(const StudyModInputEvent& input, bool active) {
    using JPAssist::StudyCommand;
    StudyCommand commands = StudyCommand::None;
    const uint64_t pressed = input.pressed;
    if (!active) {
        if ((pressed & STUDY_MOD_INPUT_RIGHT_SHOULDER) != 0) {
            commands = commands | StudyCommand::OpenWithDefinition;
        }
        if ((pressed & (STUDY_MOD_INPUT_LEFT_SHOULDER | STUDY_MOD_INPUT_LEFT_TRIGGER)) != 0) {
            commands = commands | StudyCommand::OpenRecallFirst;
        }
        return commands;
    }
    if ((pressed & STUDY_MOD_INPUT_RIGHT_SHOULDER) != 0) {
        commands = commands | StudyCommand::Close;
    }
    if ((pressed & (STUDY_MOD_INPUT_LEFT_SHOULDER | STUDY_MOD_INPUT_LEFT_TRIGGER)) != 0) {
        commands = commands | StudyCommand::ToggleDefinition;
    }
    if ((pressed & STUDY_MOD_INPUT_NAV_LEFT) != 0) {
        commands = commands | StudyCommand::PreviousWord;
    }
    if ((pressed & STUDY_MOD_INPUT_NAV_RIGHT) != 0) {
        commands = commands | StudyCommand::NextWord;
    }
    if ((pressed & STUDY_MOD_INPUT_NAV_UP) != 0) {
        commands = commands | StudyCommand::ScrollUp;
    }
    if ((pressed & STUDY_MOD_INPUT_NAV_DOWN) != 0) {
        commands = commands | StudyCommand::ScrollDown;
    }
    if ((pressed & STUDY_MOD_INPUT_FACE_RIGHT) != 0) {
        commands = commands | StudyCommand::ToggleSaved;
    }
    if ((pressed & STUDY_MOD_INPUT_FACE_LEFT) != 0) {
        commands = commands | StudyCommand::MarkKnown;
    }
    if ((pressed & STUDY_MOD_INPUT_FACE_DOWN) != 0) {
        commands = commands | StudyCommand::PlayAudio;
    }
    return commands;
}

} // namespace

bool Runtime::Initialize(const StudyModHostApi& host) {
    mHost = &host;
    mProgress.Initialize(host, "jp-assist");
    mAudio.Initialize(host, "jp-assist");
    mLayout.Initialize(host, "jp-assist");
    const bool corpusLoaded = mCorpus.LoadFromHost(host, kCorpusResource);
    mSync.Initialize(host, "jp-assist", mCorpus.Version());
    RefreshEnabledSetting();
    return corpusLoaded;
}

void Runtime::Shutdown() {
    mSync.Shutdown();
    mProgress.Save();
    mSession.ClearDialogue();
    mDialogueId = 0;
    mPageIndex = 0;
    mHost = nullptr;
}

void Runtime::SetEnabled(bool enabled) {
    if (mEnabled == enabled) {
        return;
    }
    mEnabled = enabled;
    if (enabled) {
        // The source-runtime transition may have just copied legacy state into
        // private plugin storage. Reload at the handoff boundary.
        mProgress.Load();
        mSync.Reload();
    } else {
        mSession.Exit();
        mProgress.Save();
    }
    PublishDiagnostics();
}

bool Runtime::IsEnabled() const { return mEnabled; }
bool Runtime::IsCorpusLoaded() const { return mCorpus.IsLoaded(); }
int Runtime::SelectedTokenIndex() const { return mSession.SelectedTokenIndex(); }
bool Runtime::DefinitionVisible() const { return mSession.IsDefinitionVisible(); }
bool Runtime::SelectedTokenSaved() const {
    const Token* token = CurrentToken();
    return token != nullptr && mProgress.IsSaved(token->Id());
}
bool Runtime::SelectedTokenKnown() const {
    const Token* token = CurrentToken();
    return token != nullptr && mProgress.IsKnown(token->Id(), token->senseId);
}
bool Runtime::SelectedTokenHasAudio() const {
    const Token* token = CurrentToken();
    return token != nullptr && mAudio.HasWord(token->Id());
}
int Runtime::SelectedTokenEncounterCount() const {
    const Token* token = CurrentToken();
    return token == nullptr ? 0 : mProgress.EncounterCount(token->Id());
}
size_t Runtime::PendingSyncEventCount() const { return mSync.PendingCount(); }

void Runtime::RefreshEnabledSetting() {
    if (mHost != nullptr && mHost->get_int_setting != nullptr) {
        const bool masterEnabled = mHost->get_int_setting("jp-assist", "Enabled", 1) != 0;
        SetEnabled(masterEnabled);
        if (mHost->get_int_setting("jp-assist", "ReloadState", 0) != 0) {
            mHost->set_int_setting("jp-assist", "ReloadState", 0);
            mProgress.Load();
            mSync.Reload();
        }
    }
}

void Runtime::PublishDiagnostics() {
    if (mHost == nullptr || mHost->set_int_setting == nullptr) {
        return;
    }
    const Token* token = CurrentToken();
    const Page* page = CurrentPage();
    const auto& counters = mSession.Counters();
    const auto narrow = [](uint64_t value) {
        return static_cast<int32_t>(std::min<uint64_t>(value, std::numeric_limits<int32_t>::max()));
    };
    const std::array<int32_t, 20> values{
        static_cast<int32_t>(mDialogueId),
        static_cast<int32_t>(mPageIndex),
        mEnabled && mSession.IsActive() ? 1 : 0,
        mSession.IsDefinitionVisible() ? 1 : 0,
        mSession.IsChoiceFrozen() ? 1 : 0,
        mChoiceIndex,
        mSession.SelectedTokenIndex(),
        page == nullptr ? 0 : static_cast<int32_t>(page->tokens.size()),
        page != nullptr && page->isChoice ? 1 : 0,
        token != nullptr && mProgress.IsSaved(token->Id()) ? 1 : 0,
        token != nullptr && mProgress.IsKnown(token->Id(), token->senseId) ? 1 : 0,
        token != nullptr && mAudio.HasWord(token->Id()) ? 1 : 0,
        narrow(counters.enter),
        narrow(counters.navigation),
        narrow(counters.scroll),
        narrow(counters.saveToggle),
        narrow(counters.knownMark),
        narrow(counters.definitionToggle),
        narrow(counters.audioPlay),
        mEnabled ? 1 : 0,
    };
    if (mHasPublishedDiagnostics && values == mPublishedDiagnostics) {
        return;
    }
    constexpr const char* keys[] = {
        "Diagnostics.TextId",          "Diagnostics.PageIndex",       "Diagnostics.Active",
        "Diagnostics.DefinitionVisible", "Diagnostics.ChoiceFrozen", "Diagnostics.ChoiceIndex",
        "Diagnostics.SelectedIndex",  "Diagnostics.TokenCount",      "Diagnostics.IsChoice",
        "Diagnostics.Saved",          "Diagnostics.Known",           "Diagnostics.AudioAvailable",
        "Diagnostics.EnterCount",     "Diagnostics.NavigationCount", "Diagnostics.ScrollCount",
        "Diagnostics.SaveCount",      "Diagnostics.KnownCount",      "Diagnostics.DefinitionCount",
        "Diagnostics.AudioCount",     "Diagnostics.RuntimeEnabled",
    };
    for (size_t index = 0; index < values.size(); ++index) {
        mHost->set_int_setting("jp-assist", keys[index], values[index]);
    }
    mPublishedDiagnostics = values;
    mHasPublishedDiagnostics = true;
}

const Page* Runtime::CurrentPage() const {
    return mCorpus.FindPage(mDialogueId, mPageIndex);
}

const Token* Runtime::CurrentToken() const {
    const Page* page = CurrentPage();
    const int index = mSession.SelectedTokenIndex();
    return page != nullptr && index >= 0 && index < static_cast<int>(page->tokens.size()) ? &page->tokens[index]
                                                                                         : nullptr;
}

void Runtime::Retarget(uint64_t dialogueId, uint32_t pageIndex, uint32_t flags,
                       const StudyModRect& textboxBounds) {
    mDialogueId = dialogueId;
    mPageIndex = pageIndex;
    mTextboxBounds = textboxBounds;
    const Page* page = CurrentPage();
    const int count = page == nullptr ? 0 : static_cast<int>(page->tokens.size());
    // The live host state is authoritative. Corpus metadata is retained as a
    // fallback for hosts that cannot identify choice pages, but must never
    // override a choice the game is currently presenting.
    const bool choice = (page != nullptr && page->isChoice) || (flags & STUDY_MOD_DIALOGUE_FLAG_CHOICE) != 0;
    mSession.RetargetDialogue(static_cast<uint16_t>(dialogueId), static_cast<int>(pageIndex), count, choice);
}

void Runtime::OnDialogue(const StudyModDialogueEvent& event) {
    RefreshEnabledSetting();
    if (!mEnabled) {
        PublishDiagnostics();
        return;
    }
    if (event.type == STUDY_MOD_DIALOGUE_CLOSED) {
        mProgress.Save();
        mSession.ClearDialogue();
        mDialogueId = 0;
        mChoiceIndex = -1;
        mScrollPosition = 0.0f;
        PublishDiagnostics();
        return;
    }
    mChoiceIndex = event.choice_index;
    Retarget(event.dialogue_id, event.page_index, event.flags, event.textbox_bounds);
    if (event.type == STUDY_MOD_DIALOGUE_OPENED || event.type == STUDY_MOD_DIALOGUE_PAGE_CHANGED) {
        mSync.RecordDialogue("dialogue_seen", event.dialogue_id, event.page_index);
    }
    PublishDiagnostics();
}

uint64_t Runtime::OnInput(const StudyModInputEvent& event) {
    RefreshEnabledSetting();
    mSync.RefreshEnabled();
    if (!mEnabled || (event.flags & STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE) == 0 || CurrentPage() == nullptr) {
        PublishDiagnostics();
        return 0;
    }
    const bool wasActive = mSession.IsActive();
    const auto result = mSession.HandleCommands(CommandsFromInput(event, wasActive),
                                                static_cast<uint8_t>(std::clamp(mChoiceIndex, 0, 255)));
    const Token* token = CurrentToken();
    if ((result.entered || result.selectionChanged) && token != nullptr) {
        if (result.selectionChanged) {
            mScrollPosition = 0.0f;
        }
        mProgress.RecordEncounter(token->Id());
        mSync.RecordWord("word_encountered", *token, mDialogueId, mPageIndex);
    }
    if (result.entered) {
        mSync.RecordDialogue("study_mode_opened", mDialogueId, mPageIndex);
    }
    if (result.toggleSaved && token != nullptr) {
        const bool saved = mProgress.ToggleSaved(token->Id());
        mProgress.Save();
        mSync.RecordWord(saved ? "word_saved" : "word_unsaved", *token, mDialogueId, mPageIndex);
    }
    if (result.markKnown && token != nullptr && mProgress.MarkKnown(token->Id(), token->senseId)) {
        mSession.RecordKnownMarked();
        mProgress.Save();
        mSync.RecordWord("word_known", *token, mDialogueId, mPageIndex);
    }
    if (result.playAudio && token != nullptr && mAudio.PlayWord(token->Id())) {
        mSession.RecordAudioPlayed();
    }
    if (result.scrollPixels != 0.0f) {
        mScrollPosition = std::max(mScrollPosition + result.scrollPixels, 0.0f);
    }
    if (result.exited) {
        mProgress.Save();
    }
    PublishDiagnostics();
    if (result.consumeEntryCommands) {
        return event.pressed & (STUDY_MOD_INPUT_RIGHT_SHOULDER | STUDY_MOD_INPUT_LEFT_SHOULDER |
                                STUDY_MOD_INPUT_LEFT_TRIGGER);
    }
    if (!result.consumeStudyCommands) {
        return 0;
    }
    // Primary/A and face-up/C-up deliberately pass through so dialogue can advance.
    constexpr uint64_t owned = STUDY_MOD_INPUT_RIGHT_SHOULDER | STUDY_MOD_INPUT_LEFT_SHOULDER |
                               STUDY_MOD_INPUT_LEFT_TRIGGER | STUDY_MOD_INPUT_FACE_LEFT |
                               STUDY_MOD_INPUT_FACE_RIGHT | STUDY_MOD_INPUT_FACE_DOWN | STUDY_MOD_INPUT_NAV_UP |
                               STUDY_MOD_INPUT_NAV_DOWN | STUDY_MOD_INPUT_NAV_LEFT | STUDY_MOD_INPUT_NAV_RIGHT;
    const uint64_t analogConsumption = mSession.IsChoiceFrozen() ? STUDY_MOD_INPUT_CONSUME_STICK_Y : 0;
    return (event.pressed & owned) | analogConsumption;
}

void Runtime::OnLifecycle(const StudyModLifecycleEvent&) {
    mSync.RefreshEnabled();
    if (mEnabled) {
        mProgress.Save();
        mSession.ClearDialogue();
        mDialogueId = 0;
        mChoiceIndex = -1;
    }
    PublishDiagnostics();
}

int32_t Runtime::OnNativeHighlight(uint64_t dialogueId, StudyModNativeHighlight& highlight) {
    const Token* token = CurrentToken();
    if (!mEnabled || !mSession.IsActive() || dialogueId != mDialogueId || token == nullptr) {
        return 0;
    }
    highlight.struct_size = sizeof(StudyModNativeHighlight);
    highlight.start = token->start;
    highlight.length = token->length;
    highlight.style = STUDY_MOD_HIGHLIGHT_GLOW;
    highlight.rgba = 0xA06080FF;
    return 1;
}

void Runtime::OnOverlay(const StudyModOverlayFrame& frame, const StudyModOverlayDrawApi& draw) {
    RefreshEnabledSetting();
    // Account UI commands must be observed even while no dialogue is active.
    // Overlay callbacks provide a lightweight main-thread heartbeat for that bridge.
    mSync.RefreshEnabled();
    PublishDiagnostics();
    const Page* page = CurrentPage();
    const Token* token = CurrentToken();
    if (!mEnabled || !mSession.IsActive() || page == nullptr || token == nullptr || draw.rect_filled == nullptr ||
        draw.rect == nullptr || draw.line == nullptr || draw.text == nullptr) {
        return;
    }

    const InteractiveLayout layout = mLayout.Update(frame, mTextboxBounds);
    const StudyModRect& panel = layout.panel;
    const float x = panel.x;
    const float y = panel.y;
    const float width = panel.width;
    const float height = panel.height;
    const float scale = layout.scale;
    const float opacity = mHost != nullptr && mHost->get_float_setting != nullptr
                              ? mHost->get_float_setting("jp-assist", "CardOpacity", 0.92f)
                              : 0.92f;
    draw.rect_filled(draw.context, panel, WithAlpha(kPanelRgb, opacity), 12.0f * scale);
    draw.rect(draw.context, panel, kBorderColor, 12.0f * scale, std::max(scale, 1.0f));
    if (layout.showCenterGuides) {
        draw.line(draw.context, { frame.viewport.x + frame.viewport.width * 0.5f, frame.viewport.y },
                  { frame.viewport.x + frame.viewport.width * 0.5f, frame.viewport.y + frame.viewport.height },
                  0x59C7FF38, 1.0f);
        draw.line(draw.context, { frame.viewport.x, frame.viewport.y + frame.viewport.height * 0.5f },
                  { frame.viewport.x + frame.viewport.width, frame.viewport.y + frame.viewport.height * 0.5f },
                  0x59C7FF38, 1.0f);
    }
    if (layout.snappedX) {
        draw.line(draw.context, { layout.guideX, frame.viewport.y },
                  { layout.guideX, frame.viewport.y + frame.viewport.height }, kBorderColor, 1.5f);
    }
    if (layout.snappedY) {
        draw.line(draw.context, { frame.viewport.x, layout.guideY },
                  { frame.viewport.x + frame.viewport.width, layout.guideY }, kBorderColor, 1.5f);
    }

    const float leftDivider = x + width * 0.36f;
    const float rightDivider = x + width * 0.56f;
    draw.line(draw.context, { leftDivider, y + 14.0f * scale },
              { leftDivider, y + height - 14.0f * scale }, 0x4F5C6660, std::max(scale, 1.0f));
    draw.line(draw.context, { rightDivider, y + 14.0f * scale },
              { rightDivider, y + height - 14.0f * scale }, 0x4F5C6660, std::max(scale, 1.0f));

    const float contentTop = y + 16.0f * scale;
    const float contentHeight = std::max(height - 32.0f * scale, 1.0f);
    const float englishWidth = std::max(leftDivider - x - 32.0f * scale, 1.0f);
    const float definitionWidth = std::max(x + width - rightDivider - 34.0f * scale, 1.0f);
    const bool supportsClip =
        draw.struct_size >= offsetof(StudyModOverlayDrawApi, pop_clip_rect) + sizeof(draw.pop_clip_rect) &&
        draw.push_clip_rect != nullptr && draw.pop_clip_rect != nullptr;
    const bool supportsGlyph =
        draw.struct_size >= offsetof(StudyModOverlayDrawApi, button_glyph) + sizeof(draw.button_glyph) &&
        draw.button_glyph != nullptr;
    const bool englishHasGlyph = EnglishHasNativeGlyph(page->english);
    const StudyModVec2 englishSize =
        englishHasGlyph
            ? StudyModVec2{ englishWidth,
                            DrawEnglishWithGlyphs(draw, { 0.0f, 0.0f }, 20.0f * scale, kPrimaryText,
                                                  page->english, englishWidth, supportsGlyph, false) }
            : draw.measure_text == nullptr
                  ? StudyModVec2{}
                  : draw.measure_text(draw.context, 20.0f * scale, page->english.c_str(), englishWidth, 0);

    std::string heading = token->partOfSpeech;
    if (!token->note.empty()) {
        heading += " - " + token->note;
    }
    if (mProgress.IsKnown(token->Id(), token->senseId)) {
        heading += heading.empty() ? "Known" : " - Known";
    }
    const float definitionBodyHeight = mSession.IsDefinitionVisible() && draw.measure_text != nullptr
                                           ? draw.measure_text(draw.context, 21.0f * scale, token->meaning.c_str(),
                                                               definitionWidth, 0)
                                                 .y
                                           : 21.0f * scale;
    const float definitionContentHeight = 34.0f * scale + definitionBodyHeight;
    const float englishMaximumScroll = std::max(englishSize.y - contentHeight, 0.0f);
    const float definitionMaximumScroll =
        mSession.IsDefinitionVisible() ? std::max(definitionContentHeight - contentHeight, 0.0f) : 0.0f;
    const float maximumScroll = std::max(englishMaximumScroll, definitionMaximumScroll);
    if (frame.mouse_wheel != 0.0f && frame.mouse_x >= x && frame.mouse_x <= x + width && frame.mouse_y >= y &&
        frame.mouse_y <= y + height) {
        mScrollPosition = std::max(mScrollPosition - frame.mouse_wheel * 48.0f * scale, 0.0f);
    }
    mScrollPosition = std::clamp(mScrollPosition, 0.0f, maximumScroll);
    const float englishScroll = std::min(mScrollPosition, englishMaximumScroll);
    const float definitionScroll = std::min(mScrollPosition, definitionMaximumScroll);

    const StudyModRect englishClip{ x + 14.0f * scale, contentTop, englishWidth + 4.0f * scale, contentHeight,
                                    panel.logical_screen_width, panel.logical_screen_height };
    if (supportsClip) {
        draw.push_clip_rect(draw.context, englishClip, 1);
    }
    if (englishHasGlyph) {
        DrawEnglishWithGlyphs(draw, { x + 16.0f * scale, contentTop - englishScroll }, 20.0f * scale, kPrimaryText,
                              page->english, englishWidth, supportsGlyph, true);
    } else {
        draw.text(draw.context, { x + 16.0f * scale, contentTop - englishScroll }, 20.0f * scale, kPrimaryText,
                  page->english.c_str(), englishWidth, 0);
    }
    if (supportsGlyph && englishMaximumScroll == 0.0f &&
        englishSize.y + 28.0f * scale <= contentHeight) {
        struct Hint {
            uint64_t button;
            const char* label;
            bool visible;
        };
        const Hint hints[] = {
            { STUDY_MOD_INPUT_NAV_LEFT, "move", true },
            { STUDY_MOD_INPUT_FACE_LEFT, mProgress.IsKnown(token->Id(), token->senseId) ? "known" : "know", true },
            { STUDY_MOD_INPUT_FACE_DOWN, "listen", mAudio.HasWord(token->Id()) },
            { STUDY_MOD_INPUT_FACE_RIGHT, mProgress.IsSaved(token->Id()) ? "saved" : "save", true },
            { STUDY_MOD_INPUT_PRIMARY, "next", true },
            { STUDY_MOD_INPUT_RIGHT_SHOULDER, "close", true },
        };
        const float glyphSize = 15.0f * scale;
        const float labelSize = 14.0f * scale;
        const float gap = 3.0f * scale;
        const float groupGap = 8.0f * scale;
        float hintX = x + 16.0f * scale;
        const float hintY = contentTop + contentHeight - glyphSize;
        for (const Hint& hint : hints) {
            if (!hint.visible) {
                continue;
            }
            const float labelWidth = draw.measure_text == nullptr
                                         ? labelSize * 0.5f * static_cast<float>(std::strlen(hint.label))
                                         : draw.measure_text(draw.context, labelSize, hint.label, 0.0f, 0).x;
            const float groupWidth = glyphSize + gap + labelWidth;
            if (hintX + groupWidth > leftDivider - 8.0f * scale) {
                break;
            }
            draw.button_glyph(draw.context, hint.button,
                              { hintX, hintY, glyphSize, glyphSize, panel.logical_screen_width,
                                panel.logical_screen_height },
                              0xFFFFFFFF);
            hintX += glyphSize + gap;
            draw.text(draw.context, { hintX, hintY }, labelSize, kMutedText, hint.label, 0.0f, 0);
            hintX += labelWidth + groupGap;
        }
    }
    if (supportsClip) {
        draw.pop_clip_rect(draw.context);
    }

    const std::string& reading = token->dictionaryReading.empty() ? token->reading : token->dictionaryReading;
    draw.text(draw.context, { leftDivider + 18.0f * scale, y + 18.0f * scale }, 20.0f * scale, kMutedText,
              reading.c_str(), rightDivider - leftDivider - 36.0f * scale, STUDY_MOD_TEXT_JAPANESE);
    draw.text(draw.context, { leftDivider + 18.0f * scale, y + 55.0f * scale }, 38.0f * scale, kWordColor,
              token->lemma.c_str(), rightDivider - leftDivider - 36.0f * scale, STUDY_MOD_TEXT_JAPANESE);

    const StudyModRect definitionClip{ rightDivider + 14.0f * scale, contentTop,
                                       definitionWidth + 4.0f * scale, contentHeight,
                                       panel.logical_screen_width, panel.logical_screen_height };
    if (supportsClip) {
        draw.push_clip_rect(draw.context, definitionClip, 1);
    }
    draw.text(draw.context, { rightDivider + 16.0f * scale, contentTop - definitionScroll }, 18.0f * scale,
              kMutedText,
              heading.c_str(), definitionWidth, 0);
    if (mSession.IsDefinitionVisible()) {
        draw.text(draw.context, { rightDivider + 16.0f * scale, contentTop + 34.0f * scale - definitionScroll },
                  21.0f * scale, kPrimaryText, token->meaning.c_str(), definitionWidth, 0);
    } else if (supportsGlyph) {
        const float glyphSize = 24.0f * scale;
        float promptX = rightDivider + 16.0f * scale;
        const float promptY = contentTop + 38.0f * scale;
        const auto glyph = [&](uint64_t button) {
            draw.button_glyph(draw.context, button,
                              { promptX, promptY, glyphSize, glyphSize, panel.logical_screen_width,
                                panel.logical_screen_height },
                              0xFFFFFFFF);
            promptX += glyphSize + 4.0f * scale;
        };
        glyph(STUDY_MOD_INPUT_LEFT_SHOULDER);
        draw.text(draw.context, { promptX, promptY }, 18.0f * scale, kMutedText, "/", 0.0f, 0);
        promptX += 12.0f * scale;
        glyph(STUDY_MOD_INPUT_LEFT_TRIGGER);
        draw.text(draw.context, { promptX + 4.0f * scale, promptY }, 20.0f * scale, kPrimaryText, "Reveal",
                  definitionWidth, 0);
    } else {
        draw.text(draw.context, { rightDivider + 16.0f * scale, contentTop + 34.0f * scale }, 21.0f * scale,
                  kPrimaryText, "L/Z  Reveal", definitionWidth, 0);
    }
    if (supportsClip) {
        draw.pop_clip_rect(draw.context);
    }

    const auto drawScrollbar = [&](float right, float maximum, float offset) {
        if (maximum <= 0.0f) {
            return;
        }
        const float trackHeight = contentHeight;
        const float thumbHeight = std::max(trackHeight * trackHeight / (trackHeight + maximum), 18.0f * scale);
        const float thumbY = contentTop + (trackHeight - thumbHeight) * (offset / maximum);
        draw.rect_filled(draw.context,
                         { right - 5.0f * scale, contentTop, 3.0f * scale, trackHeight, panel.logical_screen_width,
                           panel.logical_screen_height },
                         0x2E394440, 2.0f * scale);
        draw.rect_filled(draw.context,
                         { right - 5.0f * scale, thumbY, 3.0f * scale, thumbHeight, panel.logical_screen_width,
                           panel.logical_screen_height },
                         0x9AA7B0A0, 2.0f * scale);
    };
    drawScrollbar(leftDivider, englishMaximumScroll, englishScroll);
    drawScrollbar(x + width - 3.0f * scale, definitionMaximumScroll, definitionScroll);
}

} // namespace JPAssistPlugin
