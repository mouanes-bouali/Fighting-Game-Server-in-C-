cmake_minimum_required(VERSION 3.20)
project(FightingGameServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Writes build/compile_commands.json so clangd gets the real flags
# (including the ENet include dir below).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

# ─── ENet ────────────────────────────────────────────────────────────
# ENet is installed into the MinGW prefix (C:/mingw64 on this machine).
# Point at another copy with -DENET_INCLUDE_DIR=... -DENET_LIBRARY=...
# On Windows ENet also needs winmm + ws2_32, added below.
find_path(ENET_INCLUDE_DIR NAMES enet/enet.h
    HINTS "C:/mingw64/include" "$ENV{MINGW_PREFIX}/include")

find_library(ENET_LIBRARY NAMES enet libenet
    HINTS "C:/mingw64/lib" "$ENV{MINGW_PREFIX}/lib")

if(NOT ENET_INCLUDE_DIR OR NOT ENET_LIBRARY)
    message(FATAL_ERROR
        "ENet not found (looked for enet/enet.h and libenet).\n"
        "Install it with:\n"
        "  git clone --depth 1 https://github.com/lsalzman/enet.git\n"
        "  cmake -S enet -B enet-build -G Ninja -DCMAKE_INSTALL_PREFIX=C:/mingw64\n"
        "  cmake --build enet-build --target install\n"
        "or configure with -DENET_INCLUDE_DIR=... -DENET_LIBRARY=...")
endif()

message(STATUS "ENet include : ${ENET_INCLUDE_DIR}")
message(STATUS "ENet library : ${ENET_LIBRARY}")

# ─── The server ──────────────────────────────────────────────────────
set(SERVER_SOURCES
    src/adapter/ENet/Protocol.cpp
    src/adapter/ENet/EnetManager.cpp)

# Picks up every test/*.cpp, so new test files need no CMake edit.
# (test/main.cpp defines CATCH_CONFIG_MAIN; the others just #include "catch.hpp".)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS test/*.cpp)

add_executable(tests ${TEST_SOURCES} ${SERVER_SOURCES})
target_include_directories(tests PRIVATE test src ${ENET_INCLUDE_DIR})
target_link_libraries(tests PRIVATE ${ENET_LIBRARY})

if(WIN32)
    target_link_libraries(tests PRIVATE winmm ws2_32 pthread)
endif()

add_test(NAME tests COMMAND tests)
