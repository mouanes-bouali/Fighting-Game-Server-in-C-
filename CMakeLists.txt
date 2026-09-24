cmake_minimum_required(VERSION 3.20)
project(FightingGameServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Writes build/compile_commands.json so clangd gets the real flags.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

# Picks up every test/*.cpp, so new test files need no CMake edit.
# (test/main.cpp defines CATCH_CONFIG_MAIN; the others just #include "catch.hpp".)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS test/*.cpp)

add_executable(tests ${TEST_SOURCES})
target_include_directories(tests PRIVATE test src)

add_test(NAME tests COMMAND tests)

