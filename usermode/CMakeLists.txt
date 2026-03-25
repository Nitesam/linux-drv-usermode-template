cmake_minimum_required(VERSION 3.16)
project(gsd-housekeeping LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output binary to /root/NTS_WORK
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY /root/NTS_WORK)

# ── Find dependencies ────────────────────────────────────────────
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(X11 REQUIRED)

# ── ImGui sources ────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# ── Application ──────────────────────────────────────────────────
add_executable(gsd-housekeeping
    main.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(gsd-housekeeping PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
    ${CMAKE_CURRENT_SOURCE_DIR}/games/hll
    ${CMAKE_CURRENT_SOURCE_DIR}/games/dbd
)

target_link_libraries(gsd-housekeeping PRIVATE
    OpenGL::GL
    glfw
    X11::X11
    Xext
    Xfixes
    dl
    pthread
)


target_compile_options(gsd-housekeeping PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
target_link_options(gsd-housekeeping PRIVATE -s)
