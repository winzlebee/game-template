cmake_minimum_required(VERSION 3.15)

project(growth C CXX)

find_package(raylib REQUIRED)

add_subdirectory(extern/joltc)

include(cmake/MeshManifest.cmake)

add_executable(${PROJECT_NAME}-server src/server.c src/message.c src/ecs.c src/physics_jolt.c)
add_executable(${PROJECT_NAME}-client src/client.c src/message.c src/ecs.c src/physics_jolt.c src/orbit_camera.c)

target_link_libraries(${PROJECT_NAME}-client raylib joltc)
target_link_libraries(${PROJECT_NAME}-server raylib joltc)

target_include_directories(${PROJECT_NAME}-client PRIVATE
    ${CMAKE_SOURCE_DIR}/extern/nbnet
    ${CMAKE_BINARY_DIR})  # for mesh_manifest.h

target_include_directories(${PROJECT_NAME}-server PRIVATE ${CMAKE_SOURCE_DIR}/extern/nbnet ${CMAKE_BINARY_DIR})
