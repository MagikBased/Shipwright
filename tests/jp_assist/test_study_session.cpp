#include <gtest/gtest.h>

#include "StudySession.h"

namespace {

using JPAssist::StudyCommand;
using JPAssist::StudySession;

TEST(StudySession, RecallFirstAndDirectEntryChooseDefinitionVisibility) {
    StudySession session;
    session.RetargetDialogue(0x1001, 0, 3, false);

    auto result = session.HandleCommands(StudyCommand::OpenRecallFirst, 0);
    EXPECT_TRUE(result.entered);
    EXPECT_TRUE(result.consumeEntryCommands);
    EXPECT_FALSE(session.IsDefinitionVisible());

    EXPECT_TRUE(session.Exit());
    result = session.HandleCommands(StudyCommand::OpenWithDefinition, 0);
    EXPECT_TRUE(result.entered);
    EXPECT_TRUE(session.IsDefinitionVisible());
}

TEST(StudySession, NavigationIsClampedAndRememberedPerDialoguePage) {
    StudySession session;
    session.RetargetDialogue(0x1001, 0, 3, false);
    session.HandleCommands(StudyCommand::OpenWithDefinition, 0);
    session.HandleCommands(StudyCommand::NextWord, 0);
    session.HandleCommands(StudyCommand::NextWord, 0);
    session.HandleCommands(StudyCommand::NextWord, 0);
    EXPECT_EQ(session.SelectedTokenIndex(), 2);

    session.RetargetDialogue(0x1001, 1, 2, false);
    EXPECT_EQ(session.SelectedTokenIndex(), 0);
    session.RetargetDialogue(0x1001, 0, 3, false);
    EXPECT_EQ(session.SelectedTokenIndex(), 2);
}

TEST(StudySession, ChoiceSelectionIsFrozenUntilExit) {
    StudySession session;
    session.RetargetDialogue(0x103E, 0, 4, true);
    auto result = session.HandleCommands(StudyCommand::OpenWithDefinition, 1);
    ASSERT_TRUE(result.freezeChoice);
    EXPECT_EQ(result.frozenChoiceIndex, 1);

    result = session.HandleCommands(StudyCommand::NextWord, 0);
    EXPECT_TRUE(result.freezeChoice);
    EXPECT_EQ(result.frozenChoiceIndex, 1);
    EXPECT_TRUE(session.IsChoiceFrozen());

    result = session.HandleCommands(StudyCommand::Close, 0);
    EXPECT_TRUE(result.exited);
    EXPECT_FALSE(session.IsChoiceFrozen());
}

TEST(StudySession, ProducesPortableSideEffectRequestsAndCounters) {
    StudySession session;
    session.RetargetDialogue(0x1001, 0, 2, false);
    session.HandleCommands(StudyCommand::OpenWithDefinition, 0);
    const StudyCommand commands = StudyCommand::ToggleDefinition | StudyCommand::NextWord |
                                  StudyCommand::ScrollDown | StudyCommand::ToggleSaved |
                                  StudyCommand::MarkKnown | StudyCommand::PlayAudio;
    const auto result = session.HandleCommands(commands, 0);

    EXPECT_TRUE(result.definitionToggled);
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_EQ(result.scrollPixels, 80.0f);
    EXPECT_TRUE(result.toggleSaved);
    EXPECT_TRUE(result.markKnown);
    EXPECT_TRUE(result.playAudio);
    EXPECT_TRUE(result.consumeStudyCommands);
    EXPECT_EQ(session.Counters().enter, 1);
    EXPECT_EQ(session.Counters().navigation, 1);
    EXPECT_EQ(session.Counters().scroll, 1);
    EXPECT_EQ(session.Counters().saveToggle, 1);
    EXPECT_EQ(session.Counters().definitionToggle, 1);

    session.RecordKnownMarked();
    session.RecordAudioPlayed();
    EXPECT_EQ(session.Counters().knownMark, 1);
    EXPECT_EQ(session.Counters().audioPlay, 1);
}

TEST(StudySession, RefusesEntryWhenCurrentPageHasNoTokens) {
    StudySession session;
    session.RetargetDialogue(0x9999, 0, 0, false);
    const auto result = session.HandleCommands(StudyCommand::OpenWithDefinition, 0);
    EXPECT_FALSE(result.entered);
    EXPECT_FALSE(session.IsActive());
}

} // namespace
