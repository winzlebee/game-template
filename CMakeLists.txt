cmake_minimum_required(VERSION 3.15)
project(growth C)

find_package(raylib REQUIRED)

add_subdirectory(extern/joltc)

add_executable(${PROJECT_NAME}-server src/server.c src/message.c)
add_executable(${PROJECT_NAME}-client src/client.c src/message.c src/physics.c)

target_link_libraries(${PROJECT_NAME}-client raylib joltc)
target_link_libraries(${PROJECT_NAME}-server raylib)

target_include_directories(${PROJECT_NAME}-client PRIVATE ${CMAKE_SOURCE_DIR}/extern/nbnet)
target_include_directories(${PROJECT_NAME}-server PRIVATE ${CMAKE_SOURCE_DIR}/extern/nbnet)