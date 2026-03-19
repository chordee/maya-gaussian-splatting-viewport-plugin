cmake_minimum_required(VERSION 3.20)
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
