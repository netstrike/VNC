add_executable(vnc_server main.cpp)
target_link_libraries(vnc_server PRIVATE vnc_core)
if(WIN32)
  target_link_libraries(vnc_server PRIVATE vnc_windows)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(vnc_server PRIVATE vnc_linux)
else()
  # No real capture backend on this platform yet (e.g. macOS): link the
  # stub so the server still builds, with --source screen reporting
  # unavailable at runtime instead of failing to link.
  target_sources(vnc_server PRIVATE ${CMAKE_SOURCE_DIR}/core/src/screen_stub.cpp)
endif()
