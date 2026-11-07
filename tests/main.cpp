// doctest's main(): runs every TEST_CASE linked into wpc_tests.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>

// Pictures are read by the decoders' module (wpc-pictures.so): the one of this build.
static const int pictures_module = setenv("WPC_PICTURES_MODULE", WPC_BUILT_PICTURES, 1);
