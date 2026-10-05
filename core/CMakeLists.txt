add_library(vnc_core STATIC
  src/protocol.cpp
  src/tcp_transport.cpp
  src/tls_transport.cpp
  src/transport_factory.cpp
  src/tile_diff.cpp
  src/capture_common.cpp
  src/auth.cpp
  src/compress.cpp
)
target_include_directories(vnc_core PUBLIC include)
find_package(OpenSSL REQUIRED)
target_link_libraries(vnc_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
# vcpkg (common on Windows) ships a CMake config; everywhere else (Linux
# distro packages, MSYS2) pkg-config's libzstd.pc is what's available.
find_package(zstd CONFIG QUIET)
if(zstd_FOUND)
  target_link_libraries(vnc_core PUBLIC $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,zstd::libzstd_shared,zstd::libzstd_static>)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ZSTD REQUIRED libzstd)
  target_link_libraries(vnc_core PUBLIC ${ZSTD_LIBRARIES})
  target_include_directories(vnc_core PRIVATE ${ZSTD_INCLUDE_DIRS})
  target_link_directories(vnc_core PUBLIC ${ZSTD_LIBRARY_DIRS})
endif()
if(WIN32)
  target_link_libraries(vnc_core PUBLIC ws2_32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(vnc_core PUBLIC Threads::Threads)
endif()
if(MSVC)
  target_compile_options(vnc_core PRIVATE /W4)
else()
  target_compile_options(vnc_core PRIVATE -Wall -Wextra)
endif()
