#pragma once

#include <cstdint>

// Read-only lookup into the vanilla message tables.
//
// Message_FindMessage/Message_FindMessageJPN (soh/src/code/z_message_PAL.c)
// and the debug MessageViewer path (soh/soh/Enhancements/debugger/MessageViewer.cpp)
// both write their result into play->msgCtx.font, mutating the live message
// state as a side effect of "finding" a message. That makes them unsafe to
// call just to peek at the other language while a real conversation is in
// progress (see docs/JP_ASSIST_DESIGN.md section 6.3).
//
// This lookup never touches msgCtx or font - it walks the same table
// pointers (sJpnMessageEntryTablePtr / sNesMessageEntryTablePtr) and hands
// back a pointer straight into the loaded archive segment plus its length.

namespace JPAssist {

// Returns true and fills outSegment/outLength if textId exists in the table
// for `language` (a Language enum value from z64.h - LANGUAGE_ENG or
// LANGUAGE_JPN; other languages are not relevant to this mod). Returns false
// (and leaves the out-params untouched) if the table is unavailable or the
// id was not found, mirroring the "missing corpus data fails gracefully"
// requirement in the design doc's acceptance criteria.
bool DialogueRepository_Find(uint16_t textId, uint8_t language, const char** outSegment, uint32_t* outLength);

} // namespace JPAssist
