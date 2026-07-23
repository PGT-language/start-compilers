cmake_minimum_required(VERSION 3.10)
project(pgt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

include_directories(src/include/*.h)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(pgt ${SOURCES})

target_compile_options(pgt PRIVATE -g -Wall -Wextra -Wno-sign-compare -Wno-deprecated-declarations)