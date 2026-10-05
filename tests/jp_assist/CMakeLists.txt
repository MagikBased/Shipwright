include(FetchContent)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.16.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(jpassist_tests
    test_dialogue_presentation.cpp
    test_host.cpp
    test_audio_math.cpp
    test_learning_sync_store.cpp
    test_learning_sync_client.cpp
    test_message_parser.cpp
    test_native_highlight.cpp
    test_native_page_tracker.cpp
    test_overlay_layout.cpp
    test_scenario_parser.cpp
    test_study_selection_memory.cpp
    test_study_session.cpp
    test_study_token.cpp
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist/MessageParser.cpp
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist/JPAssistHost.cpp
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist/StudySession.cpp
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist/LearningSyncStore.cpp
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist/LearningSyncClient.cpp
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist/TestScenarioParser.cpp
)

target_include_directories(jpassist_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/soh
    ${CMAKE_SOURCE_DIR}/soh/include
    ${CMAKE_SOURCE_DIR}/soh/src
    ${CMAKE_SOURCE_DIR}/libultraship/include
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist
)
target_compile_definitions(jpassist_tests PRIVATE F3DEX_GBI_2)
target_link_libraries(jpassist_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(jpassist_tests)

if(ENABLE_SCRIPTING AND TARGET jpassist_plugin_package)
    add_executable(jpassist_plugin_tests
        test_plugin_loader.cpp
        ${CMAKE_SOURCE_DIR}/soh/soh/ModApi/StudyModApi.cpp
    )
    target_include_directories(jpassist_plugin_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/soh
        ${CMAKE_SOURCE_DIR}/soh/include
        ${CMAKE_SOURCE_DIR}/soh/soh/ModApi
        ${CMAKE_SOURCE_DIR}/libultraship/include
    )
    target_compile_definitions(jpassist_plugin_tests PRIVATE
        ENABLE_SCRIPTING
        STUDY_MOD_HOST_EXPORTS
        JPASSIST_PLUGIN_ARCHIVE_PATH="${CMAKE_BINARY_DIR}/games/ocarina-of-time/adapters/shipwright/plugin/jp-assist.o2r"
        JPASSIST_TCC_RUNTIME_DIR="$<TARGET_FILE_DIR:libtcc1>"
    )
    target_link_libraries(jpassist_plugin_tests PRIVATE libultraship GTest::gtest_main)
    if(UNIX AND NOT APPLE)
        target_link_options(jpassist_plugin_tests PRIVATE -Wl,--export-dynamic)
    endif()
    add_dependencies(jpassist_plugin_tests jpassist_plugin_package)
    gtest_discover_tests(jpassist_plugin_tests)
endif()
