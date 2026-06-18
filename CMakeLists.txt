cmake_minimum_required(VERSION 3.16)
project(gxemul)

set(CMAKE_CXX_STANDARD 17)

set(VERSION "rs6000-860")
set(UNSTABLE_DEVEL 1)
set(WITH_X11 1)
set(HOST_LITTLE_ENDIAN 1)
configure_file("config.h.cmake" "${CMAKE_CURRENT_BINARY_DIR}/config.h")

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/include/ ${CMAKE_CURRENT_BINARY_DIR})

find_package(PNG REQUIRED)

add_executable(gxemul src/main/GXemul.cc)
add_compile_definitions(_FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE=1 _LARGEFILE64_SOURCE=1)
add_compile_options(-g)

if (WIN32)
    target_link_libraries(gxemul PRIVATE ws2_32)
endif()
target_link_libraries(gxemul PRIVATE SDL2::SDL2 PNG::PNG)

add_subdirectory(src/components)
add_subdirectory(src/console)
add_subdirectory(src/thirdparty)
add_subdirectory(src/cpus)
add_subdirectory(src/debugger)
add_subdirectory(src/devices)
add_subdirectory(src/disk)
add_subdirectory(src/file)
add_subdirectory(src/machines)
add_subdirectory(src/main)
add_subdirectory(src/net)
add_subdirectory(src/old_main)
add_subdirectory(src/promemul)
add_subdirectory(src/symbol)
add_subdirectory(src/ui)

target_link_libraries(gxemul PRIVATE components console cpus debugger devices disk file machines main net old_main promemul symbol softfloat ui)
