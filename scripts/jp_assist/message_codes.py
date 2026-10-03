"""Message control-code constants, ported from soh/include/message_data_fmt.h.

This is a Python port of the same constants soh/soh/Enhancements/JPAssist/
MessageParser.cpp uses at runtime, kept deliberately in sync with that file
rather than trying to share a single source of truth across C++ and Python.
See docs/JP_ASSIST_DESIGN.md section 6.4.1 for why MESSAGE_TEXTID and
MESSAGE_EVENT are treated as terminators rather than page breaks: bytes
after either belong to a *different* message's data, not more of this one.
"""

from dataclasses import dataclass

# English control codes (soh/include/message_data_fmt.h, single-byte stream)
MESSAGE_NEWLINE = 0x01
MESSAGE_END = 0x02
MESSAGE_BOX_BREAK = 0x04
MESSAGE_COLOR = 0x05
MESSAGE_SHIFT = 0x06
MESSAGE_TEXTID = 0x07
MESSAGE_QUICKTEXT_ENABLE = 0x08
MESSAGE_QUICKTEXT_DISABLE = 0x09
MESSAGE_PERSISTENT = 0x0A
MESSAGE_EVENT = 0x0B
MESSAGE_BOX_BREAK_DELAYED = 0x0C
MESSAGE_AWAIT_BUTTON_PRESS = 0x0D
MESSAGE_FADE = 0x0E
MESSAGE_NAME = 0x0F
MESSAGE_OCARINA = 0x10
MESSAGE_FADE2 = 0x11
MESSAGE_SFX = 0x12
MESSAGE_ITEM_ICON = 0x13
MESSAGE_TEXT_SPEED = 0x14
MESSAGE_BACKGROUND = 0x15
MESSAGE_MARATHON_TIME = 0x16
MESSAGE_RACE_TIME = 0x17
MESSAGE_POINTS = 0x18
MESSAGE_TOKENS = 0x19
MESSAGE_UNSKIPPABLE = 0x1A
MESSAGE_TWO_CHOICE = 0x1B
MESSAGE_THREE_CHOICE = 0x1C
MESSAGE_FISH_INFO = 0x1D
MESSAGE_HIGHSCORE = 0x1E
MESSAGE_TIME = 0x1F

# Japanese control codes (u16 stream) - these are literal Shift-JIS code
# points that happen to fall in unused ranges, chosen so they never collide
# with a real kana/kanji glyph code.
MESSAGE_NEWLINE_JPN = 0x000A
MESSAGE_END_JPN = 0x8170
MESSAGE_BOX_BREAK_JPN = 0x81A5
MESSAGE_COLOR_JPN = 0x000B
MESSAGE_SHIFT_JPN = 0x86C7
MESSAGE_TEXTID_JPN = 0x81CB
MESSAGE_QUICKTEXT_ENABLE_JPN = 0x8189
MESSAGE_QUICKTEXT_DISABLE_JPN = 0x818A
MESSAGE_PERSISTENT_JPN = 0x86C8
MESSAGE_EVENT_JPN = 0x819F
MESSAGE_BOX_BREAK_DELAYED_JPN = 0x81A3
MESSAGE_AWAIT_BUTTON_PRESS_JPN = 0x81A4
MESSAGE_FADE_JPN = 0x819E
MESSAGE_NAME_JPN = 0x874F
MESSAGE_OCARINA_JPN = 0x81F0
MESSAGE_FADE2_JPN = 0x81F4
MESSAGE_SFX_JPN = 0x81F3
MESSAGE_ITEM_ICON_JPN = 0x819A
MESSAGE_TEXT_SPEED_JPN = 0x86C9
MESSAGE_BACKGROUND_JPN = 0x86B3
MESSAGE_MARATHON_TIME_JPN = 0x8791
MESSAGE_RACE_TIME_JPN = 0x8792
MESSAGE_POINTS_JPN = 0x879B
MESSAGE_TOKENS_JPN = 0x86A3
MESSAGE_UNSKIPPABLE_JPN = 0x8199
MESSAGE_TWO_CHOICE_JPN = 0x81BC
MESSAGE_THREE_CHOICE_JPN = 0x81B8
MESSAGE_FISH_INFO_JPN = 0x86A4
MESSAGE_HIGHSCORE_JPN = 0x869F
MESSAGE_TIME_JPN = 0x81A1

