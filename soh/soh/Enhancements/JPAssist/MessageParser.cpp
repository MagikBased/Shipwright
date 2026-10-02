#include "MessageParser.h"

#include "message_data_fmt.h"
#include "z64.h"

namespace JPAssist {

namespace {

// Number of extra bytes/units consumed after a control code, not counting
// the control code itself. -1 marks a page-boundary code (handled directly
// in the walk loop instead of this table). Values taken from
// soh/include/message_data_fmt.h.
int EnglishControlCodeOperandLength(uint8_t code, bool* isPageBoundary, bool* isEnd) {
    *isPageBoundary = false;
    *isEnd = false;
    switch (code) {
        case MESSAGE_BOX_BREAK:
            *isPageBoundary = true;
            return 0;
        case MESSAGE_BOX_BREAK_DELAYED:
            *isPageBoundary = true;
            return 1;
        // TEXTID jumps to a *different* message's table entry - its 2
        // operand bytes are that target id, not more of this segment. EVENT
        // hands off to a cutscene script instead of continuing the textbox.
        // Neither means "more pages follow in this buffer": whatever bytes
        // come next here belong to unrelated data (found live - a page
        // this produced from bytes after a TEXTID code turned out to be
        // real English text ("Sheesh!"), just from a different message
        // that happened to sit next in the archive, which the real game
        // never actually displayed as part of this one). Treat them like
        // END: stop parsing this segment.
        case MESSAGE_TEXTID:
        case MESSAGE_EVENT:
        case MESSAGE_END:
            *isPageBoundary = true;
            *isEnd = true;
            return (code == MESSAGE_TEXTID) ? 2 : 0;
        case MESSAGE_COLOR:
        case MESSAGE_SHIFT:
        case MESSAGE_FADE:
        case MESSAGE_ITEM_ICON:
        case MESSAGE_TEXT_SPEED:
        case MESSAGE_HIGHSCORE:
            return 1;
        case MESSAGE_FADE2:
        case MESSAGE_SFX:
            return 2;
        case MESSAGE_BACKGROUND:
            return 3;
        default:
            // NEWLINE, QUICKTEXT_ENABLE/DISABLE, PERSISTENT,
            // AWAIT_BUTTON_PRESS, NAME, OCARINA, MARATHON_TIME, RACE_TIME,
            // POINTS, TOKENS, UNSKIPPABLE, TWO_CHOICE, THREE_CHOICE,
            // FISH_INFO, TIME all take no operand bytes.
            return 0;
    }
}

int JapaneseControlCodeOperandLength(uint16_t code, bool* isPageBoundary, bool* isEnd) {
    *isPageBoundary = false;
    *isEnd = false;
    switch (code) {
        case MESSAGE_BOX_BREAK_JPN:
            *isPageBoundary = true;
            return 0;
        case MESSAGE_BOX_BREAK_DELAYED_JPN:
            *isPageBoundary = true;
            return 1;
        // See the English EnglishControlCodeOperandLength for why TEXTID and
        // EVENT terminate parsing here instead of continuing into the same
        // segment's next bytes.
        case MESSAGE_TEXTID_JPN:
        case MESSAGE_EVENT_JPN:
        case MESSAGE_END_JPN:
            *isPageBoundary = true;
            *isEnd = true;
            return (code == MESSAGE_TEXTID_JPN) ? 2 : 0;
        case MESSAGE_COLOR_JPN:
        case MESSAGE_SHIFT_JPN:
        case MESSAGE_FADE_JPN:
        case MESSAGE_ITEM_ICON_JPN:
        case MESSAGE_TEXT_SPEED_JPN:
        case MESSAGE_HIGHSCORE_JPN:
            return 1;
        case MESSAGE_FADE2_JPN:
        case MESSAGE_SFX_JPN:
            return 2;
        case MESSAGE_BACKGROUND_JPN:
            return 3;
        default:
            return 0;
    }
}

bool IsJapaneseControlCode(uint16_t value) {
    // MESSAGE_NEWLINE_JPN (0x000A) and MESSAGE_COLOR_JPN (0x000B) are below
    // any real glyph code, so a low-value check alone would catch them, but
    // check exact values throughout to mirror how Message_DecodeJPN
    // disambiguates control codes from kanji/kana glyph codes that happen to
    // share the same 0x81xx/0x86xx/0x87xx range.
    switch (value) {
        case MESSAGE_NEWLINE_JPN:
        case MESSAGE_END_JPN:
        case MESSAGE_BOX_BREAK_JPN:
        case MESSAGE_COLOR_JPN:
        case MESSAGE_SHIFT_JPN:
        case MESSAGE_TEXTID_JPN:
        case MESSAGE_QUICKTEXT_ENABLE_JPN:
        case MESSAGE_QUICKTEXT_DISABLE_JPN:
        case MESSAGE_PERSISTENT_JPN:
        case MESSAGE_EVENT_JPN:
        case MESSAGE_BOX_BREAK_DELAYED_JPN:
        case MESSAGE_AWAIT_BUTTON_PRESS_JPN:
        case MESSAGE_FADE_JPN:
        case MESSAGE_NAME_JPN:
        case MESSAGE_OCARINA_JPN:
        case MESSAGE_FADE2_JPN:
        case MESSAGE_SFX_JPN:
        case MESSAGE_ITEM_ICON_JPN:
        case MESSAGE_TEXT_SPEED_JPN:
        case MESSAGE_BACKGROUND_JPN:
        case MESSAGE_MARATHON_TIME_JPN:
        case MESSAGE_RACE_TIME_JPN:
        case MESSAGE_POINTS_JPN:
        case MESSAGE_TOKENS_JPN:
        case MESSAGE_UNSKIPPABLE_JPN:
        case MESSAGE_TWO_CHOICE_JPN:
        case MESSAGE_THREE_CHOICE_JPN:
        case MESSAGE_FISH_INFO_JPN:
        case MESSAGE_HIGHSCORE_JPN:
        case MESSAGE_TIME_JPN:
            return true;
        default:
            return false;
    }
}

const char* EnglishCustomGlyph(uint8_t value) {
    // The English message font uses the low bytes of the Japanese custom
    // glyph range for controller icons. These are not ISO-8859-1/UTF-8 text;
    // copying them verbatim produces invalid strings (and previously made
    // nlohmann::json terminate the game while saving dialogue history).
    switch (value) {
        case 0x9F:
            return "[A]";
        case 0xA0:
            return "[B]";
        case 0xA1:
            return "[C]";
        case 0xA2:
            return "[L]";
        case 0xA3:
            return "[R]";
        case 0xA4:
            return "[Z]";
        case 0xA5:
            return "[C-Up]";
        case 0xA6:
            return "[C-Down]";
        case 0xA7:
            return "[C-Left]";
        case 0xA8:
            return "[C-Right]";
        case 0xA9:
            return "[Z-target]";
        case 0xAA:
            return "[Control Stick]";
        default:
            return nullptr;
    }
}

DialogueStructure ParseEnglish(const char* segment, uint32_t length) {
    DialogueStructure result;
    result.found = true;

    DialoguePage page;
    page.unitOffset = 0;

    for (uint32_t pos = 0; pos < length;) {
        uint8_t code = static_cast<uint8_t>(segment[pos]);

        if (code == MESSAGE_TWO_CHOICE || code == MESSAGE_THREE_CHOICE) {
            page.isChoice = true;
            page.choiceCount = (code == MESSAGE_TWO_CHOICE) ? 2 : 3;
            pos += 1;
            continue;
        }

        if (code >= 0x20 && code < 0x80) {
            // Printable ASCII glyph.
            page.englishText.push_back(static_cast<char>(code));
            pos += 1;
            continue;
        }

        if (code >= 0x80) {
            const char* glyph = EnglishCustomGlyph(code);
            page.englishText += glyph != nullptr ? glyph : "?";
            pos += 1;
            continue;
        }

        bool isPageBoundary = false;
        bool isEnd = false;
        int operandLength = EnglishControlCodeOperandLength(code, &isPageBoundary, &isEnd);
        pos += 1 + operandLength;

        if (code == MESSAGE_NEWLINE) {
            page.englishText.push_back('\n');
        }

        if (isPageBoundary) {
            result.pages.push_back(page);
            if (isEnd || pos >= length) {
                break;
            }
            page = DialoguePage();
            page.unitOffset = pos;
        }
    }

    if (result.pages.empty() && !page.englishText.empty()) {
        // Segment ended without an explicit END code (shouldn't normally
        // happen, but don't drop a partially-decoded page).
        result.pages.push_back(page);
    }

    return result;
}

DialogueStructure ParseJapanese(const char* segment, uint32_t length) {
    DialogueStructure result;
    result.found = true;

    const uint16_t* units = reinterpret_cast<const uint16_t*>(segment);
    uint32_t unitCount = length / sizeof(uint16_t);

    DialoguePage page;
    page.unitOffset = 0;

    for (uint32_t pos = 0; pos < unitCount;) {
        uint16_t value = units[pos];

        if (value == MESSAGE_TWO_CHOICE_JPN || value == MESSAGE_THREE_CHOICE_JPN) {
            page.isChoice = true;
            page.choiceCount = (value == MESSAGE_TWO_CHOICE_JPN) ? 2 : 3;
            pos += 1;
            continue;
        }

        if (!IsJapaneseControlCode(value)) {
            // Kanji/kana glyph code - not decoded to text in this spike.
            pos += 1;
            continue;
        }

        bool isPageBoundary = false;
        bool isEnd = false;
        int operandLength = JapaneseControlCodeOperandLength(value, &isPageBoundary, &isEnd);
        pos += 1 + operandLength;

        if (isPageBoundary) {
            result.pages.push_back(page);
            if (isEnd || pos >= unitCount) {
                break;
            }
            page = DialoguePage();
            page.unitOffset = pos;
        }
    }

    if (result.pages.empty()) {
        result.pages.push_back(page);
    }

    return result;
}

} // namespace

DialogueStructure MessageParser_Parse(const char* segment, uint32_t length, uint8_t language) {
    if (segment == nullptr || length == 0) {
        return DialogueStructure{};
    }

    if (language == LANGUAGE_JPN) {
        return ParseJapanese(segment, length);
    }
    return ParseEnglish(segment, length);
}

} // namespace JPAssist
