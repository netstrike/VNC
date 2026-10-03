find_package(PkgConfig REQUIRED)
pkg_check_modules(X11_DEPS REQUIRED x11 xext xfixes xdamage xtst)

add_library(vnc_linux STATIC x11_capture.cpp x11_input.cpp)
target_link_libraries(vnc_linux PUBLIC vnc_core ${X11_DEPS_LIBRARIES})
target_include_directories(vnc_linux PRIVATE ${X11_DEPS_INCLUDE_DIRS})
target_compile_options(vnc_linux PRIVATE ${X11_DEPS_CFLAGS_OTHER})
