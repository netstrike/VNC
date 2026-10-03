add_executable(vnc_tests tests.cpp)
target_link_libraries(vnc_tests PRIVATE vnc_core)
add_test(NAME vnc_tests COMMAND vnc_tests)
