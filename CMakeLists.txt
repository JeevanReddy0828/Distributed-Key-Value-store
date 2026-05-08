cmake_minimum_required(VERSION 3.20)
project(OrcDB VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerOptions)

# ── Options ────────────────────────────────────────────────────────────────────
option(ORCDB_BUILD_TESTS     "Build unit and integration tests" ON)
option(ORCDB_ENABLE_SANITIZERS "Enable AddressSanitizer + ThreadSanitizer" OFF)
option(ORCDB_ENABLE_COVERAGE   "Enable gcov/lcov coverage instrumentation"  OFF)

# ── Dependencies ───────────────────────────────────────────────────────────────
find_package(OpenSSL  REQUIRED)
find_package(Threads  REQUIRED)

include(FetchContent)

# Prefer system-installed packages (available in Docker via apt); fall back to FetchContent
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.13.0
    )
    FetchContent_MakeAvailable(spdlog)
endif()

find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(yaml_cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
    )
    FetchContent_MakeAvailable(yaml_cpp)
endif()

# yaml-cpp system package uses target yaml-cpp, FetchContent uses yaml-cpp::yaml-cpp
if(TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()

# ── Core library ───────────────────────────────────────────────────────────────
file(GLOB_RECURSE ORCDB_SOURCES
    "src/util/*.cpp"
    "src/storage/*.cpp"
    "src/raft/*.cpp"
    "src/network/*.cpp"
    "src/api/*.cpp"
    "src/auth/*.cpp"
    "src/metrics/*.cpp"
)

add_library(orcdb_core STATIC ${ORCDB_SOURCES})

target_include_directories(orcdb_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(orcdb_core PUBLIC
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    yaml-cpp::yaml-cpp
)

apply_compiler_options(orcdb_core)

# ── Executable ─────────────────────────────────────────────────────────────────
add_executable(orcdb src/main.cpp)
target_link_libraries(orcdb PRIVATE orcdb_core)
apply_compiler_options(orcdb)

# ── Tests ──────────────────────────────────────────────────────────────────────
if(ORCDB_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()
