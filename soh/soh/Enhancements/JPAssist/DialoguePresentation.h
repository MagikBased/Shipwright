#pragma once

#include <cstdint>
#include <string_view>

namespace DialogueStudy {

// Portable presentation contract shared by the learning feature and a game
// adapter. It intentionally contains no Shipwright, ImGui, or OoT types.
enum class DialogueDisplayMode : int32_t {
    NativeSwap = 0,
    AttachedTranslation = 1,
    JapaneseOnly = 2,
};

enum class DialogueSurface : uint8_t {
    Hidden,
    NativeTextbox,
    AttachedPanel,
};

struct PresentationCapabilities {
    bool nativeTextReplacement = false;
    bool attachedPanel = true;
    bool dialogueAnchor = false;
    bool controllerGlyphs = false;
};

struct PresentationPlan {
    DialogueSurface surface = DialogueSurface::Hidden;
    bool usedFallback = false;
};

constexpr bool AllowsOrdinaryDialogueToggle(DialogueDisplayMode mode) {
    return mode != DialogueDisplayMode::JapaneseOnly;
}

constexpr PresentationPlan ResolvePresentationPlan(DialogueDisplayMode mode,
                                                   const PresentationCapabilities& capabilities) {
    switch (mode) {
        case DialogueDisplayMode::NativeSwap:
            if (capabilities.nativeTextReplacement) {
                return { DialogueSurface::NativeTextbox, false };
            }
            return { capabilities.attachedPanel ? DialogueSurface::AttachedPanel : DialogueSurface::Hidden, true };
        case DialogueDisplayMode::AttachedTranslation:
            return { capabilities.attachedPanel ? DialogueSurface::AttachedPanel : DialogueSurface::Hidden,
                     !capabilities.attachedPanel };
        case DialogueDisplayMode::JapaneseOnly:
        default:
            return { DialogueSurface::Hidden, false };
    }
}

class PresentationHost {
  public:
    virtual ~PresentationHost() = default;

    virtual PresentationCapabilities GetCapabilities() const = 0;
    virtual bool ShowNativeReplacement(std::string_view languageTag, std::string_view text) = 0;
    virtual void ShowAttachedTranslation(std::string_view languageTag, std::string_view text) = 0;
    virtual void RestoreNativeDialogue() = 0;
    virtual void HideDialoguePresentation() = 0;
};

} // namespace DialogueStudy