JAPANESE_CONTROL_CODES = {
    MESSAGE_NEWLINE_JPN, MESSAGE_END_JPN, MESSAGE_BOX_BREAK_JPN, MESSAGE_COLOR_JPN,
    MESSAGE_SHIFT_JPN, MESSAGE_TEXTID_JPN, MESSAGE_QUICKTEXT_ENABLE_JPN,
    MESSAGE_QUICKTEXT_DISABLE_JPN, MESSAGE_PERSISTENT_JPN, MESSAGE_EVENT_JPN,
    MESSAGE_BOX_BREAK_DELAYED_JPN, MESSAGE_AWAIT_BUTTON_PRESS_JPN, MESSAGE_FADE_JPN,
    MESSAGE_NAME_JPN, MESSAGE_OCARINA_JPN, MESSAGE_FADE2_JPN, MESSAGE_SFX_JPN,
    MESSAGE_ITEM_ICON_JPN, MESSAGE_TEXT_SPEED_JPN, MESSAGE_BACKGROUND_JPN,
    MESSAGE_MARATHON_TIME_JPN, MESSAGE_RACE_TIME_JPN, MESSAGE_POINTS_JPN,
    MESSAGE_TOKENS_JPN, MESSAGE_UNSKIPPABLE_JPN, MESSAGE_TWO_CHOICE_JPN,
    MESSAGE_THREE_CHOICE_JPN, MESSAGE_FISH_INFO_JPN, MESSAGE_HIGHSCORE_JPN,
    MESSAGE_TIME_JPN,
}

# Operand length (units after the control code, not counting the code
# itself) for English (byte units) and Japanese (u16 units).
_ENG_OPERAND_LENGTHS = {
    MESSAGE_BOX_BREAK_DELAYED: 1,
    MESSAGE_TEXTID: 2,
    MESSAGE_COLOR: 1,
    MESSAGE_SHIFT: 1,
    MESSAGE_FADE: 1,
    MESSAGE_ITEM_ICON: 1,
    MESSAGE_TEXT_SPEED: 1,
    MESSAGE_HIGHSCORE: 1,
    MESSAGE_FADE2: 2,
    MESSAGE_SFX: 2,
    MESSAGE_BACKGROUND: 3,
}

_JPN_OPERAND_LENGTHS = {
    MESSAGE_BOX_BREAK_DELAYED_JPN: 1,
    MESSAGE_TEXTID_JPN: 2,
    MESSAGE_COLOR_JPN: 1,
    MESSAGE_SHIFT_JPN: 1,
    MESSAGE_FADE_JPN: 1,
    MESSAGE_ITEM_ICON_JPN: 1,
    MESSAGE_TEXT_SPEED_JPN: 1,
    MESSAGE_HIGHSCORE_JPN: 1,
    MESSAGE_FADE2_JPN: 2,
    # Japanese messages store the complete sound ID in one u16 operand.
    # Treating it like the English two-byte stream skips the following
    # control code and leaks that control's operand into normalized text.
    MESSAGE_SFX_JPN: 1,
    MESSAGE_BACKGROUND_JPN: 3,
}

# Codes that end the CURRENT page but leave more of THIS message's own
# buffer to parse (a real box break).
_ENG_PAGE_BREAK_CODES = {MESSAGE_BOX_BREAK, MESSAGE_BOX_BREAK_DELAYED}
_JPN_PAGE_BREAK_CODES = {MESSAGE_BOX_BREAK_JPN, MESSAGE_BOX_BREAK_DELAYED_JPN}

# Codes that end parsing of this message's buffer entirely: MESSAGE_END is
# the real end; MESSAGE_TEXTID jumps to a different message's table entry
# (its operand bytes are the target id, not more of this segment); EVENT
# hands off to a cutscene script. Bytes after any of these three don't
# belong to this message - see MessageParser.cpp's identical fix, made
# after live-testing found a bogus extra page appearing from exactly this
# case.
_ENG_TERMINATOR_CODES = {MESSAGE_END, MESSAGE_TEXTID, MESSAGE_EVENT}
_JPN_TERMINATOR_CODES = {MESSAGE_END_JPN, MESSAGE_TEXTID_JPN, MESSAGE_EVENT_JPN}

