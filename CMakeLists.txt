# SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.10)

project(moq-bench)

#=============================================================================#
# Add dependencies
#=============================================================================#

set(CPM_USE_LOCAL_PACKAGES ON)

include(cmake/CPM.cmake)

## hack fix for picotls and other older submodules of libquicr
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

CPMAddPackage("gh:Quicr/libquicr#main")
CPMAddPackage("gh:jarro2783/cxxopts@3.3.1")

#=============================================================================#
# Build QPerf executable
#=============================================================================#

add_executable(moqbench_meeting src/meeting.cpp src/publisher_track_handler.cpp src/subscriber_track_handler.cpp)
target_link_libraries(moqbench_meeting PRIVATE quicr cxxopts spdlog::spdlog)
target_include_directories(moqbench_meeting PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(moqbench_meeting PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >
)

set_target_properties(moqbench_meeting PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_definitions(moqbench_meeting PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

#=============================================================================#
# Build QPerf Publication executable
#=============================================================================#

add_executable(moqbench_pub src/pub.cpp src/publisher_track_handler.cpp)
target_link_libraries(moqbench_pub PRIVATE quicr cxxopts spdlog::spdlog)
target_include_directories(moqbench_pub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(moqbench_pub PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >
)

set_target_properties(moqbench_pub PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_definitions(moqbench_pub PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

#=============================================================================#
# Build QPerf Subscription executable
#=============================================================================#

add_executable(moqbench_sub src/sub.cpp src/subscriber_track_handler.cpp)
target_link_libraries(moqbench_sub PRIVATE quicr cxxopts spdlog::spdlog)
target_include_directories(moqbench_sub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(moqbench_sub PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >
)

set_target_properties(moqbench_sub PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_definitions(moqbench_sub PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
