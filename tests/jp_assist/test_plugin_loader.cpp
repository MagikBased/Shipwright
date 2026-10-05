#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ship/resource/archive/O2rArchive.h"
#include "ship/scripting/ScriptLoader.h"
#include "StudyModApiInternal.h"

namespace {

constexpr int32_t kPluginReady = 1;

struct DrawCapture {
    int filledRects = 0;
    int outlinedRects = 0;
    int lines = 0;
    int texts = 0;
    int clipPushes = 0;
    int clipPops = 0;
    int glyphs = 0;
    bool simulateLongEnglish = false;
    float englishY = 0.0f;
    float definitionY = 0.0f;
    StudyModRect lastFilledRect{};
};

void CaptureFilledRect(void* context, StudyModRect rect, uint32_t, float) {
    auto* capture = static_cast<DrawCapture*>(context);
    capture->filledRects++;
    if (rect.width > 100.0f) {
        capture->lastFilledRect = rect;
    }
}

void CaptureRect(void* context, StudyModRect, uint32_t, float, float) {
    static_cast<DrawCapture*>(context)->outlinedRects++;
}

void CaptureLine(void* context, StudyModVec2, StudyModVec2, uint32_t, float) {
    static_cast<DrawCapture*>(context)->lines++;
}

void CaptureText(void* context, StudyModVec2 position, float, uint32_t, const char* text, float, uint32_t) {
    auto* capture = static_cast<DrawCapture*>(context);
    capture->texts++;
    if (std::string(text) == "A forest shield.") {
        capture->englishY = position.y;
    } else if (std::string(text) == "shield") {
        capture->definitionY = position.y;
    }
}

StudyModVec2 CaptureMeasureText(void* context, float, const char* text, float wrapWidth, uint32_t) {
    const auto* capture = static_cast<DrawCapture*>(context);
    return { wrapWidth, capture->simulateLongEnglish && std::string(text) == "A forest shield." ? 300.0f : 20.0f };
}

void CapturePushClip(void* context, StudyModRect, int32_t) {
    static_cast<DrawCapture*>(context)->clipPushes++;
}

void CapturePopClip(void* context) {
    static_cast<DrawCapture*>(context)->clipPops++;
}

int32_t CaptureButtonGlyph(void* context, uint64_t, StudyModRect, uint32_t) {
    static_cast<DrawCapture*>(context)->glyphs++;
    return 1;
}

void AppendLe16(std::string& bytes, uint16_t value) {
    bytes.push_back(static_cast<char>(value & 0xFF));
    bytes.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void AppendLe32(std::string& bytes, uint32_t value) {
    AppendLe16(bytes, static_cast<uint16_t>(value & 0xFFFF));
    AppendLe16(bytes, static_cast<uint16_t>(value >> 16));
}

std::string TestWav() {
    std::string bytes;
    bytes.append("RIFF", 4);
    AppendLe32(bytes, 40);
    bytes.append("WAVEfmt ", 8);
    AppendLe32(bytes, 16);
    AppendLe16(bytes, 1);
    AppendLe16(bytes, 1);
    AppendLe32(bytes, 32000);
    AppendLe32(bytes, 64000);
    AppendLe16(bytes, 2);
    AppendLe16(bytes, 16);
    bytes.append("data", 4);
    AppendLe32(bytes, 4);
    AppendLe16(bytes, 1000);
    AppendLe16(bytes, static_cast<uint16_t>(-1000));
    return bytes;
}

TEST(PluginPackage, LoadsAndNegotiatesHostAbi) {
    const auto writableRoot = std::filesystem::temp_directory_path() / "jpassist-plugin-test";
    std::error_code cleanupError;
    std::filesystem::remove_all(writableRoot, cleanupError);
    StudyModApi::SetWritableRootForTesting(writableRoot.string());
    StudyModApi::SetIntSettingForTesting("jp-assist", "AccountSyncEnabled", 1);
    StudyModApi::SetResourceForTesting("jp_assist/runtime_data.json", R"json({
        "metadata":{"corpusVersion":"plugin-test"},
        "messages":{"0x1234":{"source":{"messageId":"0x1234"},"pages":[{
            "japanese":"森の盾", "english":"A forest shield.", "isChoice":false,
            "tokens":[
                {"surface":"森","lemma":"森","reading":"もり","partOfSpeech":"noun",
                 "meaning":"forest","start":0,"length":1},
                {"surface":"盾","lemma":"盾","reading":"たて","partOfSpeech":"noun",
                 "meaning":"shield","start":2,"length":1}
            ]
        }]},"0x2345":{"source":{"messageId":"0x2345"},"pages":[{
            "japanese":"石を取る", "english":"[A] Take the stone.", "isChoice":false,
            "tokens":[
                {"surface":"石","lemma":"石","reading":"いし","partOfSpeech":"noun",
                 "meaning":"stone","start":0,"length":1}
            ]
        }]}}
    })json");
    StudyModApi::SetResourceForTesting(
        "jp_assist/audio_manifest.json",
        R"json({"schemaVersion":1,"entries":[{"wordId":"盾|たて","wordAudio":"audio/shield.wav"}]})json");
    StudyModApi::SetResourceForTesting("jp_assist/audio/shield.wav", TestWav());
    auto archive = std::make_shared<Ship::O2rArchive>(JPASSIST_PLUGIN_ARCHIVE_PATH);
    archive->Load();
    ASSERT_TRUE(archive->IsLoaded());

    Ship::ScriptLoader loader({}, 1, "-g -Wl", {}, { JPASSIST_TCC_RUNTIME_DIR }, {});
    ASSERT_NO_THROW(loader.Compile(archive));
    ASSERT_NO_THROW(loader.LoadAll());
    EXPECT_TRUE(StudyModApi::HasRegisteredMod("jp-assist"));

    auto getStatus = reinterpret_cast<int32_t (*)()>(loader.GetFunction("jp-assist", "JPAssistPlugin_GetStatus"));
    ASSERT_NE(getStatus, nullptr);
    EXPECT_EQ(getStatus(), kPluginReady);

    auto getDialogueCount =
        reinterpret_cast<uint32_t (*)()>(loader.GetFunction("jp-assist", "JPAssistPlugin_GetDialogueEventCount"));
    auto getInputCount =
        reinterpret_cast<uint32_t (*)()>(loader.GetFunction("jp-assist", "JPAssistPlugin_GetInputEventCount"));
    auto getLifecycleCount = reinterpret_cast<uint32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_GetLifecycleEventCount"));
    auto setConsumeMask = reinterpret_cast<void (*)(uint64_t)>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_SetConsumeMask"));
    auto getHighlightQueryCount = reinterpret_cast<uint32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_GetHighlightQueryCount"));
    auto playTestAudio =
        reinterpret_cast<int32_t (*)()>(loader.GetFunction("jp-assist", "JPAssistPlugin_PlayTestAudio"));
    auto getOverlayFrameCount = reinterpret_cast<uint32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_GetOverlayFrameCount"));
    auto setRuntimeEnabled = reinterpret_cast<void (*)(int32_t)>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_SetRuntimeEnabled"));
    auto isCorpusLoaded = reinterpret_cast<int32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_IsCorpusLoaded"));
    auto getSelectedTokenIndex = reinterpret_cast<int32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_GetSelectedTokenIndex"));
    auto isDefinitionVisible = reinterpret_cast<int32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_IsDefinitionVisible"));
    auto isSelectedSaved = reinterpret_cast<int32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_IsSelectedTokenSaved"));
    auto isSelectedKnown = reinterpret_cast<int32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_IsSelectedTokenKnown"));
    auto getEncounterCount = reinterpret_cast<int32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_GetSelectedTokenEncounterCount"));
    auto selectedTokenHasAudio = reinterpret_cast<int32_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_SelectedTokenHasAudio"));
    auto getPendingSyncEventCount = reinterpret_cast<size_t (*)()>(
        loader.GetFunction("jp-assist", "JPAssistPlugin_GetPendingSyncEventCount"));
    ASSERT_NE(getDialogueCount, nullptr);
    ASSERT_NE(getInputCount, nullptr);
    ASSERT_NE(getLifecycleCount, nullptr);
    ASSERT_NE(setConsumeMask, nullptr);
    ASSERT_NE(getHighlightQueryCount, nullptr);
    ASSERT_NE(playTestAudio, nullptr);
    ASSERT_NE(getOverlayFrameCount, nullptr);
    ASSERT_NE(setRuntimeEnabled, nullptr);
    ASSERT_NE(isCorpusLoaded, nullptr);
    ASSERT_NE(getSelectedTokenIndex, nullptr);
    ASSERT_NE(isDefinitionVisible, nullptr);
    ASSERT_NE(isSelectedSaved, nullptr);
    ASSERT_NE(isSelectedKnown, nullptr);
    ASSERT_NE(getEncounterCount, nullptr);
    ASSERT_NE(selectedTokenHasAudio, nullptr);
    ASSERT_NE(getPendingSyncEventCount, nullptr);
    ASSERT_TRUE(isCorpusLoaded());
    const StudyModHostApi* hostApi = StudyMod_GetHostApi(1, 1);
    ASSERT_NE(hostApi, nullptr);
    EXPECT_NE(hostApi->capabilities & STUDY_MOD_CAP_SETTINGS, 0U);
    EXPECT_NE(hostApi->get_int_setting, nullptr);
    EXPECT_NE(hostApi->set_int_setting, nullptr);

    const StudyModDialogueEvent dialogue{ sizeof(StudyModDialogueEvent), STUDY_MOD_DIALOGUE_OPENED, 0x1234, 0,
                                          -1, 0, STUDY_MOD_LANGUAGE_JAPANESE, 0, {} };
    StudyModApi::DispatchDialogue(dialogue);
    EXPECT_EQ(getDialogueCount(), 1U);

    setConsumeMask(STUDY_MOD_INPUT_PRIMARY);
    const StudyModInputEvent input{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                    STUDY_MOD_INPUT_PRIMARY | STUDY_MOD_INPUT_SECONDARY, 0, 0, 0, 0 };
    EXPECT_EQ(StudyModApi::DispatchInput(input), STUDY_MOD_INPUT_PRIMARY);
    EXPECT_EQ(getInputCount(), 1U);

    setConsumeMask(STUDY_MOD_INPUT_CONSUME_STICK_X);
    EXPECT_EQ(StudyModApi::DispatchInput(input), STUDY_MOD_INPUT_CONSUME_STICK_X);

    const StudyModLifecycleEvent lifecycle{ sizeof(StudyModLifecycleEvent), STUDY_MOD_LIFECYCLE_SCENE_CHANGED, 7 };
    StudyModApi::DispatchLifecycle(lifecycle);
    EXPECT_EQ(getLifecycleCount(), 1U);

    StudyModNativeHighlight highlight{};
    EXPECT_FALSE(StudyModApi::QueryNativeHighlight(0x1234, highlight));

    setConsumeMask(0);
    StudyModApi::DispatchDialogue(dialogue);
    const StudyModInputEvent open{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                   STUDY_MOD_INPUT_RIGHT_SHOULDER, 0, 0, 0, 0 };
    EXPECT_EQ(StudyModApi::DispatchInput(open), STUDY_MOD_INPUT_RIGHT_SHOULDER);
    EXPECT_TRUE(isDefinitionVisible());
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.Active", 0), 1);
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.DefinitionVisible", 0), 1);
    EXPECT_TRUE(StudyModApi::QueryNativeHighlight(0x1234, highlight));
    EXPECT_EQ(highlight.start, 0U);
    EXPECT_EQ(highlight.length, 1U);

    const StudyModInputEvent next{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                   STUDY_MOD_INPUT_NAV_RIGHT | STUDY_MOD_INPUT_PRIMARY, 0, 0, 0, 0 };
    EXPECT_EQ(StudyModApi::DispatchInput(next), STUDY_MOD_INPUT_NAV_RIGHT);
    EXPECT_EQ(getSelectedTokenIndex(), 1);
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.SelectedIndex", -1), 1);
    EXPECT_EQ(getEncounterCount(), 1);
    EXPECT_TRUE(selectedTokenHasAudio());
    EXPECT_TRUE(StudyModApi::QueryNativeHighlight(0x1234, highlight));
    EXPECT_EQ(highlight.start, 2U);
    EXPECT_EQ(highlight.length, 1U);

    const StudyModInputEvent toggleDefinition{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                               STUDY_MOD_INPUT_LEFT_TRIGGER, 0, 0, 0, 0 };
    EXPECT_EQ(StudyModApi::DispatchInput(toggleDefinition), STUDY_MOD_INPUT_LEFT_TRIGGER);
    EXPECT_FALSE(isDefinitionVisible());
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.DefinitionVisible", 1), 0);
    EXPECT_GE(getHighlightQueryCount(), 3U);

    const StudyModInputEvent saveAndKnow{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                         STUDY_MOD_INPUT_FACE_RIGHT | STUDY_MOD_INPUT_FACE_LEFT, 0, 0, 0, 0 };
    EXPECT_EQ(StudyModApi::DispatchInput(saveAndKnow),
              STUDY_MOD_INPUT_FACE_RIGHT | STUDY_MOD_INPUT_FACE_LEFT);
    EXPECT_TRUE(isSelectedSaved());
    EXPECT_TRUE(isSelectedKnown());
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.Saved", 0), 1);
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.Known", 0), 1);
    EXPECT_TRUE(std::filesystem::exists(writableRoot / "jp-assist" / "jp_assist_progress.json"));
    EXPECT_GE(getPendingSyncEventCount(), 5U);
    EXPECT_TRUE(std::filesystem::exists(writableRoot / "jp-assist" / "jp_assist_sync.json"));

    const StudyModInputEvent listen{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                     STUDY_MOD_INPUT_FACE_DOWN, 0, 0, 0, 0 };
    EXPECT_EQ(StudyModApi::DispatchInput(listen), STUDY_MOD_INPUT_FACE_DOWN);
    EXPECT_TRUE(StudyModApi::IsAudioPlaying());
    int16_t spokenSamples[4] = {};
    StudyModApi::MixAudio(spokenSamples, 2);
    EXPECT_EQ(spokenSamples[0], 900);
    EXPECT_EQ(spokenSamples[1], 900);
    EXPECT_EQ(spokenSamples[2], -900);
    EXPECT_EQ(spokenSamples[3], -900);
    EXPECT_FALSE(StudyModApi::IsAudioPlaying());

    ASSERT_NE(playTestAudio(), 0);
    EXPECT_TRUE(StudyModApi::IsAudioPlaying());
    int16_t audioSamples[4] = {};
    StudyModApi::MixAudio(audioSamples, 2);
    EXPECT_EQ(audioSamples[0], 900);
    EXPECT_EQ(audioSamples[1], -900);
    EXPECT_EQ(audioSamples[2], 1800);
    EXPECT_EQ(audioSamples[3], -1800);
    EXPECT_FALSE(StudyModApi::IsAudioPlaying());
    DrawCapture drawCapture;
    const StudyModOverlayFrame overlayFrame{ sizeof(StudyModOverlayFrame),
                                              { 0.0f, 0.0f, 1280.0f, 720.0f, 1280.0f, 720.0f },
                                              1.0f / 60.0f,
                                              0,
                                              0,
                                              0,
                                              0,
                                              0,
                                              0 };
    const StudyModOverlayDrawApi drawApi{ sizeof(StudyModOverlayDrawApi), &drawCapture, CaptureFilledRect,
                                          CaptureRect, CaptureLine, CaptureText, CaptureMeasureText,
                                          CapturePushClip, CapturePopClip, CaptureButtonGlyph };
    hostApi->set_int_setting("jp-assist", "BeginPairing", 1);
    StudyModApi::DispatchOverlay(overlayFrame, drawApi);
    EXPECT_EQ(getOverlayFrameCount(), 1U);
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "BeginPairing", -1), 0);
    EXPECT_EQ(drawCapture.filledRects, 1);
    EXPECT_EQ(drawCapture.outlinedRects, 1);
    EXPECT_EQ(drawCapture.lines, 2);
    EXPECT_GE(drawCapture.texts, 5);
    EXPECT_EQ(drawCapture.clipPushes, 2);
    EXPECT_EQ(drawCapture.clipPops, 2);
    EXPECT_GE(drawCapture.glyphs, 2);

    drawCapture.simulateLongEnglish = true;
    StudyModApi::DispatchOverlay(overlayFrame, drawApi);
    const float englishBeforeScroll = drawCapture.englishY;
    const float definitionBeforeScroll = drawCapture.definitionY;
    const StudyModInputEvent scrollDown{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                         STUDY_MOD_INPUT_NAV_DOWN, 0, 0, 0, 0 };
    EXPECT_EQ(StudyModApi::DispatchInput(scrollDown), STUDY_MOD_INPUT_NAV_DOWN);
    StudyModApi::DispatchOverlay(overlayFrame, drawApi);
    EXPECT_LT(drawCapture.englishY, englishBeforeScroll);
    EXPECT_FLOAT_EQ(drawCapture.definitionY, definitionBeforeScroll);
    drawCapture.simulateLongEnglish = false;

    const StudyModRect originalPanel = drawCapture.lastFilledRect;
    StudyModOverlayFrame dragStart = overlayFrame;
    dragStart.mouse_x = originalPanel.x + 8.0f;
    dragStart.mouse_y = originalPanel.y + 4.0f;
    dragStart.mouse_pressed = STUDY_MOD_MOUSE_LEFT;
    dragStart.mouse_held = STUDY_MOD_MOUSE_LEFT;
    StudyModApi::DispatchOverlay(dragStart, drawApi);
    StudyModOverlayFrame dragMove = dragStart;
    dragMove.mouse_pressed = 0;
    dragMove.mouse_x -= 90.0f;
    dragMove.mouse_y += 45.0f;
    dragMove.modifiers = STUDY_MOD_MODIFIER_SHIFT;
    StudyModApi::DispatchOverlay(dragMove, drawApi);
    EXPECT_LT(drawCapture.lastFilledRect.x, originalPanel.x);
    EXPECT_FLOAT_EQ(drawCapture.lastFilledRect.y, originalPanel.y);
    StudyModOverlayFrame dragEnd = dragMove;
    dragEnd.mouse_held = 0;
    dragEnd.modifiers = 0;
    StudyModApi::DispatchOverlay(dragEnd, drawApi);
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Layout.NoDialogue.Valid", 0), 1);

    const StudyModRect movedPanel = drawCapture.lastFilledRect;
    StudyModOverlayFrame resizeStart = overlayFrame;
    resizeStart.mouse_x = movedPanel.x + movedPanel.width - 4.0f;
    resizeStart.mouse_y = movedPanel.y + movedPanel.height - 4.0f;
    resizeStart.mouse_pressed = STUDY_MOD_MOUSE_LEFT;
    resizeStart.mouse_held = STUDY_MOD_MOUSE_LEFT;
    StudyModApi::DispatchOverlay(resizeStart, drawApi);
    StudyModOverlayFrame resizeMove = resizeStart;
    resizeMove.mouse_pressed = 0;
    resizeMove.mouse_x += 80.0f;
    resizeMove.mouse_y += 40.0f;
    StudyModApi::DispatchOverlay(resizeMove, drawApi);
    EXPECT_GT(drawCapture.lastFilledRect.width, movedPanel.width);
    EXPECT_GT(drawCapture.lastFilledRect.height, movedPanel.height);
    StudyModOverlayFrame resizeEnd = resizeMove;
    resizeEnd.mouse_held = 0;
    StudyModApi::DispatchOverlay(resizeEnd, drawApi);

    const int glyphsBeforeEnglishMarker = drawCapture.glyphs;
    const StudyModDialogueEvent glyphDialogue{ sizeof(StudyModDialogueEvent), STUDY_MOD_DIALOGUE_UPDATED, 0x2345,
                                               0, -1, 0, STUDY_MOD_LANGUAGE_JAPANESE, 0, {} };
    StudyModApi::DispatchDialogue(glyphDialogue);
    StudyModApi::DispatchOverlay(overlayFrame, drawApi);
    EXPECT_GT(drawCapture.glyphs, glyphsBeforeEnglishMarker);

    const StudyModDialogueEvent choiceDialogue{ sizeof(StudyModDialogueEvent), STUDY_MOD_DIALOGUE_UPDATED, 0x1234,
                                                0, 1, 2, STUDY_MOD_LANGUAGE_JAPANESE,
                                                STUDY_MOD_DIALOGUE_FLAG_CHOICE, {} };
    StudyModApi::DispatchDialogue(choiceDialogue);
    const StudyModInputEvent choiceDeflection{ sizeof(StudyModInputEvent), STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE,
                                               STUDY_MOD_INPUT_NAV_DOWN, 0, 0, 0, -80 };
    EXPECT_EQ(StudyModApi::DispatchInput(choiceDeflection),
              STUDY_MOD_INPUT_NAV_DOWN | STUDY_MOD_INPUT_CONSUME_STICK_Y);

    StudyModApi::DispatchLifecycle(lifecycle);
    EXPECT_EQ(getLifecycleCount(), 2U);
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.Active", 1), 0);

    EXPECT_NO_THROW(loader.UnloadAll());
    EXPECT_FALSE(StudyModApi::HasRegisteredMod("jp-assist"));
    EXPECT_EQ(hostApi->get_int_setting("jp-assist", "Diagnostics.RuntimeEnabled", 1), 0);
    StudyModApi::ClearResourcesForTesting();
    std::filesystem::remove_all(writableRoot, cleanupError);
    EXPECT_EQ(StudyModApi::DispatchInput(input), 0U);
    EXPECT_FALSE(StudyModApi::QueryNativeHighlight(0x1234, highlight));
    EXPECT_FALSE(StudyModApi::IsAudioPlaying());
    StudyModApi::DispatchOverlay(overlayFrame, drawApi);
}

} // namespace
