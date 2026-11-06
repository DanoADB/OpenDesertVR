# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.24)
project(OpenDesertReflectionLab LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Reflection lab requires Windows x64 and Windows SDK DXC")
endif()
add_executable(odv_shader_reflection reflection.cpp)
target_compile_features(odv_shader_reflection PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(odv_shader_reflection PRIVATE /W4 /WX /EHsc)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_test(NAME reflection_owned COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/owned_reflection.py" "$<TARGET_FILE:odv_shader_reflection>")
set_tests_properties(reflection_owned PROPERTIES TIMEOUT 60)
# Research helper only, no install target or M0/game linkage.
