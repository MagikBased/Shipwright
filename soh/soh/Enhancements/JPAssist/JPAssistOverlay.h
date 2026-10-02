#pragma once

#include <cstdint>
#include <string>

#include "StudyRepository.h"

namespace JPAssist {

// Registers one frame-safe GuiWindow with Ship's normal GUI draw loop.
void JPAssistOverlay_Register();
bool JPAssistOverlay_HasJapaneseFont();

// Study Mode combines the official English line with the selected word's
// Anki-like card. The manager owns navigation; the overlay is presentation-only.
void JPAssistOverlay_ShowStudy(const StudyPage& page, int selectedTokenIndex, bool saved, int encounterCount);
void JPAssistOverlay_ScrollStudy(float pixels);
void JPAssistOverlay_Hide();

} // namespace JPAssist
