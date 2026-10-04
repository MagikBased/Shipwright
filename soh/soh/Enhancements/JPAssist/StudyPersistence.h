#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Study progress, stored separately from OoT save files (design doc section
// 9: "Store JP Assist state separately from OoT save files"). Backed by
// jp_assist_progress.json under Ship::Context::GetPathRelativeToAppDirectory,
// the same directory shipofharkinian.json/Save/presets already live in.

namespace JPAssist {

struct HistoryEntry {
    uint16_t textId = 0;
    std::string englishText; // English-only, same reason as JPAssistTypes.h's DialoguePage
    int64_t unixTime = 0;
};

// Loads the progress file if present. A missing or malformed file is not an
// error - starts from empty progress instead (design doc's acceptance
// criteria: "A malformed progress file must never prevent the game from
// starting").
void StudyPersistence_Load();

// Writes the current in-memory progress to disk, atomically (temp file then
// rename), mirroring the pattern soh/soh/SaveManager.cpp uses for .sav files.
void StudyPersistence_Save();

bool StudyPersistence_IsSaved(const std::string& tokenId);
void StudyPersistence_ToggleSaved(const std::string& tokenId);

// Known state is sense-specific: homographs may share a lemma/reading while
// carrying different meanings. Marking is intentionally one-way in-game;
// the account site remains the place to correct or undo a knowledge claim.
bool StudyPersistence_IsKnown(const std::string& tokenId, const std::string& senseId);
void StudyPersistence_MarkKnown(const std::string& tokenId, const std::string& senseId);

// Increments the in-memory encounter count and last-encounter timestamp for
// tokenId. Does not write to disk itself - call StudyPersistence_Save()
// separately (e.g. once when Study Mode closes) to avoid a disk write on
// every navigation press.
void StudyPersistence_RecordEncounter(const std::string& tokenId);

int StudyPersistence_GetEncounterCount(const std::string& tokenId);

// Design doc section 9's "message history" field / section 11's "keep a
// short, searchable history of recently seen lines". Appends and trims to
// the most recent kMaxHistoryEntries; does not
// write to disk itself, same batching reasoning as encounter counts.
void StudyPersistence_RecordHistoryEntry(uint16_t textId, const std::string& englishText);
const std::vector<HistoryEntry>& StudyPersistence_GetHistory();

} // namespace JPAssist
