#include <gtest/gtest.h>

#include "MessageParser.h"
#include "message_data_fmt.h"
#include "z64.h"

TEST(MessageParser, SplitsEnglishPages) {
    const char message[] = { 'O', 'n', 'e', MESSAGE_BOX_BREAK, 'T', 'w', 'o', MESSAGE_END };
    const auto result = JPAssist::MessageParser_Parse(message, sizeof(message), LANGUAGE_ENG);
    ASSERT_TRUE(result.found);
    ASSERT_EQ(result.pages.size(), 2);
    EXPECT_EQ(result.pages[0].englishText, "One");
    EXPECT_EQ(result.pages[1].englishText, "Two");
}

TEST(MessageParser, DetectsEnglishChoices) {
    const char message[] = { MESSAGE_TWO_CHOICE, 'Y', MESSAGE_NEWLINE, 'N', MESSAGE_END };
    const auto result = JPAssist::MessageParser_Parse(message, sizeof(message), LANGUAGE_ENG);
    ASSERT_EQ(result.pages.size(), 1);
    EXPECT_TRUE(result.pages[0].isChoice);
    EXPECT_EQ(result.pages[0].choiceCount, 2);
}

TEST(MessageParser, StopsAtTextId) {
    const char message[] = { 'A', MESSAGE_TEXTID, 0x12, 0x34, 'B', MESSAGE_END };
    const auto result = JPAssist::MessageParser_Parse(message, sizeof(message), LANGUAGE_ENG);
    ASSERT_EQ(result.pages.size(), 1);
    EXPECT_EQ(result.pages[0].englishText, "A");
}

TEST(MessageParser, SplitsJapanesePagesAndDetectsChoice) {
    const uint16_t message[] = { 0x8441, MESSAGE_BOX_BREAK_JPN, MESSAGE_TWO_CHOICE_JPN, 0x8442, MESSAGE_END_JPN };
    const auto result = JPAssist::MessageParser_Parse(reinterpret_cast<const char*>(message), sizeof(message),
                                                       LANGUAGE_JPN);
    ASSERT_EQ(result.pages.size(), 2);
    EXPECT_FALSE(result.pages[0].isChoice);
    EXPECT_TRUE(result.pages[1].isChoice);
    EXPECT_EQ(result.pages[1].choiceCount, 2);
}
