#pragma once

#include <cstdint>
#include <string>

#include "LearningSyncClient.h"

namespace JPAssist {

struct StudyToken;

void LearningSync_Initialize();
void LearningSync_Configure();
void LearningSync_BeginPairing();
void LearningSync_Disconnect();
LearningSyncStatus LearningSync_GetStatus();

void LearningSync_RecordWordEvent(const std::string& type, const StudyToken& token, uint16_t textId,
                                  int pageIndex, uint32_t count = 1);
void LearningSync_RecordDialogueEvent(const std::string& type, uint16_t textId, int pageIndex);

} // namespace JPAssist
