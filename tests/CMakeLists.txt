# SPDX-License-Identifier: Apache-2.0

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(repomancer-tests
  fixture_repo.cpp
  test_porcelain.cpp
  test_log_format.cpp
  test_graph.cpp
  test_branch_model.cpp
  test_graph_branch_layout.cpp
  test_diff.cpp
  test_blame.cpp
  test_patch.cpp
  test_refs.cpp
  test_stats.cpp
  test_process_runner.cpp
  test_settings.cpp
  test_git_integration.cpp
  test_ssh_keys.cpp
  test_ssh_agent.cpp
  test_ssh_config.cpp
  test_ssh_known_hosts.cpp
  test_ssh_connection.cpp
  test_forge_oauth.cpp
  test_forge_api.cpp
)

target_link_libraries(repomancer-tests PRIVATE
  repomancer::core
  Catch2::Catch2WithMain
)

repomancer_warnings(repomancer-tests)
repomancer_harden(repomancer-tests)

include(Catch)
catch_discover_tests(repomancer-tests)
