#include <gtest/gtest.h>

#include "StudySelectionMemory.h"

TEST(StudySelectionMemory, RestoresSelectionForTheSameDialoguePage) {
    JPAssist::StudySelectionMemory memory;

    EXPECT_EQ(memory.Restore(0x1034, 0, 12), 0);
    memory.Remember(0x1034, 0, 5, 12);
    EXPECT_EQ(memory.Restore(0x1034, 0, 12), 5);
}

TEST(StudySelectionMemory, KeepsMessagesAndPagesIndependent) {
    JPAssist::StudySelectionMemory memory;

    memory.Remember(0x1034, 0, 3, 12);
    memory.Remember(0x1034, 1, 7, 10);
    memory.Remember(0x1035, 0, 2, 4);

    EXPECT_EQ(memory.Restore(0x1034, 0, 12), 3);
    EXPECT_EQ(memory.Restore(0x1034, 1, 10), 7);
    EXPECT_EQ(memory.Restore(0x1035, 0, 4), 2);
}

TEST(StudySelectionMemory, ClampsRememberedSelectionToCurrentTokenCount) {
    JPAssist::StudySelectionMemory memory;

    memory.Remember(0x1034, 0, 9, 10);
    EXPECT_EQ(memory.Restore(0x1034, 0, 4), 3);
    EXPECT_EQ(memory.Restore(0x1034, 0, 0), 0);
}
