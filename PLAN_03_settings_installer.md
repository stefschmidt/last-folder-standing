# PLAN 03 — LFS.Settings + Installer

Prereq: PLAN_01 + PLAN_02 done and working end to end.

## Settings UI (C# / WinUI 3, unpackaged)

One window, two controls, Save button. Nothing else.

- **Number of folders**: slider/numberbox 1–15, default 5
- **Excluded folders**: editable list (add via folder picker OR free-text glob),
  remove per row. Show resolved example ("%TEMP% → C:\Users\stef\AppData\Local\Temp")
- Writes settings.json (schema in docs/DEVELOPMENT.md) atomically; monitor picks it up via its
  file watcher — no IPC needed
- Launched from tray menu ("Open Settings") and Start menu shortcut
- Single instance; if already open, bring to front

## Installer (Inno Setup)

- Per-user install to `%LOCALAPPDATA%\Programs\LastFolderStanding` — no admin needed
  since all registration is HKCU
- Steps: copy binaries → regsvr32-equivalent (call DllRegisterServer via
  `regsvr32 /s`) → add HKCU Run entry for monitor → start monitor → restart Explorer?
  NO — don't kill the user's Explorer. Instead: `SHChangeNotify` + note in final page
  ("entry appears after next Explorer restart / immediately in new dialogs")
- Uninstall: quit monitor (mutex + WM_CLOSE to tray window), unregister DLL, remove
  Run entry, remove %LOCALAPPDATA% data (ask: keep settings?)
- Version scheme: SemVer, stamped into all binaries

## GitHub release setup

- `build.ps1 -Release` produces signed-ready artifacts + installer exe
- GitHub Actions: windows-latest, build + upload installer on tag push
- README badges, screenshots (Explorer pane + Notepad dialog side by side)
- CONTRIBUTING.md pointing at the PLAN structure
- Mention in README: related PowerToys feature request microsoft/PowerToys#37681

## Deliverables

- [x] Settings window (2 settings), atomic write, launch from tray + Start menu
- [x] Inno Setup script, per-user, no-admin install + clean uninstall
- [x] build.ps1 release mode (`-Config Release -Installer`)
- [x] GitHub Actions release workflow (plus a CI workflow on push/PR)
- [x] README polish
- [ ] Screenshots (needs a real Explorer session, see the manual test protocol)

## Deviations from the plan

**Win32 instead of WinUI 3.** WinUI 3 needs the Windows App Runtime on the target
machine: either a ~60 MB self-contained payload or a machine-wide runtime install
that prompts for admin. For a window with two settings that is the wrong trade,
and it breaks the per-user no-admin promise this very plan makes. The window is a
resource dialog in C++, ~110 KB, no runtime, no dependencies.

**Uninstall leftovers.** `restartreplace` / `uninsrestartdelete` write to
`PendingFileRenameOperations` under HKLM and therefore need admin — which a
per-user installer does not have, so they silently do nothing. When Explorer still
has the extension DLL mapped at uninstall time, the installer instead writes an
HKCU `RunOnce` entry that removes the folder at the next logon. It only does this
when the DLL is actually still there, so the usual uninstall leaves nothing behind.

**Installer testing.** `Start-Process -Wait` hangs on Inno setups because they
relaunch themselves; use `Start-Process -PassThru` plus `Wait-Process -Timeout`.
A silent uninstall never asks about the data directory — it keeps it.
