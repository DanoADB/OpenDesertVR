# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.24)
project(OpenDesertStaticOwned LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Static owned qualification requires Windows MSVC x64")
endif()
add_executable(odv_static_owned owned.cpp)
target_compile_features(odv_static_owned PRIVATE cxx_std_20)
target_compile_options(odv_static_owned PRIVATE /W4 /WX /EHsc)
target_link_options(odv_static_owned PRIVATE /INCREMENTAL:NO /OPT:NOICF)
get_filename_component(linker_directory "${CMAKE_LINKER}" DIRECTORY)
find_program(owned_dumpbin NAMES dumpbin.exe HINTS "${linker_directory}" REQUIRED NO_DEFAULT_PATH)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_test(NAME static_extent_owned COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/owned_static.py" "$<TARGET_FILE:odv_static_owned>" "${owned_dumpbin}")
set_tests_properties(static_extent_owned PROPERTIES TIMEOUT 30)
# No install target, game code, GPU work or M0 runtime linkage.
