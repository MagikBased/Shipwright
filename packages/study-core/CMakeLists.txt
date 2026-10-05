find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL QUIET)

add_library(jp_assist_study_core STATIC
    src/LearningSyncClient.cpp
    src/LearningSyncStore.cpp
    src/StudySession.cpp
)
target_include_directories(jp_assist_study_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/jp_assist"
)
target_compile_features(jp_assist_study_core PUBLIC cxx_std_20)
target_link_libraries(jp_assist_study_core PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(jp_assist_study_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CURL_FOUND)
    target_compile_definitions(jp_assist_study_core PRIVATE JPASSIST_HAS_CURL=1)
    target_link_libraries(jp_assist_study_core PRIVATE CURL::libcurl)
endif()
