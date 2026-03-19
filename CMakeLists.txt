cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# vcpkg integration — must be configured before project().
#
# If the VCPKG_ROOT environment variable is set, the toolchain and triplet are
# applied automatically.  You can still override them on the command line:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE="..." -DVCPKG_TARGET_TRIPLET="..."
#
# Default triplet: x64-windows-static-md
#   Static GLEW linked into the .mll — no glew32.dll needed at runtime.
#   Uses /MD (dynamic CRT) to match Maya's own runtime, avoiding CRT conflicts.
# ---------------------------------------------------------------------------
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain (auto-detected from VCPKG_ROOT)")
    message(STATUS "[vcpkg] Auto-detected at $ENV{VCPKG_ROOT}")
endif()

if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING
        "vcpkg triplet — static lib + dynamic CRT (/MD) to match Maya")
endif()

project(GaussianSplatPlugin)

set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Maya DevKit — required, must be set explicitly.
# Example: cmake -B build -DMAYA_DEVKIT="C:/Users/you/devkitBase2024"
# ---------------------------------------------------------------------------
set(MAYA_DEVKIT "" CACHE PATH "Maya DevKit root (headers + import libs)")

if(NOT MAYA_DEVKIT)
    message(FATAL_ERROR
        "MAYA_DEVKIT is not set.\n"
        "Run: cmake -B build -DMAYA_DEVKIT=\"<path/to/devkitBase>\"")
endif()

if(NOT EXISTS "${MAYA_DEVKIT}/include/maya/MObject.h")
    message(FATAL_ERROR
        "Maya DevKit not found at '${MAYA_DEVKIT}'.\n"
        "Check that the path is correct and the DevKit is fully extracted.")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(GLEW CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "src/*.cpp")
list(APPEND SOURCES "third_party/tinyply/tinyply.cpp")

add_library(GaussianSplatPlugin SHARED ${SOURCES})

# ---------------------------------------------------------------------------
# Include / link
# ---------------------------------------------------------------------------
target_include_directories(GaussianSplatPlugin PRIVATE
    "${MAYA_DEVKIT}/include"
    third_party/tinyply
    src
)

target_link_directories(GaussianSplatPlugin PRIVATE
    "${MAYA_DEVKIT}/lib"
)

target_link_libraries(GaussianSplatPlugin PRIVATE
    GLEW::GLEW
    OpenMaya
    OpenMayaUI
    OpenMayaRender
    Foundation
)

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
set_target_properties(GaussianSplatPlugin PROPERTIES
    SUFFIX ".mll"
    PREFIX ""
)

target_compile_definitions(GaussianSplatPlugin PRIVATE
    NT_PLUGIN
    REQUIRE_IOSTREAM
    _USE_MATH_DEFINES
)

# GLEW_STATIC must be defined when linking against the static GLEW library.
if(VCPKG_TARGET_TRIPLET MATCHES "static")
    target_compile_definitions(GaussianSplatPlugin PRIVATE GLEW_STATIC)
endif()

# ---------------------------------------------------------------------------
# Post-build: copy shaders alongside the .mll
# ---------------------------------------------------------------------------
add_custom_command(TARGET GaussianSplatPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/shaders"
        "$<TARGET_FILE_DIR:GaussianSplatPlugin>/shaders"
    COMMENT "Copying shaders to output directory"
)
