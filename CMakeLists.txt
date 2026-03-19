cmake_minimum_required(VERSION 3.20)
project(GaussianSplatPlugin)

set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Maya version — drives default DevKit / install paths and the API year define.
# Override any individual path to point elsewhere without changing MAYA_VERSION.
# ---------------------------------------------------------------------------
set(MAYA_VERSION "2024" CACHE STRING "Target Maya version (e.g. 2024, 2025, 2026)")

set(MAYA_DEVKIT
    "C:/Users/$ENV{USERNAME}/devkitBase${MAYA_VERSION}"
    CACHE PATH "Maya DevKit root (headers + import libs)")

set(MAYA_INSTALL
    "C:/Program Files/Autodesk/Maya${MAYA_VERSION}"
    CACHE PATH "Maya installation root (runtime DLLs)")

# ---------------------------------------------------------------------------
# Validate DevKit
# ---------------------------------------------------------------------------
if(NOT EXISTS "${MAYA_DEVKIT}/include/maya/MObject.h")
    message(FATAL_ERROR
        "Maya DevKit not found at '${MAYA_DEVKIT}'.\n"
        "Set -DMAYA_DEVKIT=<path> or -DMAYA_VERSION=<ver> to match your installation.")
endif()

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
    MAYA_VERSION=${MAYA_VERSION}
)

# ---------------------------------------------------------------------------
# Post-build: copy shaders alongside the .mll
# ---------------------------------------------------------------------------
add_custom_command(TARGET GaussianSplatPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/shaders"
        "$<TARGET_FILE_DIR:GaussianSplatPlugin>/shaders"
    COMMENT "Copying shaders to output directory"
)
