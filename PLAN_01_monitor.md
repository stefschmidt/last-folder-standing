# PLAN 01 — LFS.Monitor

Background process that turns Windows' scattered "recently used folder" traces into one
clean, filtered, ordered list: `state.json`.

## Sources

### Source A: Common-dialog MRU (primary)

- `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ComDlg32\OpenSavePidlMRU`
  - Subkeys per extension (`wav`, `pdf`, `*`, ...), values `0,1,2,...` = binary PIDLs,
    value `MRUListEx` = DWORD array giving recency order (first = newest), terminated 0xFFFFFFFF
  - PIDL points to the **file**; we want its parent folder → `ILRemoveLastID`
- `HKCU\...\ComDlg32\LastVisitedPidlMRU`
  - Values pair an executable name (UTF-16) with a PIDL of the **folder** last used by
    that executable. Same MRUListEx mechanism.
- Decode PIDLs with `SHGetPathFromIDListW` after `ILClone`/validation. Reject anything
  that doesn't yield a filesystem path (virtual folders, search results).
- **Watch**: `RegNotifyChangeKeyValue` on `ComDlg32` (recursive, REG_NOTIFY_CHANGE_LAST_SET),
  re-armed after each event, on a dedicated thread. Debounce 500 ms (dialogs write
  several values per save).
- Timestamps: registry values carry none. On change event, diff current MRU head
  against previous snapshot; newly-seen or newly-promoted entries get `now()` as
  lastUsedUtc. Persist snapshot in `%LOCALAPPDATA%\LastFolderStanding\mru_snapshot.json`
  so restarts don't re-stamp everything.

### Source B: shell:recent (secondary, catches SHAddToRecentDocs apps e.g. Office)

- `%APPDATA%\Microsoft\Windows\Recent\*.lnk`
- Watch with `ReadDirectoryChangesW` (FILE_NOTIFY_CHANGE_FILE_NAME | LAST_WRITE),
  same 500 ms debounce.
- Resolve .lnk via `IShellLink::Resolve` with `SLR_NO_UI | SLR_NOSEARCH | SLR_NOTRACK
  | SLR_NOLINKINFO` and a short timeout flag set — **never** hit the network or MSI.
  If the target is a folder → use it directly; if a file → parent folder.
- Timestamp = .lnk last-write time (this one is real, use it as-is).
- If the Recent folder doesn't exist / tracking is disabled: log once, run on Source A
  only. This must not be an error state.

### Source C: Explorer activity (added after v1 planning)

The dialog MRU only records Open/Save dialogs. Working in a folder *in Explorer* —
pasting a file, dragging something in, creating a file — leaves no trace there, so
those folders never showed up. Two documented APIs cover it without hooks:

- `IShellWindows` — which folders are open in Explorer right now, polled every 2 s
  (a window's location changes without any window-level event, so polling is needed)
- `SHChangeNotifyRegister` — `SHCNE_CREATE | MKDIR | RENAMEITEM | RENAMEFOLDER`,
  recursive from the desktop, on the watcher's own thread and message window

**Neither works alone.** Change notifications fire for every process on the machine:
measured on a live desktop, Dropbox's cache folder appeared within seconds of
starting, unprompted. So a change only counts when it lands in a folder that is open
in Explorer at that moment.

A folder qualifies when either:
- something happens in it while it is open → counts immediately, or
- a window sits on it for 5 s → counts as "worked in"

The dwell rule keeps folders you merely click through on the way elsewhere out of the
list. Results are persisted to `explorer_activity.json` (max 50 entries), because
unlike the registry sources there is nothing to re-read them from after a restart.

## Pipeline (on any debounced event)

1. Collect candidate (path, timestamp, source) tuples from both sources
2. Normalize paths (final backslash off, `GetLongPathNameW`, case-preserve but compare
   case-insensitive)
3. Drop: non-existent paths (single `GetFileAttributesW`, but skip the existence check
   for UNC/network paths — assume alive, don't block), the user's own excluded globs,
   and always-excluded system noise: `%TEMP%`, `AppData\Local\Temp`, `Recent` itself
4. Dedupe by path, keep newest timestamp
5. Sort desc by timestamp, truncate to `maxFolders`
6. Compare against current state.json content; if changed, write atomically
   (write temp file + `ReplaceFileW`)

## Settings

- Read `settings.json` (schema in docs/DEVELOPMENT.md) at start, watch for changes, re-run
  pipeline on change. Missing file = defaults (maxFolders 5, no excludes).
- Glob matching: implement simple `*`/`?` matcher (PathMatchSpecExW is fine),
  expand env vars first.

## Process model

- Single instance (named mutex `Local\LastFolderStanding.Monitor`)
- Tray icon: tooltip shows current top folder; menu = Open Settings / Pause / Quit
- Autostart via `HKCU\...\Run` (installer sets it; Settings can toggle later — NOT a
  v1 setting, hardcode on)
- `--console` mode: no tray, no state.json — print every pipeline result to stdout.
  This is the primary dev/test mode.

## Deliverables

- [x] CMake or .vcxproj for LFS.Monitor, builds via build.ps1
- [x] MRU reader: OpenSavePidlMRU + LastVisitedPidlMRU → (path, order) list
- [x] Snapshot-diff timestamping
- [x] Recent .lnk reader with safe resolve (SLGP_RAWPATH, see docs/MRU_FORMATS.md)
- [x] Watchers (registry + directory) with debounce
- [x] Pipeline + atomic state.json writer
- [x] settings.json reader/watcher + glob excludes
- [x] Tray icon + single instance + --console mode
- [ ] Manual test protocol: save from Notepad, Chrome download "save as", Office save;
      verify order, dedupe, exclude behavior in --console output

Added beyond the plan: `--dump` prints every raw source entry with its group and
timestamp before filtering. This is what verified the PIDL decoding against a
live system and is the first thing to reach for when the list looks wrong.
