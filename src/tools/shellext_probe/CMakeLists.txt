# Development-only harness for LFS.ShellExtension. Never shipped.
add_executable(shellext_probe main.cpp)

target_include_directories(shellext_probe PRIVATE
    "${CMAKE_SOURCE_DIR}/src/LFS.ShellExtension"
)

target_sources(shellext_probe PRIVATE
    "${CMAKE_SOURCE_DIR}/src/LFS.ShellExtension/pidl.cpp"
    "${CMAKE_SOURCE_DIR}/src/LFS.ShellExtension/guids.cpp"
)

target_link_libraries(shellext_probe PRIVATE
    lfs_common
    shell32
    shlwapi
    ole32
    uuid
)

set_target_properties(shellext_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