# The original Japanese font repurposes these otherwise-unused Shift-JIS
# values as controller glyphs (documented by textures/kanji.xml). Decoding
# them as ordinary Shift-JIS produces misleading Greek letters.
_JPN_CUSTOM_GLYPHS = {
    0x839F: "[A]",
    0x83A0: "[B]",
    0x83A1: "[C]",
    0x83A2: "[L]",
    0x83A3: "[R]",
    0x83A4: "[Z]",
    0x83A5: "[C-Up]",
    0x83A6: "[C-Down]",
    0x83A7: "[C-Left]",
    0x83A8: "[C-Right]",
    0x83A9: "[Z-target]",
    0x83AA: "[Control Stick]",
}

# The English table uses the low byte of the same custom font glyphs.
_ENG_CUSTOM_GLYPHS = {code & 0xFF: label for code, label in _JPN_CUSTOM_GLYPHS.items()}


@dataclass
class Page:
    text: str
    is_choice: bool = False
    choice_count: int = 0


def parse_english(data: bytes) -> list[Page]:
    """Split raw English message bytes into pages, decoding printable text."""
    pages: list[Page] = []
    page = Page(text="")
    pos = 0
    length = len(data)

    while pos < length:
        code = data[pos]

        if code in (MESSAGE_TWO_CHOICE, MESSAGE_THREE_CHOICE):
            page.is_choice = True
            page.choice_count = 2 if code == MESSAGE_TWO_CHOICE else 3
            pos += 1
            continue

        if code >= 0x20:
            if code < 0x80:
                page.text += chr(code)
            else:
                # Never leak a raw game-font byte into a UTF-8 string.
                page.text += _ENG_CUSTOM_GLYPHS.get(code, "?")
            pos += 1
            continue

        if code == MESSAGE_NEWLINE:
            page.text += "\n"

        operand_length = _ENG_OPERAND_LENGTHS.get(code, 0)
        pos += 1 + operand_length

        if code in _ENG_PAGE_BREAK_CODES:
            pages.append(page)
            page = Page(text="")
        elif code in _ENG_TERMINATOR_CODES:
            pages.append(page)
            break

    if not pages:
        pages.append(page)
    return pages


def parse_japanese(data: bytes) -> list[Page]:
    """Split raw Japanese message bytes (little-endian u16 stream - the
    archive's OTR header declares ByteOrder=0, and this was confirmed
    against real message data: decoding bytes 83 7c 83 50 83 62 83 67 as
    little-endian u16s [0x837C, 0x8350, 0x8362, 0x8367] and then re-pairing
    each as (high=unit>>8, low=unit&0xFF) for Shift-JIS gives "ポケット",
    a real word) into pages, decoding non-control-code units as literal
    Shift-JIS characters."""
    units = [data[i] | (data[i + 1] << 8) for i in range(0, len(data) - 1, 2)]
    pages: list[Page] = []
    page = Page(text="")
    pos = 0
    length = len(units)

    while pos < length:
        value = units[pos]

        if value in _JPN_CUSTOM_GLYPHS:
            page.text += _JPN_CUSTOM_GLYPHS[value]
            pos += 1
            continue

        if value in (MESSAGE_TWO_CHOICE_JPN, MESSAGE_THREE_CHOICE_JPN):
            page.is_choice = True
            page.choice_count = 2 if value == MESSAGE_TWO_CHOICE_JPN else 3
            pos += 1
            continue

        if value not in JAPANESE_CONTROL_CODES:
            page.text += bytes([value >> 8, value & 0xFF]).decode("shift_jis", errors="replace")
            pos += 1
            continue

        if value == MESSAGE_NEWLINE_JPN:
            page.text += "\n"

        operand_length = _JPN_OPERAND_LENGTHS.get(value, 0)
        pos += 1 + operand_length

        if value in _JPN_PAGE_BREAK_CODES:
            pages.append(page)
            page = Page(text="")
        elif value in _JPN_TERMINATOR_CODES:
            pages.append(page)
            break

    if not pages:
        pages.append(page)
    return pages
