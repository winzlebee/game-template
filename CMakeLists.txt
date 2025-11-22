cmake_minimum_required(VERSION 3.15)
project(growth C)

find_package(raylib REQUIRED)

add_executable(${PROJECT_NAME} src/main.c)

target_link_libraries(${PROJECT_NAME} raylib)