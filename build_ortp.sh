#!/bin/bash
set -e # Exit immediately if a command exits with a non-zero status.
set -x # Print commands and their arguments as they are executed.

# Define base paths
BASE_DIR="/root/screamrouter/src/audio_engine"
BCTOOLBOX_SRC_DIR="${BASE_DIR}/bctoolbox"
BCUNIT_SRC_DIR="${BASE_DIR}/bcunit"
ORTP_SRC_DIR="${BASE_DIR}/ortp"

# Define local install directories for dependencies
BCTOOLBOX_INSTALL_DIR="${BCTOOLBOX_SRC_DIR}/_install"
BCUNIT_INSTALL_DIR="${BCUNIT_SRC_DIR}/_install"

echo "----------------------------------------"
echo "Building bctoolbox..."
echo "----------------------------------------"
cd "${BCTOOLBOX_SRC_DIR}"
rm -rf CMakeCache.txt Makefile cmake_install.cmake install_manifest.txt config.h bctoolbox.pc bctoolbox-tester.pc CMakeFiles/ _build/ _install/
mkdir -p "_build"
cd "_build"
cmake -DCMAKE_INSTALL_PREFIX="${BCTOOLBOX_INSTALL_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_TESTS_COMPONENT=OFF \
      -DENABLE_UNIT_TESTS=OFF \
      ..
make -j$(nproc)
make install

echo "----------------------------------------"
echo "Building bcunit..."
echo "----------------------------------------"
cd "${BCUNIT_SRC_DIR}"
rm -rf CMakeCache.txt Makefile cmake_install.cmake install_manifest.txt config.h bcunit.pc CMakeFiles/ BCUnitConfig.cmake BCUnitConfigVersion.cmake _build/ _install/
mkdir -p "_build"
cd "_build"
cmake -DCMAKE_INSTALL_PREFIX="${BCUNIT_INSTALL_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      ..
make -j$(nproc)
make install

echo "----------------------------------------"
echo "Patching installed BCToolboxConfig.cmake to avoid BCUnit variable collision..."
echo "----------------------------------------"
BCTOOLBOX_CONFIG_FILE_TO_PATCH="${BCTOOLBOX_INSTALL_DIR}/share/BCToolbox/cmake/BCToolboxConfig.cmake"
if [ -f "${BCTOOLBOX_CONFIG_FILE_TO_PATCH}" ]; then
    sed -i.bak 's/^[[:space:]]*find_dependency([[:space:]]*BCUnit[[:space:]]*)/#&/' "${BCTOOLBOX_CONFIG_FILE_TO_PATCH}"
    echo "Patched ${BCTOOLBOX_CONFIG_FILE_TO_PATCH}"
else
    echo "ERROR: Cannot find ${BCTOOLBOX_CONFIG_FILE_TO_PATCH} to patch."
    exit 1
fi

echo "----------------------------------------"
echo "Building ortp..."
echo "----------------------------------------"
cd "${ORTP_SRC_DIR}"
rm -rf _build/ CMakeCache.txt CMakeFiles/ Makefile cmake_install.cmake src/ortp
mkdir -p "_build"
cd "_build"

# Add -Wno-maybe-uninitialized to CMAKE_C_FLAGS for ortp to suppress specific compilation errors
# Add -DENABLE_UNIT_TESTS=OFF to disable ortp's own unit tests, which require bctoolbox/tester.h
cmake -DBCToolbox_DIR="${BCTOOLBOX_INSTALL_DIR}/share/BCToolbox/cmake" \
      -DBCUnit_DIR="${BCUNIT_INSTALL_DIR}/share/BCUnit/cmake" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-Wno-maybe-uninitialized" \
      -DENABLE_UNIT_TESTS=OFF \
      ..

make -j$(nproc)

echo "----------------------------------------"
echo "Build complete."
echo "ortp executable might be in ${ORTP_SRC_DIR}/_build/src/ortp (or similar path within _build)"
echo "----------------------------------------"
