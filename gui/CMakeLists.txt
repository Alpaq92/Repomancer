# SPDX-License-Identifier: Apache-2.0

include(${wxWidgets_USE_FILE})

add_executable(repomancer WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/main_frame.cpp
  src/log_model.cpp
)

target_link_libraries(repomancer PRIVATE
  repomancer::core
  ${wxWidgets_LIBRARIES}
)

repomancer_warnings(repomancer)
repomancer_harden(repomancer)
