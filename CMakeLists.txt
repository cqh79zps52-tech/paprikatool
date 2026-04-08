cmake_minimum_required(VERSION 3.16)
project(paprika_tool C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build a universal binary on macOS so the same artifact runs on
# both Apple Silicon and Intel Macs.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3 /D_CRT_SECURE_NO_WARNINGS /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(paprika
    src/main.c
    src/util.c
    src/installer.c
    src/downloader.c
    src/autocut.c
    src/config.c
)

target_include_directories(paprika PRIVATE include)

if(APPLE)
    set_target_properties(paprika PROPERTIES OUTPUT_NAME "paprika")
elseif(WIN32)
    set_target_properties(paprika PROPERTIES OUTPUT_NAME "paprika")
endif()

install(TARGETS paprika RUNTIME DESTINATION bin)
