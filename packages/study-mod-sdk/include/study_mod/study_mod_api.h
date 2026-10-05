#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(STUDY_MOD_HOST_EXPORTS)
#define STUDY_MOD_API_EXPORT __declspec(dllexport)
#else
#define STUDY_MOD_API_EXPORT __declspec(dllimport)
#endif
#else
#define STUDY_MOD_API_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define STUDY_MOD_HOST_ABI_VERSION_1 1U

typedef enum StudyModHostCapability {
    STUDY_MOD_CAP_WRITABLE_STORAGE = UINT64_C(1) << 0,
    STUDY_MOD_CAP_RESOURCE_DATA = UINT64_C(1) << 1,
    STUDY_MOD_CAP_DIALOGUE_EVENTS = UINT64_C(1) << 2,
    STUDY_MOD_CAP_SEMANTIC_INPUT = UINT64_C(1) << 3,
    STUDY_MOD_CAP_OVERLAY = UINT64_C(1) << 4,
    STUDY_MOD_CAP_NATIVE_TEXT_HIGHLIGHT = UINT64_C(1) << 5,
    STUDY_MOD_CAP_AUDIO_PLAYBACK = UINT64_C(1) << 6,
    STUDY_MOD_CAP_LIFECYCLE_EVENTS = UINT64_C(1) << 7,
    STUDY_MOD_CAP_SETTINGS = UINT64_C(1) << 8,
} StudyModHostCapability;

/** Copies a private writable path and returns its required size including NUL. */
typedef size_t (*StudyModGetWritablePathFn)(const char* mod_id, const char* relative_path, char* output,
                                            size_t output_size);
/** Returns the byte size of a virtual resource, or zero when it is absent. */
typedef size_t (*StudyModGetResourceSizeFn)(const char* resource_name);
/** Copies a virtual resource and returns its full size, or zero when absent. */
typedef size_t (*StudyModCopyResourceDataFn)(const char* resource_name, void* output, size_t output_size);

typedef enum StudyModDialogueEventType {
    STUDY_MOD_DIALOGUE_OPENED = 1,
    STUDY_MOD_DIALOGUE_PAGE_CHANGED = 2,
    STUDY_MOD_DIALOGUE_UPDATED = 3,
    STUDY_MOD_DIALOGUE_CLOSED = 4,
} StudyModDialogueEventType;

typedef enum StudyModDialogueFlags {
    STUDY_MOD_DIALOGUE_FLAG_PAGE_DECODED = 1U << 0,
    STUDY_MOD_DIALOGUE_FLAG_CHOICE = 1U << 1,
} StudyModDialogueFlags;

typedef enum StudyModLanguage {
    STUDY_MOD_LANGUAGE_UNKNOWN = 0,
    STUDY_MOD_LANGUAGE_ENGLISH = 1,
    STUDY_MOD_LANGUAGE_JAPANESE = 2,
    STUDY_MOD_LANGUAGE_GERMAN = 3,
    STUDY_MOD_LANGUAGE_FRENCH = 4,
} StudyModLanguage;

typedef struct StudyModRect {
    float x;
    float y;
    float width;
    float height;
    float logical_screen_width;
    float logical_screen_height;
} StudyModRect;

/** Snapshot is valid only for the duration of its callback. */
typedef struct StudyModDialogueEvent {
    uint32_t struct_size;
    uint32_t type;
    uint64_t dialogue_id;
    uint32_t page_index;
    int32_t choice_index;
    uint32_t choice_count;
    uint32_t language;
    uint32_t flags;
    StudyModRect textbox_bounds;
} StudyModDialogueEvent;

typedef enum StudyModInputButton {
    STUDY_MOD_INPUT_PRIMARY = UINT64_C(1) << 0,
    STUDY_MOD_INPUT_SECONDARY = UINT64_C(1) << 1,
    STUDY_MOD_INPUT_MENU = UINT64_C(1) << 2,
    STUDY_MOD_INPUT_LEFT_SHOULDER = UINT64_C(1) << 3,
    STUDY_MOD_INPUT_RIGHT_SHOULDER = UINT64_C(1) << 4,
    STUDY_MOD_INPUT_LEFT_TRIGGER = UINT64_C(1) << 5,
    STUDY_MOD_INPUT_FACE_UP = UINT64_C(1) << 6,
    STUDY_MOD_INPUT_FACE_DOWN = UINT64_C(1) << 7,
    STUDY_MOD_INPUT_FACE_LEFT = UINT64_C(1) << 8,
    STUDY_MOD_INPUT_FACE_RIGHT = UINT64_C(1) << 9,
    STUDY_MOD_INPUT_NAV_UP = UINT64_C(1) << 10,
    STUDY_MOD_INPUT_NAV_DOWN = UINT64_C(1) << 11,
    STUDY_MOD_INPUT_NAV_LEFT = UINT64_C(1) << 12,
    STUDY_MOD_INPUT_NAV_RIGHT = UINT64_C(1) << 13,
} StudyModInputButton;

