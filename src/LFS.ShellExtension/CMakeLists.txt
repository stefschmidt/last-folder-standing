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

# x64 builds LFS.ShellExtension.dll, x86 builds LFS.ShellExtension32.dll. They
# ship side by side in the same folder, so the names have to differ.
set_target_properties(LFS.ShellExtension PROPERTIES
    OUTPUT_NAME "${LFS_EXTENSION_OUTPUT_NAME}"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Static CRT: the DLL is loaded into explorer.exe and every file-dialog host.
# Depending on a redistributable there is asking for a process that cannot
# start because of us.
set_property(TARGET LFS.ShellExtension PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The icon rides along so the registered DefaultIcon can point at the DLL.
# The description says which of the two DLLs you are looking at in Explorer.
if(LFS_BUILD_32BIT)
    set(lfs_ext_description "Last Folder Standing navigation pane extension (32-bit)")
else()
    set(lfs_ext_description "Last Folder Standing navigation pane extension")
endif()
lfs_add_version_resource(LFS.ShellExtension
    "${lfs_ext_description}" "${LFS_EXTENSION_OUTPUT_NAME}.dll" "VFT_DLL" TRUE)
