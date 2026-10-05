#include <gtest/gtest.h>

#include "JPAssistHost.h"

namespace {

class HostPathTest : public testing::Test {
  protected:
    void TearDown() override {
        JPAssist::JPAssistHost_ResetPathResolver();
    }
};

TEST_F(HostPathTest, UsesRelativePathsWithoutAGameHost) {
    EXPECT_EQ(JPAssist::JPAssistHost_LocateDataFile("jp_assist/data.json"), "jp_assist/data.json");
    EXPECT_EQ(JPAssist::JPAssistHost_GetWritableFile("progress.json"), "progress.json");
}

TEST_F(HostPathTest, RoutesDataAndWritableFilesThroughInstalledHost) {
    JPAssist::JPAssistHost_SetPathResolver([](JPAssist::HostPathKind kind, const std::string& relativePath) {
        return std::string(kind == JPAssist::HostPathKind::Data ? "/mod/" : "/save/") + relativePath;
    });

    EXPECT_EQ(JPAssist::JPAssistHost_LocateDataFile("corpus.json"), "/mod/corpus.json");
    EXPECT_EQ(JPAssist::JPAssistHost_GetWritableFile("progress.json"), "/save/progress.json");
}

TEST_F(HostPathTest, ReadsDataThroughInstalledHostWithoutAFilePath) {
    JPAssist::JPAssistHost_SetDataReader([](const std::string& relativePath, std::string& contents) {
        if (relativePath != "jp_assist/runtime_data.json") {
            return false;
        }
        contents = R"({"messages":{}})";
        return true;
    });

    std::string contents;
    EXPECT_TRUE(JPAssist::JPAssistHost_ReadDataFile("jp_assist/runtime_data.json", contents));
    EXPECT_EQ(contents, R"({"messages":{}})");
    EXPECT_FALSE(JPAssist::JPAssistHost_ReadDataFile("missing.json", contents));
}

} // namespace
