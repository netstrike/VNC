add_library(vnc_windows STATIC desktop_capture.cpp input.cpp)
target_link_libraries(vnc_windows PUBLIC vnc_core d3d11 dxgi gdi32 user32)

add_executable(vnc_service service.cpp)
target_link_libraries(vnc_service PRIVATE advapi32 wtsapi32 userenv)
target_compile_definitions(vnc_service PRIVATE UNICODE _UNICODE)
