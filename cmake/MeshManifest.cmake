# Scan res/meshes for .glb files and generate a mesh manifest header
# that includes MESH_* enum values and ANIM_* animation index defines.
#
# The scanner re-runs automatically when any .glb file is added, removed, or modified.

file(GLOB_RECURSE MESH_FILES "${CMAKE_SOURCE_DIR}/res/meshes/*.glb")
list(SORT MESH_FILES)
list(LENGTH MESH_FILES MESH_COUNT)

add_executable(anim-scanner cmake/anim_scanner.c)
target_link_libraries(anim-scanner raylib)

set(MANIFEST_H "${CMAKE_BINARY_DIR}/mesh_manifest.h")

add_custom_command(
    OUTPUT "${MANIFEST_H}"
    COMMAND anim-scanner "${MANIFEST_H}" "${CMAKE_SOURCE_DIR}" ${MESH_FILES}
    DEPENDS anim-scanner ${MESH_FILES}
    COMMENT "Generating mesh_manifest.h (${MESH_COUNT} meshes)"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MESH_FILES}")
add_custom_target(mesh_manifest DEPENDS "${MANIFEST_H}")
