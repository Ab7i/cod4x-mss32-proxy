cmake_minimum_required(VERSION 3.15)
set(CMAKE_SYSTEM_NAME Windows)
project(cod4x-mss32-proxy C)

# Produce a DLL named exactly "mss32.dll" (no "lib" prefix)
set(CMAKE_SHARED_LIBRARY_PREFIX "")

# main.c + exports.def together — CMake passes the .def file to the linker
add_library(mss32 SHARED
    main.c
    exports.def
)

# 32-bit, size-optimized, hardened warnings
target_compile_options(mss32 PRIVATE
    -m32
    -Os
    -Wall
    -Wextra
)

# Static link runtime — no libgcc_s_dw2-1.dll dependency at runtime.
# --enable-stdcall-fixup: tolerate minor name-decoration mismatches.
target_link_options(mss32 PRIVATE
    -m32
    -static
    -static-libgcc
    -s
    -Wl,--enable-stdcall-fixup
)

# Only the Windows APIs we actually call from main.c
target_link_libraries(mss32 PRIVATE
    kernel32
    user32
)