/*
 * Reserved callback-result bits for consuming analog axes. These deliberately
 * live outside the semantic button range so they can be added to ABI v1
 * without changing StudyModInputEvent or invalidating older plugins.
 */
#define STUDY_MOD_INPUT_CONSUME_STICK_X (UINT64_C(1) << 62)
#define STUDY_MOD_INPUT_CONSUME_STICK_Y (UINT64_C(1) << 63)
#define STUDY_MOD_INPUT_CONSUME_AXIS_MASK \
    (STUDY_MOD_INPUT_CONSUME_STICK_X | STUDY_MOD_INPUT_CONSUME_STICK_Y)

typedef enum StudyModInputFlags {
    STUDY_MOD_INPUT_FLAG_DIALOGUE_ACTIVE = 1U << 0,
} StudyModInputFlags;

typedef struct StudyModInputEvent {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t pressed;
    uint64_t held;
    uint64_t released;
    int16_t stick_x;
    int16_t stick_y;
} StudyModInputEvent;

typedef enum StudyModLifecycleEventType {
    STUDY_MOD_LIFECYCLE_SCENE_CHANGED = 1,
    STUDY_MOD_LIFECYCLE_GAME_EXITED = 2,
} StudyModLifecycleEventType;

typedef struct StudyModLifecycleEvent {
    uint32_t struct_size;
    uint32_t type;
    int64_t value;
} StudyModLifecycleEvent;

typedef void (*StudyModDialogueCallback)(const StudyModDialogueEvent* event, void* user_data);
/**
 * Returns pressed StudyModInputButton bits and/or
 * STUDY_MOD_INPUT_CONSUME_STICK_X/Y bits that the host should consume.
 */
typedef uint64_t (*StudyModInputCallback)(const StudyModInputEvent* event, void* user_data);
typedef void (*StudyModLifecycleCallback)(const StudyModLifecycleEvent* event, void* user_data);

typedef enum StudyModNativeHighlightStyle {
    STUDY_MOD_HIGHLIGHT_GLOW = 1U << 0,
} StudyModNativeHighlightStyle;

typedef struct StudyModNativeHighlight {
    uint32_t struct_size;
    uint32_t start;
    uint32_t length;
    uint32_t style;
    uint32_t rgba;
} StudyModNativeHighlight;

/** Returns nonzero when the provider supplied a highlight for this dialogue. */
typedef int32_t (*StudyModNativeHighlightCallback)(uint64_t dialogue_id, StudyModNativeHighlight* highlight,
                                                   void* user_data);

typedef enum StudyModAudioFormat {
    STUDY_MOD_AUDIO_PCM_S16 = 1,
} StudyModAudioFormat;

/**
 * A short sound copied synchronously by the host. ABI v1 accepts 32 kHz,
 * interleaved stereo signed-16 PCM and limits clips to 30 seconds.
 */
typedef struct StudyModAudioClip {
    uint32_t struct_size;
    uint32_t format;
    const int16_t* samples;
    size_t frame_count;
    uint32_t sample_rate;
    uint32_t channels;
} StudyModAudioClip;

typedef uint64_t (*StudyModRegisterDialogueCallbackFn)(const char* mod_id, StudyModDialogueCallback callback,
                                                       void* user_data);
typedef uint64_t (*StudyModRegisterInputCallbackFn)(const char* mod_id, StudyModInputCallback callback,
                                                    void* user_data);
typedef uint64_t (*StudyModRegisterLifecycleCallbackFn)(const char* mod_id, StudyModLifecycleCallback callback,
                                                        void* user_data);
typedef uint64_t (*StudyModRegisterNativeHighlightCallbackFn)(const char* mod_id,
                                                              StudyModNativeHighlightCallback callback,
                                                              void* user_data);
typedef int32_t (*StudyModUnregisterCallbackFn)(uint64_t subscription_id);
typedef uint32_t (*StudyModUnregisterModCallbacksFn)(const char* mod_id);
/** Replaces any currently playing study-mod clip. Returns nonzero on success. */
typedef int32_t (*StudyModPlayAudioFn)(const char* mod_id, const StudyModAudioClip* clip);
/** Stops the clip only when it is owned by mod_id. */
typedef int32_t (*StudyModStopAudioFn)(const char* mod_id);
typedef int32_t (*StudyModGetIntSettingFn)(const char* mod_id, const char* key, int32_t default_value);
typedef int32_t (*StudyModSetIntSettingFn)(const char* mod_id, const char* key, int32_t value);
typedef float (*StudyModGetFloatSettingFn)(const char* mod_id, const char* key, float default_value);
typedef int32_t (*StudyModSetFloatSettingFn)(const char* mod_id, const char* key, float value);
/** Copies a UTF-8 setting and returns its required size including NUL. */
typedef size_t (*StudyModGetStringSettingFn)(const char* mod_id, const char* key, const char* default_value,
                                             char* output, size_t output_size);
