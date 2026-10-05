#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Internal renderer bridge; plugins register providers through StudyModHostApi. */
int32_t StudyModHost_QueryNativeHighlight(uint64_t dialogue_id, uint32_t* start, uint32_t* length);

#ifdef __cplusplus
}
#endif
