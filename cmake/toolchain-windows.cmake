# ---
# Windows cross-compilation toolchain  (Linux host → Windows x86-64 target)
# Uses MXE (M cross environment) with a static Qt6 build.
#
# -- One-time setup ---
#
#   1. Clone and build MXE (this takes a while; Qt6 is large):
#
#       git clone https://github.com/mxe/mxe /opt/mxe
#       cd /opt/mxe
#       make qt6-qtbase qt6-qtnetwork \
#            MXE_TARGETS="x86_64-w64-mingw32.static" \
#            MXE_PLUGIN_DIRS="plugins/llvm-mingw"   # optional: use clang instead of GCC
#
#   2. (Optional) Add /opt/mxe/usr/bin to PATH so cmake can find the cross-tools:
#
#       export PATH="/opt/mxe/usr/bin:$PATH"
#
#   3. Build from the project root:
#
#       cmake -B build_windows_release \
#             -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows.cmake \
#             -DCMAKE_BUILD_TYPE=Release
#       cmake --build build_windows_release
#
#   The resulting nerevarine_organizer.exe is fully self-contained
#   (no external DLLs needed) because MXE links Qt6 statically.
#
# -- Customisation ---
#   Override MXE_ROOT at configure time if MXE is installed elsewhere:
#       cmake ... -DMXE_ROOT=/home/user/mxe ...
# ---

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# MXE installation root - override with -DMXE_ROOT=... if needed
set(MXE_ROOT "/opt/mxe" CACHE PATH "MXE root directory")
set(MXE_TARGET "x86_64-w64-mingw32.static")

# Cross-compiler binaries provided by MXE
set(CMAKE_C_COMPILER   "${MXE_ROOT}/usr/bin/${MXE_TARGET}-gcc")
set(CMAKE_CXX_COMPILER "${MXE_ROOT}/usr/bin/${MXE_TARGET}-g++")
set(CMAKE_RC_COMPILER  "${MXE_ROOT}/usr/bin/${MXE_TARGET}-windres")
set(CMAKE_AR           "${MXE_ROOT}/usr/bin/${MXE_TARGET}-ar")
set(CMAKE_RANLIB       "${MXE_ROOT}/usr/bin/${MXE_TARGET}-ranlib")
set(CMAKE_STRIP        "${MXE_ROOT}/usr/bin/${MXE_TARGET}-strip")

# Tell CMake where to find libraries and headers for the target
set(CMAKE_FIND_ROOT_PATH "${MXE_ROOT}/usr/${MXE_TARGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # use host tools (cmake, moc, etc.)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # target libraries only
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)    # target headers only
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)    # target cmake packages only

# Point find_package(Qt6 ...) at the MXE-built Qt6
set(Qt6_DIR "${MXE_ROOT}/usr/${MXE_TARGET}/qt6/lib/cmake/Qt6"
    CACHE PATH "Qt6 cmake config directory (MXE)")

# MXE static builds define this; CMakeLists.txt uses it to skip windeployqt
set(MXE_TARGET "${MXE_TARGET}" CACHE STRING "MXE target triple")

# Static Qt6 plugin initialisation - needed so Qt can find its built-in plugins
# (platform, imageformat, etc.) at runtime inside the .exe
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
