cmake_minimum_required(VERSION 3.27)

project(cde
    DESCRIPTION "Common Desktop Environment window chrome for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SRC "src/*.cpp")

add_library(cde SHARED ${SRC})

# hyprpm builds with `make`, which emits no SONAME, so the plugin's path is its identity to
# dlopen. A shared SONAME breaks that: glibc reuses an already-mapped object with the same
# SONAME even when given a different path, which silently runs the old build (gotcha 3).
set_target_properties(cde PROPERTIES NO_SONAME ON)

# Hyprland plugins must not emit GNU-unique symbols. The cache statics inside inline
# functions (including the ABI client-hash helper) are otherwise STB_GNU_UNIQUE, and a
# previously mapped plugin's copy wins, so a fresh build reports the OLD build's hash.
# The Makefile passes --no-gnu-unique on its combined compile/link line; do it at compile
# time here, where it actually stops the compiler emitting UNIQUE bindings.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(cde PRIVATE -fno-gnu-unique)
    target_link_options(cde PRIVATE --no-gnu-unique)
endif()

# hyprland.pc already carries the plugin headers and pulls cairo in transitively.
# Do not hand-roll include dirs here; they drift with every hyprland bump.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
target_link_libraries(cde PRIVATE PkgConfig::HYPRLAND)

install(TARGETS cde)