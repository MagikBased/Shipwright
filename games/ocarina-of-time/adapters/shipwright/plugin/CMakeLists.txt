set(JPASSIST_PLUGIN_STAGE "${CMAKE_CURRENT_BINARY_DIR}/stage")
set(JPASSIST_PLUGIN_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/jp-assist.o2r")

find_package(nlohmann_json CONFIG REQUIRED)
include(FetchContent)
FetchContent_GetProperties(dr_libs)

if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(JPASSIST_PLUGIN_PLATFORM "windows_arm64")
    else()
        set(JPASSIST_PLUGIN_PLATFORM "windows_x64")
    endif()
elseif(APPLE)
    set(JPASSIST_PLUGIN_PLATFORM "darwin")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(JPASSIST_PLUGIN_PLATFORM "linux_arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(JPASSIST_PLUGIN_PLATFORM "linux_x64")
else()
    set(JPASSIST_PLUGIN_PLATFORM "linux_x86")
endif()
configure_file(manifest.json.in "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" @ONLY)

add_library(jpassist_plugin_binary MODULE
    dist/plugin_main.cpp
    src/PluginAudio.cpp
    src/PluginCorpus.cpp
    src/PluginLayout.cpp
    src/PluginProgress.cpp
    src/PluginSync.cpp
    src/PluginRuntime.cpp
)
target_include_directories(jpassist_plugin_binary PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${dr_libs_SOURCE_DIR}"
)
target_compile_features(jpassist_plugin_binary PRIVATE cxx_std_20)
target_link_libraries(jpassist_plugin_binary PRIVATE
    ${CMAKE_DL_LIBS}
    jp_assist_study_core
    nlohmann_json::nlohmann_json
    study_mod_sdk
)
set_target_properties(jpassist_plugin_binary PROPERTIES
    PREFIX ""
    OUTPUT_NAME "jp-assist"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/binary"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

set(JPASSIST_LOCAL_CORPUS "${PROJECT_SOURCE_DIR}/scripts/jp_assist/out/runtime_data.json")
if(EXISTS "${JPASSIST_LOCAL_CORPUS}")
    set(JPASSIST_CORPUS_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory "${JPASSIST_PLUGIN_STAGE}/jp_assist"
        COMMAND ${CMAKE_COMMAND} -E copy "${JPASSIST_LOCAL_CORPUS}"
                "${JPASSIST_PLUGIN_STAGE}/jp_assist/runtime_data.json"
    )
    set(JPASSIST_CORPUS_ARCHIVE_ENTRY jp_assist/runtime_data.json)
    set(JPASSIST_CORPUS_DEPENDENCY "${JPASSIST_LOCAL_CORPUS}")
else()
    set(JPASSIST_CORPUS_COMMANDS)
    set(JPASSIST_CORPUS_ARCHIVE_ENTRY)
    set(JPASSIST_CORPUS_DEPENDENCY)
endif()

set(JPASSIST_LOCAL_AUDIO_ROOT "${PROJECT_SOURCE_DIR}/scripts/jp_assist/out/local_bundle/jp_assist")
if(EXISTS "${JPASSIST_LOCAL_AUDIO_ROOT}/audio_manifest.json" AND
   IS_DIRECTORY "${JPASSIST_LOCAL_AUDIO_ROOT}/audio")
    set(JPASSIST_AUDIO_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory "${JPASSIST_PLUGIN_STAGE}/jp_assist"
        COMMAND ${CMAKE_COMMAND} -E copy "${JPASSIST_LOCAL_AUDIO_ROOT}/audio_manifest.json"
                "${JPASSIST_PLUGIN_STAGE}/jp_assist/audio_manifest.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${JPASSIST_LOCAL_AUDIO_ROOT}/audio"
                "${JPASSIST_PLUGIN_STAGE}/jp_assist/audio"
    )
    set(JPASSIST_AUDIO_ARCHIVE_ENTRIES jp_assist/audio_manifest.json jp_assist/audio)
    set(JPASSIST_AUDIO_DEPENDENCIES "${JPASSIST_LOCAL_AUDIO_ROOT}/audio_manifest.json")
else()
    set(JPASSIST_AUDIO_COMMANDS)
    set(JPASSIST_AUDIO_ARCHIVE_ENTRIES)
    set(JPASSIST_AUDIO_DEPENDENCIES)
endif()

add_custom_command(
    OUTPUT "${JPASSIST_PLUGIN_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${JPASSIST_PLUGIN_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${JPASSIST_PLUGIN_STAGE}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/manifest.json"
            "${JPASSIST_PLUGIN_STAGE}/manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:jpassist_plugin_binary>"
            "${JPASSIST_PLUGIN_STAGE}/dist/jp-assist.bin"
    ${JPASSIST_CORPUS_COMMANDS}
    ${JPASSIST_AUDIO_COMMANDS}
    COMMAND ${CMAKE_COMMAND} -E rm -f "${JPASSIST_PLUGIN_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E chdir "${JPASSIST_PLUGIN_STAGE}"
            ${CMAKE_COMMAND} -E tar cf "${JPASSIST_PLUGIN_OUTPUT}" --format=zip
            manifest.json dist/jp-assist.bin ${JPASSIST_CORPUS_ARCHIVE_ENTRY} ${JPASSIST_AUDIO_ARCHIVE_ENTRIES}
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" jpassist_plugin_binary ${JPASSIST_CORPUS_DEPENDENCY}
            ${JPASSIST_AUDIO_DEPENDENCIES}
    VERBATIM
    COMMENT "Packaging the JP Assist binary plugin as jp-assist.o2r"
)

add_custom_target(jpassist_plugin_package ALL DEPENDS "${JPASSIST_PLUGIN_OUTPUT}")
