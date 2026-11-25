# BadBLE keyboard and DuckyScript

BadBLE is under **Bluetooth → Specter BLE → BadBLE Keyboard** on Marauder V8 / ESP32-C5.
It advertises **Specter Keyboard**, uses the existing Red Team and BT+ arm gates,
and accepts one host at a time. The HID transport is the hello-test implementation
already verified by the user, including Android. The DuckyScript parser, key tables
and press/release/gap engine are adapted from the supplied `bt_badble.cpp`.

## Touchscreen

1. Enable Red Team and arm BT+ using the existing menus.
2. Open BadBLE Keyboard and pair **Specter Keyboard** on your own test host.
3. Wait for **Ready** and open an empty text editor on the host.
4. Tap **Scripts**, then select a filename. The file is loaded and validated;
   selection does not execute it. The screen shows filename, step/character counts and errors.
5. Tap **Run**. There is a 1.5-second lead-in before script execution.
6. **Stop** cancels the script, attempts an all-keys-up report and disconnects the keyboard.
   **Back** from the script picker returns to status without running anything.
   **Back** from status stops the module, releases the BLE host and restores the menu.

Run is disabled until the encrypted host connection has subscribed to keyboard
reports. Scripts cannot be changed during a run. Reconnection never resumes or
starts a script. A host disconnect/unsubscribe cancels the run, even if the host
reconnects between main-loop updates. A failed report notification aborts the run.
Only one keyboard/other BLE module runs at a time.

## SD scripts

Put `.txt`, `.duck` or `.ducky` files directly in:

```text
/btloot/payloads/
```

Use ASCII filenames, at most 63 characters, with letters, digits, spaces, periods,
underscores or hyphens. Paths and subdirectories are not accepted. The picker is
paged and also includes a built-in **demo-hello** when no SD card is available.
The selected script is compiled into a bounded snapshot; editing the SD file later
requires selecting it again to load those edits.

`bb install-demo` creates `/btloot/payloads/hello-specter.txt` if absent. It never
overwrites an existing file. The same example is in
[`examples/badble/hello-specter.txt`](../examples/badble/hello-specter.txt).
It types `Hello from Specter SD...` without Enter or application shortcuts.
The built-in demo types `Hello from Specter` without Enter.

The GUI SD browser can upload scripts to this directory while radio jobs are
stopped. Dedicated BadBLE GUI forms are a separate follow-up; its raw console
can send the serial commands below.

## Supported DuckyScript subset

| Syntax | Behavior / limits |
| --- | --- |
| `REM text` | Comment. Commands are case-insensitive. |
| `STRING text` | Type US-layout ASCII text, preserving trailing spaces. |
| `STRINGLN text` | Type text followed by Enter. |
| `DELAY ms` | Wait 0..60000 ms without blocking touch processing. |
| `DEFAULTDELAY ms` / `DEFAULT_DELAY ms` | Add 0..5000 ms after each following keypress; does not alter explicit DELAY. |
| `REPEAT n` | Repeat the preceding executable command 1..1024 additional times; consecutive repeats retain that command's anchor. |
| `CTRL`, `SHIFT`, `ALT`, `GUI` | Modifier key alone, or combined with one ordinary key. CONTROL, WINDOWS, CMD and META aliases are supported. |
| Named keys | ENTER/RETURN, ESC/ESCAPE, BACKSPACE/BS, TAB, SPACE, CAPSLOCK, DELETE/DEL, INSERT, HOME, END, PAGEUP, PAGEDOWN, arrows, PRINTSCREEN/PRTSC, SCROLLLOCK, PAUSE, NUMLOCK, MENU/APP and F1..F12. |

Examples of key syntax: `SHIFT TAB`, `CTRL-SHIFT ESC`. A combo supports at most
one non-modifier key. This is a subset, not DuckyScript 3.0: no variables,
conditionals, loops other than REPEAT, Unicode input or keyboard-layout packs.
Literal backslash-n in a STRING is literal text, not an escape sequence; use
STRINGLN or ENTER for a newline.

Limits: **32 KiB source and 1024 compiled steps**. Unknown commands, unsupported
characters, malformed numbers, overflow, NUL bytes and step-table exhaustion
reject the entire script before typing. An invalid selection clears the previous
runnable script. Long scripts are not silently truncated. Press/release timing
uses the already-tested 35 ms hold; host keyboard layout can affect typed symbols.

