add_executable(LFS.Settings WIN32
    main.cpp
    settings.rc
    resource.h
)

target_include_directories(LFS.Settings PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(LFS.Settings PRIVATE
    lfs_common
    comctl32
    shell32
    ole32
    user32
)

set_target_properties(LFS.Settings PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# app.manifest is embedded via settings.rc (visual styles + per-monitor DPI);
# stop the linker from generating a competing one.
set_property(TARGET LFS.Settings APPEND_STRING PROPERTY LINK_FLAGS " /MANIFEST:NO")

lfs_add_version_resource(LFS.Settings
    "Last Folder Standing settings" "LFS.Settings.exe" "VFT_APP" TRUE)
