# PLAN 02 — LFS.ShellExtension

COM in-proc namespace extension that pins "Last Folder Standing" into the navigation
pane of Explorer and all common file dialogs, showing the folders from state.json as
clickable child nodes.

Prereq: PLAN_01 done — state.json exists and updates live.

## Approach

Model: **shortcut-style namespace extension** (like OneDrive/Dropbox nav-pane entries).
We do NOT implement a full custom IShellFolder view. The node's children are the target
folders themselves; clicking one navigates the view to the real filesystem folder.

### Registration (per-user, HKCU only — no admin)

- `HKCU\Software\Classes\CLSID\{our-clsid}`
  - InprocServer32 → DLL path, ThreadingModel Apartment
  - `System.IsPinnedToNameSpaceTree` = 1
  - `SortOrderIndex` → position in pane (experiment: place below cloud entries,
    above "This PC" — value around 0x42)
  - `ShellFolder\Attributes` = 0xF080004D-style folder attributes (has subfolders,
    is folder, browsable) — exact value determined during implementation
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace\{our-clsid}`
- `HKCU\...\Explorer\HideDesktopIcons\NewStartPanel\{our-clsid}` = 1 (keep it off the desktop)

### Implementation core

- `IShellFolder2` / `IPersistFolder2` for the root node
  - `EnumObjects`: read state.json → emit one simple child PIDL per folder
    (child PIDL = our own struct: index + stored target path)
  - `GetDisplayNameOf`: folder leaf name; `GetAttributesOf`: SFGAO_FOLDER | SFGAO_LINK
  - `BindToObject` on a child: bind to the REAL folder's IShellFolder
    (`SHBindToObject` on the target path) so navigation shows actual content
  - `GetUIObjectOf`: IExtractIconW → return the target folder's real icon
    (`SHGetFileInfoW` with icon location), root node gets our app icon (`app.ico`)
- Live refresh: watch `%LOCALAPPDATA%\LastFolderStanding` via
  `SHChangeNotifyRegister`-compatible mechanism — simplest robust route:
  monitor calls `SHChangeNotify(SHCNE_UPDATEDIR, ...)` on our PIDL root after each
  state.json write (add that one call to PLAN_01's writer — it may load nothing but
  shell32, keep it trivial), extension re-enumerates on demand.
- All state.json reads: tiny JSON parser (no dependency pulling in exceptions across
  COM boundaries — use a single-header parser, wrap in try/catch, on ANY failure render
  empty list, never propagate).

## Stability rules (this DLL lives inside explorer.exe)

- No CRT static state that breaks on multiple attach; DllMain does nothing but store
  hInstance
- Refcount everything correctly; run with Application Verifier during dev
- Never block: no network, no dialogs, no locks held across shell calls
- If state.json is missing/corrupt → node shows no children, that's it

## Dev/test loop

1. `regsvr32 LFS.ShellExtension.dll` (per-user works from non-elevated cmd with
   HKCU registration in DllRegisterServer)
2. Restart Explorer; check node appears in nav pane
3. Open Notepad → Ctrl+O: node must appear in the dialog's pane too
4. Click child → navigates in same window/dialog ✔
5. Save something from another app → child list reorders live
6. Crash drill: corrupt state.json manually, confirm Explorer survives
7. `regsvr32 /u` cleanly removes everything

## Deliverables

- [x] Project + DllRegisterServer/DllUnregisterServer (HKCU, no admin)
- [x] Root folder object pinned into nav pane at target position (SortOrderIndex 0x42)
- [x] Child enumeration from state.json
- [x] BindToObject → real folder navigation (same window)
- [x] Icons (root icon from the DLL's own resource, children real folder icons)
- [x] Live refresh via SHChangeNotify from monitor (SHCNE_UPDATEDIR on our PIDL)
- [ ] Verified in: Explorer, Notepad dialog, Chrome save dialog, Cubase save dialog
- [x] Crash drill passed — malformed, undersized, unterminated and foreign PIDLs are
      all rejected; verified by `shellext_probe`
- [ ] Application Verifier run

## What made this testable

`src/tools/shellext_probe` loads the DLL directly and drives the whole interface
the way the shell does, without registering anything and without involving
Explorer. It also feeds in deliberately broken PIDLs. Run it before every
registration — it turns "hope Explorer survives" into a check that takes a second.

`shellext_probe --registered` goes one step further and verifies the node really
appears in the desktop's `SHCONTF_NAVIGATION_ENUM`, which is the enumeration the
navigation pane itself performs.

## Implementation notes

The trick that makes clicking a child navigate to the real folder: a child reports
`SFGAO_FILESYSTEM` and returns the actual target path from `GetDisplayNameOf` with
`SHGDN_FORPARSING`. The shell then treats the item as that folder. `BindToObject`
additionally hands out the real `IShellFolder` via `SHParseDisplayName` +
`SHBindToObject`.

`CompareIDs` orders by the item's index in state.json, not by name, so the newest
folder stays on top instead of the shell sorting the list alphabetically.

`GetUIObjectOf` serves `IExtractIconW` and nothing else. Context menus, drag/drop
and property sheets are each another way to crash the host process, and none of
them is needed to click a folder.
