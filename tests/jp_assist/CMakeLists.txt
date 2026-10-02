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
    test_message_parser.cpp
    test_scenario_parser.cpp
    test_study_token.cpp
    ${CMAKE_SOURCE_DIR}/soh/soh/Enhancements/JPAssist/MessageParser.cpp
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
