cmake_minimum_required(VERSION 3.16)

project(reed_r8080 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(reed-r8080
    src/main.cpp
)

target_include_directories(reed-r8080 PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_compile_options(reed-r8080 PRIVATE -Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
target_link_libraries(reed-r8080 PRIVATE Threads::Threads)
