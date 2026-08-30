add_executable(LFS.Monitor
    main.cpp
    app_paths.cpp
    settings.cpp
    snapshot.cpp
    mru_reader.cpp
    recent_reader.cpp
    pipeline.cpp
    state_writer.cpp
    watcher.cpp
    tray.cpp
    folder_entry.h
    app_paths.h
    settings.h
    snapshot.h
    mru_reader.h
    recent_reader.h
    pipeline.h
    state_writer.h
    watcher.h
    tray.h
)

target_link_libraries(LFS.Monitor PRIVATE
    lfs_common
    shell32
    shlwapi
    ole32
    advapi32
    user32
)

set_target_properties(LFS.Monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DPI_AWARE "PerMonitor"
)

lfs_add_version_resource(LFS.Monitor
    "Last Folder Standing monitor" "LFS.Monitor.exe" "VFT_APP" TRUE)

# Console subsystem: --console and --dump are the dev/test modes and need a
# console. The tray build hides its window at startup instead (see main.cpp),
# which costs a brief flash but keeps one binary for both jobs.
