# SPDX-License-Identifier: Apache-2.0

add_library(repomancer-core STATIC
  src/process/process_runner.cpp
  src/settings.cpp
  src/vcs/graph.cpp
  src/vcs/branch_model.cpp
  src/vcs/blame.cpp
  src/vcs/diff.cpp
  src/vcs/patch.cpp
  src/vcs/refs.cpp
  src/vcs/stats.cpp
  src/vcs/git/porcelain.cpp
  src/vcs/git/log_format.cpp
  src/vcs/git/git_driver.cpp
)
add_library(repomancer::core ALIAS repomancer-core)

# std::getenv (HOME/APPDATA lookup in settings.cpp) is safe here; MSVC's /sdl
# otherwise promotes its C4996 deprecation to a hard error.
if(MSVC)
  target_compile_definitions(repomancer-core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(repomancer-core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(repomancer-core PUBLIC cxx_std_20)

target_link_libraries(repomancer-core
  PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
  PRIVATE
    reproc++
)

repomancer_warnings(repomancer-core)
repomancer_harden(repomancer-core)
