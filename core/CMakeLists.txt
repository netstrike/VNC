add_library(vnc_core STATIC
  src/protocol.cpp
  src/tcp_transport.cpp
  src/tls_transport.cpp
  src/transport_factory.cpp
  src/tile_diff.cpp
  src/capture_common.cpp
  src/auth.cpp
)
target_include_directories(vnc_core PUBLIC include)
find_package(OpenSSL REQUIRED)
target_link_libraries(vnc_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
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
