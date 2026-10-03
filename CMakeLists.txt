cmake_minimum_required(VERSION 3.20)
project(vnc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VNC_BUILD_TESTS "Build tests" ON)

add_subdirectory(core)
add_subdirectory(server)
add_subdirectory(client)

if(WIN32)
  add_subdirectory(platform/windows)
endif()

if(VNC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
