cmake_minimum_required(VERSION 3.27)

project(cde
    DESCRIPTION "Common Desktop Environment window chrome for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SRC "src/*.cpp")

add_library(cde SHARED ${SRC})

# hyprland.pc already carries the plugin headers and pulls cairo in transitively.
# Do not hand-roll include dirs here; they drift with every hyprland bump.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
target_link_libraries(cde PRIVATE PkgConfig::HYPRLAND)

install(TARGETS cde)