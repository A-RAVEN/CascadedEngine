#!/usr/bin/env bash
set -e

# MSVC toolchain (from CMake cache and VS installation)
MSVC_VER="14.50.35717"
MSVC_BASE="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/${MSVC_VER}"
MSVC_BIN="${MSVC_BASE}/bin/Hostx64/x64"
MSVC_INCLUDE="${MSVC_BASE}/include"
MSVC_LIB="${MSVC_BASE}/lib/x64"

# Windows SDK
SDK_VER="10.0.26100.0"
SDK_BASE="C:/Program Files (x86)/Windows Kits/10"
SDK_BIN="${SDK_BASE}/bin/${SDK_VER}/x64"
SDK_INCLUDE="${SDK_BASE}/Include/${SDK_VER}"
SDK_LIB="${SDK_BASE}/Lib/${SDK_VER}/um/x64"

# Ninja
NINJA="C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"

# Build directory
BUILD_DIR="E:/Projects/CascadedEngine/out/build/x64-relWithDebugInfo"

# Set environment
export PATH="${MSVC_BIN}:${SDK_BIN}:${PATH}"

export INCLUDE="${MSVC_INCLUDE};${SDK_INCLUDE}/ucrt;${SDK_INCLUDE}/um;${SDK_INCLUDE}/shared"
export LIB="${MSVC_LIB};${SDK_LIB};${SDK_BASE}/Lib/${SDK_VER}/ucrt/x64"
export LIBPATH="${MSVC_LIB};${SDK_LIB};${SDK_BASE}/Lib/${SDK_VER}/ucrt/x64"

echo "=== Building VulkanRenderBackendNew ==="
echo "MSVC: ${MSVC_BIN}"
echo "SDK:  ${SDK_VER}"

"${NINJA}" -C "${BUILD_DIR}" VulkanRenderBackendNew

echo ""
echo "BUILD SUCCESSFUL"