#pragma once

#include <cstdint>

#include "JPAssistTypes.h"

// Read-only control-code walker. Given the raw segment bytes returned by
// DialogueRepository, this splits a message into pages and flags choice
// pages, without touching any live game state (play->msgCtx, font, or
// gSaveContext) - unlike Message_Decode/Message_DecodeJPN, which are the
// game's real decoders but mutate msgCtx fields, reset text timers, and push
// texture-cache-invalidation graphics commands as side effects of decoding
// (soh/src/code/z_message_PAL.c, Message_Decode ~line 2241). That makes them
// unsafe to call mid-conversation just to inspect the other language - see
// the Milestone 1 spike notes in docs/JP_ASSIST_DESIGN.md section 6.4.

namespace JPAssist {

DialogueStructure MessageParser_Parse(const char* segment, uint32_t length, uint8_t language);

} // namespace JPAssist