## Serial reference

| Command | Purpose |
| --- | --- |
| `bb serve` | Advertise Specter Keyboard; no automatic typing. |
| `bb status` | Connection/readiness, running state, selection and notice. |
| `bb list` | List built-in and supported SD files. |
| `bb validate "hello-specter.txt"` | Load and compile without starting radio or typing. |
| `bb run "hello-specter.txt"` | Select/compile this SD file and explicitly run on a ready host. |
| `bb run` | Run the currently compiled selection. |
| `bb hello` | Explicitly run the built-in hello test. |
| `bb type text` | Manually compile/run a STRING on a ready host. |
| `bb key SHIFT TAB` | Manually compile/run a key combination. |
| `bb stop` / `stopble` | Cancel and disconnect; works without rearming. |
| `bb install-demo` | Create the harmless SD example if absent. |
| `bb script` | Print the grammar and limits. |

Without an extension, a serial filename defaults to `.txt`. Quote names with spaces.
The supplied module's queued reconnect execution is deliberately not wired into
this manual-run flow; `bb queue` is unavailable. No Airoha or `wt_*` WiFi modules
were imported as part of this keyboard integration.

## Event logs and validation

Each run attempts a metadata JSONL log under `/btloot/badble/`, or the active
Specter Session's indexed `bluetooth/` directory. Events are `run_start`,
`run_done` and `run_abort`, with script name, uptime, step and character count.
Typed text is not copied into these event records. Missing log storage is reported
on serial; logs use the existing shared BLE writer, not a new integrity guarantee.

Host tests exercise production parsing, whitespace, CRLF, repeats, key reports,
limits, malformed input, timing/rollover, notification failures, stop and disconnect.
Touchscreen tests cover paging, selection without running, readiness, Stop/Back,
and BLE release. Manual hello typing was user-confirmed on Android; SD script
interoperability still needs the same owned-host acceptance test.

### V8 checks — 2026-11-02

The ESP32-C5 build uses 2,240,996 bytes (67%) of program storage and 114,140 bytes
(34%) of global RAM. The image was flashed with upload hash verification.
On-device checks passed for SD demo installation without overwrite, built-in/SD
discovery, CRC upload/download of a filename with spaces, quoted filename loading,
CRLF/repeat parsing, refusal to run without a ready host, whole-script rejection
and selection clearing, path traversal rejection and the retained built-in hello.
Only the test's temporary files were removed; `hello-specter.txt` remains on SD.
The test did not start the keyboard or type into a host. Serial was closed afterward.

### Restart and Android report-delivery correction — 2026-11-02

A repeated keyboard start could reuse NimBLE server objects marked started after
host shutdown had discarded their underlying GATT database. Server teardown now
first stops the host, then clears native objects and invalidates the HID wrapper
and scanner cache together. Scanner-only teardown can retain its scanner objects.
Persistent pairing bonds are not erased. Keyboard startup checks the real native
GATT report registration and fails explicitly if the report is missing.

Each press and release now sends a copied eight-byte notification directly to the
paired connection, rather than relying on deferred characteristic-value updates.
Send failures propagate to the script runner. The keyboard screen distinguishes
waiting for encryption from waiting for host reports and shows Queued / TX / err
counters while connected; `bb status` also reports registration, connection,
encryption, notification subscription and protocol mode. TX events are transport
status, not an acknowledgement that Android inserted text into an application.
Error -1 means queuing failed; other nonzero errors are NimBLE transmit status.

The built-in `demo-hello` adds 100 ms default delay per character for visible
acceptance testing. Press hold remains 35 ms; no Enter or shortcuts were added.
The user reconnected Specter Keyboard, focused an Android note and confirmed
that the hello text types again with this build.

Validation: production report snapshot/routing/failure tests, teardown ordering
and failed-stop handling, script compiler/runner, modal/inspector and GATT address
regressions passed. On the V8, three keyboard start/stop cycles retained valid
native HID report registration; a BLE scan between starts completed without a
crash. The automated restart check did not start a typing script. Build uses
2,243,382 bytes program storage (67%) and 114,172 bytes globals (34%); upload hash
verified. Serial was closed before the user's Android typing confirmation.
