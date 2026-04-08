cmake_minimum_required(VERSION 3.16)
project(paprika_tool C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(PAPRIKA_BUILD_GUI "Build the raylib + raygui GUI front-end" ON)

# Universal macOS build (Apple Silicon + Intel) so one artifact runs on both.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3 /D_CRT_SECURE_NO_WARNINGS /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

set(PAPRIKA_SOURCES
    src/main.c
    src/util.c
    src/installer.c
    src/downloader.c
    src/autocut.c
    src/config.c
    src/thread.c
    src/job.c
)

if(PAPRIKA_BUILD_GUI)
    list(APPEND PAPRIKA_SOURCES src/gui.c)
endif()

# ── On Windows we want a windowed app so launching from Explorer doesn't
#    pop a console. CLI/scripting modes still work from a terminal — both
#    cmd.exe and PowerShell will print stdout when launched from a shell. ──
if(WIN32 AND PAPRIKA_BUILD_GUI)
    add_executable(paprika WIN32 ${PAPRIKA_SOURCES})
    # WIN32 subsystem normally expects WinMain. Force the CRT to call the
    # plain `int main` we already have.
    if(MSVC)
        target_link_options(paprika PRIVATE "/ENTRY:mainCRTStartup")
    endif()
else()
    add_executable(paprika ${PAPRIKA_SOURCES})
endif()

target_include_directories(paprika PRIVATE include)

if(PAPRIKA_BUILD_GUI)
    target_compile_definitions(paprika PRIVATE PAPRIKA_HAS_GUI=1)

    include(FetchContent)

    set(FETCHCONTENT_QUIET OFF)

    # raylib — pure C, MIT, builds clean on MSVC and clang.
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    set(SUPPORT_FILEFORMAT_FLAC OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(raylib)

    # raygui — single-header widgets by the same author.
    FetchContent_Declare(
        raygui
        GIT_REPOSITORY https://github.com/raysan5/raygui.git
        GIT_TAG        4.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(raygui)

    target_include_directories(paprika PRIVATE ${raygui_SOURCE_DIR}/src)
    target_link_libraries(paprika PRIVATE raylib)

    if(APPLE)
        target_link_libraries(paprika PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
            "-framework OpenGL")
    elseif(WIN32)
        target_link_libraries(paprika PRIVATE winmm gdi32 opengl32)
    endif()
endif()

# pthreads on POSIX for our thread shim.
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(paprika PRIVATE Threads::Threads)
endif()

set_target_properties(paprika PROPERTIES OUTPUT_NAME "paprika")

install(TARGETS paprika RUNTIME DESTINATION bin)
