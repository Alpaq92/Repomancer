# SPDX-License-Identifier: Apache-2.0

include(${wxWidgets_USE_FILE})

# wxbf (third_party/wxbf, wxWindows Library Licence 3.1): borderless frame +
# window gripper backing the integrated title bar. Windows and Linux only;
# macOS keeps the native frame.
if(WIN32 OR (UNIX AND NOT APPLE))
  file(GLOB WXBF_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/wxbf/src/*.cpp")
  add_library(wxbf STATIC ${WXBF_SRC})
  target_include_directories(wxbf PUBLIC "${CMAKE_SOURCE_DIR}/third_party/wxbf/include")
  target_link_libraries(wxbf PUBLIC ${wxWidgets_LIBRARIES})
  set_target_properties(wxbf PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  if(UNIX AND NOT APPLE)
    # borderless_frame_gtk.cpp includes <gdk/gdk.h> directly.
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
      pkg_check_modules(GTK3_WXBF IMPORTED_TARGET gtk+-3.0)
      if(GTK3_WXBF_FOUND)
        target_link_libraries(wxbf PUBLIC PkgConfig::GTK3_WXBF)
      endif()
    endif()
  endif()
endif()

add_executable(repomancer WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/main_frame.cpp
  src/remote_progress_dialog.cpp
  src/title_bar.cpp
  src/gtk_header_bar.cpp
  src/log_model.cpp
  src/log_view.cpp
  src/blame_dialog.cpp
  src/commit_dialog.cpp
  src/clone_dialog.cpp
  src/ssh_key_dialog.cpp
  src/entropy_dialog.cpp
  src/ssh_setup_dialog.cpp
  src/file_history_dialog.cpp
  src/preferences_dialog.cpp
  src/repo_view.cpp
  src/graph_renderer.cpp
  src/diff_view.cpp
  src/theme.cpp
)

target_link_libraries(repomancer PRIVATE
  repomancer::core
  ${wxWidgets_LIBRARIES}
)
if(TARGET wxbf)
  target_link_libraries(repomancer PRIVATE wxbf)
  target_compile_definitions(repomancer PRIVATE REPOMANCER_HAVE_WXBF=1)
endif()

# theme.cpp's wx<3.3 fallback talks to GtkSettings directly; wx does not
# propagate GTK include dirs.
if(UNIX AND NOT APPLE)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0)
    if(GTK3_FOUND)
      target_link_libraries(repomancer PRIVATE PkgConfig::GTK3)
    endif()
  endif()
endif()

repomancer_warnings(repomancer)
repomancer_harden(repomancer)

# Desktop integration assets (Linux): the application entry + icon that the
# Tier-0 shell menus (packaging/shell) reference by name. The .desktop is
# configured with the installed binary path.
if(UNIX AND NOT APPLE)
  set(REPOMANCER_BIN "${CMAKE_INSTALL_PREFIX}/bin/repomancer")
  configure_file(
    "${CMAKE_SOURCE_DIR}/packaging/shell/linux/repomancer.desktop"
    "${CMAKE_CURRENT_BINARY_DIR}/repomancer.desktop" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/repomancer.desktop"
          DESTINATION share/applications)
  install(FILES "${CMAKE_SOURCE_DIR}/packaging/icons/repomancer.svg"
          DESTINATION share/icons/hicolor/scalable/apps)
endif()
