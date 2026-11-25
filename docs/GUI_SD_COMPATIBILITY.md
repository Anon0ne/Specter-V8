# GUI SD file-manager compatibility

The Pro/Workspace GUI uses the `sd` protocol (`@SD:OK` / `@SD:ERR` results),
not the legacy human-readable `ls` command. The combined Specter firmware was
missing this handler. This change restores `SDFileManager.h/.cpp` from the
previously used local `ESP32Marauder-sd-file-manager` source tree (MIT, upstream
copyright retained), and routes `sd` before ordinary CLI dispatch.

Implemented operations: ls, tree, stat, df, cat, crc, mkdir, rm, mv, cp, get,
put. Transfers use base64 chunks and CRC32. Listing includes modification epochs.
Upload line length is bounded, and parent-path traversal is rejected.

Additional GUI access points expose existing functionality:

- `team status|blue|red`: explicit persisted team-mode selection.
- `guardian start|stop|status`: passive Guardian scanning/state.

These do not bypass the existing offensive-mode or BT-arm gates.

Hardware validation (2026-11-02): V8/C5 compile 2,203,426 bytes (65%), globals
101,524 bytes (30%); flashed with verified hashes. The actual GUI client listed
78 root entries and 549 Rian templates, downloaded/CRC-checked a nested
18,013-byte HTML file, and received template-selection acknowledgment without
starting a portal. A uniquely named temporary directory verified upload,
download, modified dates, copy, move, stat, disk usage and removal. The Pro GUI's
threaded browser independently displayed all 549 templates. Serial closed.

Companion GUI work: `specter/pro-integration` in `Anon0ne/marauder-v8-gui`.
