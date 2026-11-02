# Unit tests: the same cases as the Rust tests, one file per module.
#   ctest --test-dir build --output-on-failure
include(doctest)

add_executable(wpc_tests
  main.cpp
  test_color.cpp
  test_arc.cpp
  test_extract.cpp
  test_config.cpp
)
target_link_libraries(wpc_tests PRIVATE wpc_core doctest::doctest)
doctest_discover_tests(wpc_tests)
