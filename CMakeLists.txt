cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(OpenGL_GL_PREFERENCE GLVND)

project(CENG469-SSR LANGUAGES C CXX)

add_subdirectory(ext)

# Compile options
add_library(compile_options INTERFACE)
if(UNIX AND NOT APPLE)
    target_compile_options(
        compile_options INTERFACE
        -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wpedantic -Wconversion -Wsign-conversion
        -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough
    )
else()
    target_compile_options(
        compile_options INTERFACE
        /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296 /w14311 /w14545
        /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928
        /permissive-
        /external:anglebrackets /external:W0
        /D_CRT_SECURE_NO_WARNINGS
    )
endif()

# ImGui setup
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/imgui)
set(SRC_IMGUI
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

set(SRC_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utility.h
    ${SRC_IMGUI}
)

find_package(OpenGL)

add_executable(SSRProject)
target_sources(SSRProject PRIVATE ${SRC_ALL})
target_link_libraries(SSRProject
                      PRIVATE
                        glfw
                        glad
                        stb_image
                        glm
                        compile_options
                        OpenGL::GL)

target_include_directories(SSRProject PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)

set_target_properties(SSRProject PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/working_dir)

if(UNIX AND NOT APPLE)
    target_link_libraries(SSRProject PRIVATE X11)
else()
    target_link_libraries(SSRProject)
    set_target_properties(SSRProject PROPERTIES
                          VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/working_dir)
endif()
