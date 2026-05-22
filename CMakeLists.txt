cmake_minimum_required(VERSION 3.16)
project(gxemul)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/include/)

add_compile_definitions(-DNDEBUG)

add_executable(gxemul src/main/GXemul.cc)
add_dependencies(gxemul generate_commands)
add_dependencies(gxemul generate_components)
target_link_libraries(gxemul PRIVATE SDL2::SDL2)

add_subdirectory(src/components)
add_subdirectory(src/console)
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
add_subdirectory(src/thirdparty)
add_subdirectory(src/ui)

target_link_libraries(gxemul PRIVATE components cpus debugger devices disk file machines main net old_main promemul symbol softfloat ui)