typedef int32_t (*StudyModSetStringSettingFn)(const char* mod_id, const char* key, const char* value);
/** Requests durable persistence after one or more namespaced setting writes. */
typedef int32_t (*StudyModFlushSettingsFn)(const char* mod_id);

typedef enum StudyModOverlayMouseButton {
    STUDY_MOD_MOUSE_LEFT = 1U << 0,
    STUDY_MOD_MOUSE_RIGHT = 1U << 1,
    STUDY_MOD_MOUSE_MIDDLE = 1U << 2,
} StudyModOverlayMouseButton;

typedef enum StudyModOverlayModifier {
    STUDY_MOD_MODIFIER_SHIFT = 1U << 0,
    STUDY_MOD_MODIFIER_CONTROL = 1U << 1,
    STUDY_MOD_MODIFIER_ALT = 1U << 2,
} StudyModOverlayModifier;

typedef enum StudyModTextFlags {
    STUDY_MOD_TEXT_JAPANESE = 1U << 0,
} StudyModTextFlags;

typedef struct StudyModVec2 {
    float x;
    float y;
} StudyModVec2;

typedef struct StudyModOverlayFrame {
    uint32_t struct_size;
    StudyModRect viewport;
    float delta_time;
    float mouse_x;
    float mouse_y;
    float mouse_wheel;
    uint32_t mouse_pressed;
    uint32_t mouse_held;
    uint32_t modifiers;
} StudyModOverlayFrame;

/** Host drawing functions are valid only during the overlay callback. */
typedef struct StudyModOverlayDrawApi {
    uint32_t struct_size;
    void* context;
    void (*rect_filled)(void* context, StudyModRect rect, uint32_t rgba, float rounding);
    void (*rect)(void* context, StudyModRect rect, uint32_t rgba, float rounding, float thickness);
    void (*line)(void* context, StudyModVec2 from, StudyModVec2 to, uint32_t rgba, float thickness);
    void (*text)(void* context, StudyModVec2 position, float size, uint32_t rgba, const char* utf8,
                 float wrap_width, uint32_t flags);
    StudyModVec2 (*measure_text)(void* context, float size, const char* utf8, float wrap_width, uint32_t flags);
    /** Optional ABI-v1 tail: scopes subsequent primitives to a rectangle. */
    void (*push_clip_rect)(void* context, StudyModRect rect, int32_t intersect_with_current);
    void (*pop_clip_rect)(void* context);
    /** Draws the host's native glyph for one StudyModInputButton value. */
    int32_t (*button_glyph)(void* context, uint64_t button, StudyModRect bounds, uint32_t rgba);
} StudyModOverlayDrawApi;

typedef void (*StudyModOverlayCallback)(const StudyModOverlayFrame* frame, const StudyModOverlayDrawApi* draw,
                                        void* user_data);
typedef uint64_t (*StudyModRegisterOverlayCallbackFn)(const char* mod_id, StudyModOverlayCallback callback,
                                                      void* user_data);

/** ABI v1 table. Append optional fields; callers check struct_size and capabilities. */
typedef struct StudyModHostApi {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t capabilities;
    const char* host_id;
    const char* game_id;
    StudyModGetWritablePathFn get_writable_path;
    StudyModGetResourceSizeFn get_resource_size;
    StudyModCopyResourceDataFn copy_resource_data;
    StudyModRegisterDialogueCallbackFn register_dialogue_callback;
    StudyModRegisterInputCallbackFn register_input_callback;
    StudyModRegisterLifecycleCallbackFn register_lifecycle_callback;
    StudyModUnregisterCallbackFn unregister_callback;
    StudyModUnregisterModCallbacksFn unregister_mod_callbacks;
    StudyModRegisterNativeHighlightCallbackFn register_native_highlight_callback;
    StudyModPlayAudioFn play_audio;
    StudyModStopAudioFn stop_audio;
    StudyModGetIntSettingFn get_int_setting;
    StudyModSetIntSettingFn set_int_setting;
    StudyModGetFloatSettingFn get_float_setting;
    StudyModSetFloatSettingFn set_float_setting;
    StudyModGetStringSettingFn get_string_setting;
    StudyModSetStringSettingFn set_string_setting;
    StudyModRegisterOverlayCallbackFn register_overlay_callback;
    StudyModFlushSettingsFn flush_settings;
} StudyModHostApi;

/** Returns the newest API in the inclusive version range, or NULL. */
STUDY_MOD_API_EXPORT const StudyModHostApi* StudyMod_GetHostApi(uint32_t minimum_version, uint32_t maximum_version);

#ifdef __cplusplus
}
#endif
