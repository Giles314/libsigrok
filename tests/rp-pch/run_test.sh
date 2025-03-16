#!/bin/bash

# Unit tests for rp-pch driver
# Set current directory to:
#   libsigrok/tests/rp-pch/
# Then run this script:
#   ./run_test.sh

set -e

BUILD_DIR=../../build
GCC_OPTIONS="-g -O0 -c -Wall -Werror"
RP_PCH_SRC_DIR=../../src/hardware/rp-pch
C_INCLUDES="-I . -I ../.. -I ../../include -I $PKG_CONFIG_SYSTEM_INCLUDE_PATH/glib-2.0 -I $PKG_CONFIG_SYSTEM_LIBRARY_PATH/glib-2.0/include"
G_LIBS=" -dynamic -L $PKG_CONFIG_SYSTEM_LIBRARY_PATH -lglib-2.0"
gcc $GCC_OPTIONS $RP_PCH_SRC_DIR/protocol-rp.c -D UNIT_TEST $C_INCLUDES -o $BUILD_DIR/protocol-rp.o
gcc $GCC_OPTIONS $RP_PCH_SRC_DIR/api-rp.c -D UNIT_TEST $C_INCLUDES -o $BUILD_DIR/api-rp.o
gcc $GCC_OPTIONS test_rp-pch.c -D UNIT_TEST $C_INCLUDES -o $BUILD_DIR/test_rp-pch.o
gcc $GCC_OPTIONS stub.c -D UNIT_TEST $C_INCLUDES -o $BUILD_DIR/stub.o

gcc $BUILD_DIR/protocol-rp.o $BUILD_DIR/api-rp.o $BUILD_DIR/test_rp-pch.o $BUILD_DIR/stub.o $G_LIBS -o $BUILD_DIR/test_rp.exe
$BUILD_DIR/test_rp.exe