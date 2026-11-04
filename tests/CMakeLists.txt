# Unit tests, one file per module.
#   ctest --test-dir build --output-on-failure
include(doctest)

# Driver modules only for the tests, in their own folder (never installed).
foreach(fake fake fake-other)
  add_library(wpc-driver-${fake} MODULE fake_driver.cpp)
  target_link_libraries(wpc-driver-${fake} PRIVATE wpc_drivers_sdk)
  target_compile_options(wpc-driver-${fake} PRIVATE -fno-gnu-unique)
  set_target_properties(wpc-driver-${fake} PROPERTIES
    PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endforeach()
target_compile_definitions(wpc-driver-fake-other PRIVATE FAKE_FROM_ANOTHER_BUILD)

add_executable(wpc_tests
  main.cpp
  test_color.cpp
  test_arc.cpp
  test_paint.cpp
  test_extract.cpp
  test_image.cpp
  test_config.cpp
  test_ipc.cpp
  test_openrgb.cpp
  test_fusion2.cpp
  test_razer.cpp
  test_modules.cpp
  test_kde.cpp
  test_settings.cpp
  test_service.cpp
  test_daemon_domain.cpp
  test_driver_modules.cpp
  test_terminal.cpp
)
target_link_libraries(wpc_tests PRIVATE wpc_daemon_infrastructure wpc_terminal_infrastructure
  wpc_driver_openrgb wpc_driver_fusion2 wpc_driver_razer wpc_png doctest::doctest)
target_compile_definitions(wpc_tests PRIVATE
  WPC_TEST_MODULES="${CMAKE_CURRENT_BINARY_DIR}/modules"
  WPC_BUILT_MODULES="${PROJECT_BINARY_DIR}/src/drivers")
add_dependencies(wpc_tests wpc_drivers wpc-driver-fake wpc-driver-fake-other)
doctest_discover_tests(wpc_tests)

# Every file is where its users are (docs/ARCHITECTURE.md, "Where things go").
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME "every file is where its users are"
    COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/cmake/usage_map.py ${PROJECT_SOURCE_DIR}/src --check)
endif()

# Each layer holds and includes only what it may (docs/ARCHITECTURE.md, "The layers").
if(Python3_FOUND)
  add_test(NAME "layers hold and include only what they may"
    COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/cmake/check_layers.py ${PROJECT_SOURCE_DIR}/src)
endif()
