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

CPMAddPackage("gh:quicr/libquicr#main")
CPMAddPackage("gh:jarro2783/cxxopts@3.3.1")

if (TARGET fmt)
    set(SPDLOG_FMT_EXTERNAL ON)
endif()
CPMAddPackage("gh:gabime/spdlog@1.17.0")

#=============================================================================#
# Build MoQ Bench executable
#=============================================================================#

add_executable(moqbench src/bench.cpp src/publisher_track_handler.cpp src/subscriber_track_handler.cpp)
target_link_libraries(moqbench PRIVATE quicr cxxopts spdlog::spdlog)
target_include_directories(moqbench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(moqbench PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >
)

set_target_properties(moqbench PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)
target_compile_definitions(moqbench PRIVATE $<TARGET_PROPERTY:quicr,COMPILE_DEFINITIONS>)

#=============================================================================#
# Build Parallel Bench runner
#=============================================================================#

add_executable(parallel_moqbench src/parallel_bench.cpp)
target_link_libraries(parallel_moqbench PRIVATE quicr cxxopts)

target_compile_options(parallel_moqbench PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >
)

set_target_properties(parallel_moqbench PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_definitions(parallel_moqbench PRIVATE $<TARGET_PROPERTY:quicr,COMPILE_DEFINITIONS>)
