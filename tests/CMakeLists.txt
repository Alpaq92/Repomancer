# SPDX-License-Identifier: Apache-2.0

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(repomancer-tests
  fixture_repo.cpp
  test_porcelain.cpp
  test_log_format.cpp
  test_process_runner.cpp
  test_settings.cpp
  test_git_integration.cpp
)

target_link_libraries(repomancer-tests PRIVATE
  repomancer::core
  Catch2::Catch2WithMain
)

repomancer_warnings(repomancer-tests)
repomancer_harden(repomancer-tests)

include(Catch)
catch_discover_tests(repomancer-tests)
