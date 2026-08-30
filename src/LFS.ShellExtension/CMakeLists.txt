add_library(LFS.ShellExtension SHARED
    dll.cpp
    guids.cpp
    class_factory.cpp
    root_folder.cpp
    enum_children.cpp
    extract_icon.cpp
    state_reader.cpp
    pidl.cpp
    LFS.ShellExtension.def
    dll.h
    class_factory.h
    root_folder.h
    enum_children.h
    extract_icon.h
    state_reader.h
    pidl.h
)

target_link_libraries(LFS.ShellExtension PRIVATE
    lfs_common
    shell32
    shlwapi
    ole32
    uuid
    advapi32
    user32
)

set_target_properties(LFS.ShellExtension PROPERTIES
    OUTPUT_NAME "LFS.ShellExtension"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Static CRT: the DLL is loaded into explorer.exe and every file-dialog host.
# Depending on a redistributable there is asking for a process that cannot
# start because of us.
set_property(TARGET LFS.ShellExtension PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The icon rides along so the registered DefaultIcon can point at the DLL.
lfs_add_version_resource(LFS.ShellExtension
    "Last Folder Standing navigation pane extension" "LFS.ShellExtension.dll" "VFT_DLL" TRUE)
