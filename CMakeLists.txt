cmake_minimum_required(VERSION 3.20)
project(GaussianSplatPlugin)

set(CMAKE_CXX_STANDARD 17)
# Point to DevKitBase for headers/libs
set(MAYA_DEVKIT "C:/Users/chordee.lin/devkitBase2024" CACHE PATH "Maya DevKit root")
# Point to Maya installation for potential runtime deps
set(MAYA_INSTALL "C:/Program Files/Autodesk/Maya2024" CACHE PATH "Maya install root")

file(GLOB_RECURSE SOURCES "src/*.cpp")
# Include tinyply implementation
list(APPEND SOURCES "third_party/tinyply/tinyply.cpp")

add_library(GaussianSplatPlugin SHARED ${SOURCES})

target_include_directories(GaussianSplatPlugin PRIVATE
    ${MAYA_DEVKIT}/include
    third_party/tinyply
    src
)

target_link_directories(GaussianSplatPlugin PRIVATE
    ${MAYA_DEVKIT}/lib
)

target_link_libraries(GaussianSplatPlugin PRIVATE
    OpenMaya
    OpenMayaUI
    OpenMayaRender
    Foundation
)

set_target_properties(GaussianSplatPlugin PROPERTIES
    SUFFIX ".mll"
    PREFIX ""
)

target_compile_definitions(GaussianSplatPlugin PRIVATE
    NT_PLUGIN
    REQUIRE_IOSTREAM
    _USE_MATH_DEFINES
)

# Copy shaders alongside the .mll so the plugin can find them at runtime
add_custom_command(TARGET GaussianSplatPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/shaders
        $<TARGET_FILE_DIR:GaussianSplatPlugin>/shaders
    COMMENT "Copying shaders to output directory"
)
