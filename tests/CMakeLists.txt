# Unit tests: the same cases as the Rust tests, one file per module.
#   ctest --test-dir build --output-on-failure
include(doctest)

add_executable(wpc_tests
  main.cpp
  test_color.cpp
  test_arc.cpp
  test_extract.cpp
  test_config.cpp
  test_ipc.cpp
  test_openrgb.cpp
  test_kde.cpp
)
target_link_libraries(wpc_tests PRIVATE wpc_core wpc_openrgb wpc_kde doctest::doctest)
doctest_discover_tests(wpc_tests)

# Prints what Plasma reports right now (needs a running Plasma session).
add_executable(kde_current kde_current.cpp)
target_link_libraries(kde_current PRIVATE wpc_kde)
