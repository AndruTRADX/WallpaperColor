# wpc_driver(<name> SOURCES <files…> [LIBRARIES <targets…>])
#
# Builds a driver as its own file, wpc-driver-<name>.so, in <build>/src/drivers/ (where the
# programs look for it in the build folder). The programs never link it: they load it while
# one of its components is chosen (src/drivers/host).
#
#   SOURCES    the driver's code; src/drivers/<name>/module.cpp (wpc_driver_info) is added
#   LIBRARIES  what only this driver needs, linked into it alone
#
# It also makes wpc_driver_<name>, a static library of the same code for the tests.
#
# The module is built so that it can be unloaded and exports one symbol:
#   -fvisibility=hidden        only wpc_driver_info (marked visible in contracts/driver.hpp) is exported
#   -fno-gnu-unique            GCC's "unique" symbols would stop glibc from ever unloading it
#   --exclude-libs,ALL         code from static libraries (wpc_utils) stays private to it
#   --no-undefined             a missing symbol is a build error, not a load error later
function(wpc_driver name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})

  add_library(wpc_driver_${name} STATIC ${ARG_SOURCES})
  target_link_libraries(wpc_driver_${name} PUBLIC wpc_drivers_sdk ${ARG_LIBRARIES})
  target_compile_options(wpc_driver_${name} PRIVATE -fno-gnu-unique)
  set_target_properties(wpc_driver_${name} PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

  add_library(wpc-driver-${name} MODULE module.cpp)
  target_link_libraries(wpc-driver-${name} PRIVATE wpc_driver_${name})
  target_compile_options(wpc-driver-${name} PRIVATE -fno-gnu-unique)
  target_link_options(wpc-driver-${name} PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)
  set_target_properties(wpc-driver-${name} PROPERTIES
    PREFIX "" OUTPUT_NAME wpc-driver-${name}
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/src/drivers"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

  set_property(GLOBAL APPEND PROPERTY WPC_DRIVER_MODULES wpc-driver-${name})
endfunction()
