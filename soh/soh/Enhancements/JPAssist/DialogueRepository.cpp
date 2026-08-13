#include "DialogueRepository.h"

#include "message_data_static.h"
#include "z64.h"

// Defined in soh/src/code/z_message_PAL.c. Same globals the vanilla lookup
// and MessageViewer use - we only ever read them.
extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;

namespace JPAssist {

bool DialogueRepository_Find(uint16_t textId, uint8_t language, const char** outSegment, uint32_t* outLength) {
    MessageTableEntry* table = (language == LANGUAGE_JPN) ? sJpnMessageEntryTablePtr : sNesMessageEntryTablePtr;

    if (table == nullptr) {
        return false;
    }

    for (MessageTableEntry* entry = table; entry->textId != 0xFFFF; entry++) {
        if (entry->textId == textId) {
            *outSegment = entry->segment;
            *outLength = entry->msgSize;
            return true;
        }
    }

    return false;
}

} // namespace JPAssist
