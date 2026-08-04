# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25)

project(repomancer
  VERSION 0.1.0
  DESCRIPTION "Cross-platform visual client for version control"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(REPOMANCER_BUILD_GUI "Build the wxWidgets GUI application" ON)
option(REPOMANCER_BUILD_TESTS "Build unit and integration tests" ON)
option(REPOMANCER_WERROR "Treat warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Hardening)

find_package(reproc++ CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_subdirectory(core)

if(REPOMANCER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(REPOMANCER_BUILD_GUI)
  if(UNIX)
    # CMake's FindwxWidgets locates wx-config with ONLY_CMAKE_FIND_ROOT_PATH,
    # and the vcpkg toolchain narrows the find root to vcpkg_installed —
    # pre-seed the cache so a system wxWidgets is usable when the vcpkg
    # 'gui' feature is off.
    find_program(wxWidgets_CONFIG_EXECUTABLE NAMES wx-config)
  endif()
  find_package(wxWidgets COMPONENTS stc aui core base)
  if(wxWidgets_FOUND)
    add_subdirectory(gui)
  else()
    message(STATUS "wxWidgets not found — GUI target skipped "
                   "(configure with the 'gui' vcpkg feature or install system wx)")
  endif()
endif()
