#pragma once

#include <cstdint>
#include <string>

#include "StudyRepository.h"

namespace JPAssist {

// Registers one frame-safe GuiWindow with Ship's normal GUI draw loop.
void JPAssistOverlay_Register();

// Dialogue preview is used by the non-destructive language-switch scaffold.
// It intentionally does not mutate MessageContext; later runtime testing can
// replace this presentation adapter with native textbox replacement.
void JPAssistOverlay_ShowDialogue(const std::string& languageLabel, const std::string& text);

// Study Mode renders the normalized page, selectable token strip, and an
// Anki-like card. The manager owns navigation; the overlay is presentation-only.
void JPAssistOverlay_ShowStudy(const StudyPage& page, int selectedTokenIndex, bool saved, int encounterCount,
                               bool showEnglishSentence);
void JPAssistOverlay_Hide();

} // namespace JPAssist
