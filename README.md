# Last Folder Standing

**The folders you just used — always one click away, in Explorer *and* in every Open/Save dialog.**

You save a render in your DAW, switch to the browser, hit "Upload" — and the file dialog dumps you in `Downloads`. Again. Last Folder Standing pins a live list of your most recently used folders into the Explorer navigation pane. Because Open/Save dialogs share that same pane, the list is right there in every standard Windows file dialog, in every application.

## What it does

- Adds a **"Last Folder Standing"** node to the left navigation pane (Explorer + all common file dialogs)
- Shows the **N most recently used folders**, newest on top, updated live
- "Used" = picked in any standard Open/Save dialog, in any program — **or** worked in
  through Explorer itself: pasting a file, dragging something in, or just keeping the
  folder open for a few seconds
- Click an entry → navigate there in the same window/dialog

## What makes it different

- **Not a popup, not a hotkey tool** (→ Listary, Quick Access Popup): the list lives *inside* the navigation tree, zero extra UI
- **Not the Windows "Recent Folders" registry tweak**: works even when Windows recent-item tracking is disabled (we read the `OpenSavePidlMRU` dialog history, which Windows keeps writing regardless), and adds configuration the tweak can't offer
- **Configurable**: number of folders + exclude list. That's all. No feature creep.

## Configuration

Two settings. Deliberately nothing else.

| Setting | Default | Range |
|---|---|---|
| `maxFolders` | 5 | 1–15 |
| `excludePaths` | `[]` | glob patterns, e.g. `%TEMP%`, `*\node_modules\*` |

Excluded folders are skipped; the next most recent folder takes their slot.

## How it works

```
┌─────────────────────┐     watches      ┌──────────────────────────────┐
│ LFS.Monitor         │ ───────────────► │ HKCU ...\ComDlg32\           │
│ (background, tray)  │                  │   OpenSavePidlMRU            │
│                     │                  │   LastVisitedPidlMRU         │
│  merge + dedupe     │ ───────────────► │ %APPDATA%\...\Recent (.lnk)  │
│  + apply excludes   │ ───────────────► │ Explorer: open windows +     │
│                     │                  │   shell change notifications │
└────────┬────────────┘                  └──────────────────────────────┘
         │ writes %LOCALAPPDATA%\LastFolderStanding\state.json
         ▼
┌─────────────────────┐
│ LFS.ShellExtension  │  COM namespace extension, pinned to nav pane.
│ (in-proc, dumb)     │  Reads state.json, renders child nodes, navigates.
└─────────────────────┘
```

The shell extension contains **no logic** — it only renders `state.json`. All parsing, watching and filtering lives in the monitor process. If the monitor dies, Explorer keeps running with the last known list.

## Known limitations

- Programs with fully custom file dialogs that bypass the Windows common dialog **and** don't call `SHAddToRecentDocs` are invisible to us (rare; mostly cross-platform toolkits that draw their own dialogs)
- Legacy XP-style dialogs have no navigation pane, so there is nothing to render into
- Explorer activity only counts in folders you actually have open. Dropping a file into a
  folder you never opened — via "Send to", or onto a shortcut — doesn't register. That
  restriction is on purpose: without it, every background process writing files would
  end up in your list

## Installing

Grab the installer from the releases page and run it. It installs per user into
`%LOCALAPPDATA%\Programs\LastFolderStanding` and never asks for admin rights,
because everything it registers lives under `HKEY_CURRENT_USER`.

The entry shows up in any application you start from then on. Applications that
were already running keep showing the old state — Windows reads the registration
once per process, so those need a restart. Explorer is no exception; the installer
deliberately does not kill your desktop session to force it.

Uninstall through Settings → Apps as usual. It asks whether to keep your settings
and folder list.

## Building

Needs Visual Studio 2022 with the x64 and x86 C++ toolsets and, for the installer,
Inno Setup 6.

```powershell
.\build.ps1                              # Debug
.\build.ps1 -Config Release
.\build.ps1 -Config Release -Installer   # also builds the setup .exe
```

Four binaries come out of it:

| | |
|---|---|
| `LFS.Monitor.exe` | background process, tray icon, writes `state.json` |
| `LFS.ShellExtension.dll` | the navigation pane entry, reads `state.json` |
| `LFS.ShellExtension32.dll` | the same, for 32-bit applications |
| `LFS.Settings.exe` | the two settings |

The extension ships twice because a 32-bit application can only load a 32-bit
DLL — without the second one, the entry is missing from those file dialogs.

See `PLAN_01`–`PLAN_03` for the implementation plans and `docs/DEVELOPMENT.md` for
repo conventions and the test procedure.

## License

MIT
