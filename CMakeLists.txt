cmake_minimum_required(VERSION 3.21)

project(LastFolderStanding
    VERSION 0.4.0
    DESCRIPTION "Most recently used folders, pinned into the Explorer navigation pane"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "This project targets MSVC / Windows only.")
endif()

# Two architectures, deliberately. Monitor and settings window are x64 only, but
# the shell extension gets loaded into whatever process shows a file dialog --
# and a 32-bit process can only load a 32-bit DLL. Plenty of those are still
# around (eM Client, older Adobe tools, a lot of audio software), so the
# extension is built a second time as x86 and registered in the 32-bit view.
# Configure a separate build tree with -A Win32 for it; build.ps1 does that.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(LFS_BUILD_32BIT ON)
    set(LFS_EXTENSION_OUTPUT_NAME "LFS.ShellExtension32")
else()
    set(LFS_BUILD_32BIT OFF)
    set(LFS_EXTENSION_OUTPUT_NAME "LFS.ShellExtension")
endif()

add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus)
add_compile_definitions(
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    STRICT
)

# Header-only helpers shared by Monitor and ShellExtension.
add_library(lfs_common INTERFACE)
target_include_directories(lfs_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(LFS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(LFS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(LFS_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(LFS_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico")

# Stamps version info (and optionally the app icon) into a target's binary.
# Icon resource id 1 is what Windows shows for the executable itself.
function(lfs_add_version_resource target description filename filetype with_icon)
    set(LFS_FILE_DESCRIPTION "${description}")
    set(LFS_ORIGINAL_FILENAME "${filename}")
    set(LFS_TARGET_NAME "${target}")
    set(LFS_FILETYPE "${filetype}")
    if(with_icon AND EXISTS "${LFS_APP_ICON}")
        set(LFS_ICON_STATEMENT "1 ICON \"${LFS_APP_ICON}\"")
    else()
        set(LFS_ICON_STATEMENT "")
    endif()

    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${CMAKE_SOURCE_DIR}/src/common/version.rc.in" "${generated}" @ONLY)
    target_sources(${target} PRIVATE "${generated}")
endfunction()

add_subdirectory(src/LFS.ShellExtension)
add_subdirectory(src/tools/shellext_probe)

# The 32-bit tree exists for the extension DLL alone. Building the x64-only
# binaries a second time would produce nothing anyone installs.
if(NOT LFS_BUILD_32BIT)
    add_subdirectory(src/LFS.Monitor)
    add_subdirectory(src/LFS.Settings)
    add_subdirectory(src/tools/shell_activity_probe)
endif()
