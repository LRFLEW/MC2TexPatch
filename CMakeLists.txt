cmake_minimum_required(VERSION 3.1)
project(MC2TexPatch)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
include_directories(${ZLIB_INCLUDE_DIRS})

file(GLOB SOURCES "*.cpp")
add_executable(MC2TexPatch ${SOURCES})
target_link_libraries(MC2TexPatch ${ZLIB_LIBRARIES})
