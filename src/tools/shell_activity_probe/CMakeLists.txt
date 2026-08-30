# Development-only measurement tool for Explorer activity sources. Never shipped.
add_executable(shell_activity_probe main.cpp)

target_link_libraries(shell_activity_probe PRIVATE
    lfs_common
    shell32
    shlwapi
    ole32
    oleaut32
    uuid
    user32
)

set_target_properties(shell_activity_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
