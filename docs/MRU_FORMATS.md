# MRU source format notes

Reference material for PLAN_01. Verify everything against a live system — these
structures are undocumented and observed, not specified.

## OpenSavePidlMRU

`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ComDlg32\OpenSavePidlMRU`

- One subkey per file extension used in a dialog, plus `*` (catch-all, most useful)
- Values named `0`, `1`, `2`, ... : REG_BINARY, each a full ITEMIDLIST (PIDL) of the
  **file** that was opened/saved
- Value `MRUListEx`: REG_BINARY, sequence of little-endian DWORDs = value names in
  recency order, first entry = most recent, terminated by 0xFFFFFFFF
- To get the folder: parse PIDL → `ILRemoveLastID` → `SHGetPathFromIDListW`
- PIDLs may reference virtual locations (Libraries, search results, MTP devices):
  `SHGetPathFromIDListW` fails → skip entry
- Practical scope: reading the `*` subkey alone covers nearly everything; per-extension
  subkeys duplicate it. Start with `*` + iterate others only if coverage gaps show up.

## LastVisitedPidlMRU

`HKCU\...\ComDlg32\LastVisitedPidlMRU`

- Values `0..n`: REG_BINARY = UTF-16LE executable name (null-terminated) immediately
  followed by a PIDL of the **folder** last used by that executable
- Same MRUListEx ordering
- This gives folder-per-application; OpenSavePidlMRU gives file-per-dialog-use.
  Both feed the same pipeline; dedupe handles overlap.

## Recent folder

`%APPDATA%\Microsoft\Windows\Recent`

- Flat folder of .lnk files written by SHAddToRecentDocs (Explorer itself, Office, many
  apps). Also contains AutomaticDestinations/CustomDestinations (jumplists) — ignore
  those subfolders, top-level .lnk only.
- .lnk resolve flags: SLR_NO_UI | SLR_NOSEARCH | SLR_NOTRACK | SLR_NOLINKINFO,
  timeout high-word trick (MAKELONG(flags, timeout_ms)) to cap resolve time
- Empty/absent when the user disabled "Show recently opened items..." — run without it.

## Implementation notes (verified on Windows 11 26200, 2026-08-29)

Two things turned out differently from the assumptions above:

**Registry key write times are usable timestamps.** `RegQueryInfoKeyW` returns a
last-write time per subkey, and on a real system those differ meaningfully
(`*` = minutes ago, `ai` = three months ago, `bmp` = three weeks ago). The head
entry of a subkey's MRUListEx is by definition what caused that write, so it gets
a real timestamp instead of a synthetic one. Lower-ranked entries still need the
snapshot, because any write to the subkey updates the key time without those
entries having been used. Rank N gets `keyTime - N seconds` on first sight only,
which is enough to order a cold start sensibly.

**No `IShellLink::Resolve`.** The plan called for Resolve with
`SLR_NO_UI | SLR_NOSEARCH | SLR_NOTRACK | SLR_NOLINKINFO`. In practice
`IShellLink::GetPath(..., SLGP_RAWPATH)` is strictly better here: it reads the
stored path without any chance of touching the network, MSI or the link tracking
service, and a stale path is dropped by the pipeline's existence check anyway.
Verified with a .lnk pointing at `\\nonexistent-host\share\...`: returns
immediately, no stall.

**Reading all extension subkeys is cheap.** 605 raw entries across ~90 subkeys
parse in well under the debounce window, so there is no reason to special-case
the `*` subkey.

## Gotchas observed in the wild

- Dialogs write MRU on OK, not on navigation — "last folder" means last **committed**
  open/save, which is exactly the wanted semantic
- Some apps write several extensions' subkeys in one save → debounce is mandatory
- Network-share PIDLs can take seconds to resolve if the host is gone — never resolve
  PIDLs on the UI/shell thread, never verify UNC existence
- Registry notifications fire for the whole ComDlg32 tree; re-read incrementally
  (compare MRUListEx heads) instead of full re-parse if it ever gets slow (it won't
  for <100 entries — don't optimize prematurely)
