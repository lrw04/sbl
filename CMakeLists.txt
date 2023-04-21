cmake_minimum_required(VERSION 3.14)
project(sbl)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)

Set(FETCHCONTENT_QUIET FALSE)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent
    GIT_PROGRESS TRUE GIT_SHALLOW TRUE GIT_TAG v3.10.5
    USES_TERMINAL_DOWNLOAD TRUE)
FetchContent_MakeAvailable(json)

add_executable(sbl sbl.cpp)
target_link_libraries(sbl nlohmann_json::nlohmann_json)
