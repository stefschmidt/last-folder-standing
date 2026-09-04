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

# So the probe defaults to the DLL from its own build tree. The 32-bit probe
# must load the 32-bit DLL: LoadLibrary on the wrong one fails outright.
target_compile_definitions(shellext_probe PRIVATE
    "LFS_EXTENSION_DLL_NAME=L\"${LFS_EXTENSION_OUTPUT_NAME}.dll\""
)

set_target_properties(shellext_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
