# QtProbe tests

Run `make test` at the repository root, or run `make -C qtprobe test-core`,
`test-ui`, or `test-ioc` separately. Tests build in the selected Qt object tree.
On Windows, `build-windows.bat test 4` initializes the compiler and runtime
environment and runs all three suites. Text logs are retained as `test-core.txt`,
`test-ui.txt` and `test-ioc.txt` in the object directory. The QtStripTool launch
test builds a native recorder helper, so it needs neither a Unix script nor
an installed QtStripTool. The two X11 drag interoperability tests are skipped
by the offscreen suite on Windows, as on other offscreen platforms.
Qt Test is required. The IOC test also requires EPICS Base's `softIocPVA` (or
`softIoc` for CA-only builds); it creates its own records and sets explicit
loopback-only CA/PVA address lists and isolated server ports. It never uses site
PV names. It verifies CA/PVA metadata, values, writes, errors, arrays, read-only
behavior, reconnect and the headless JSON command, the record explorer, plus
precision display, alarm limits, CA/PVA long strings (`DESC$` and a CHAR
waveform), record processing, and an inspector's write → Undo → Process round
trip with its log. UI tests use Qt's offscreen platform, with temporary
settings. Missing IOC prerequisites fail.

Artifacts, including IOC output and a UI screenshot, are stored under the Qt
object directory's `test-artifacts/`. IOC processes are terminated by test
cleanup. Tests do not install packages, change EPICS configuration or alter
production channels.

The offscreen UI suite also checks Help menu/F1 integration, full-guide search
and wrapping, topic navigation and window reuse, the offline manual and its
site override, and About credits, animation lifetime and Open help behavior.
Manual links use a Qt URL handler in tests, so no external browser is launched.
Help and About screenshots are saved beside the inspector screenshot.

Workspace coverage includes the welcome view, tab and Next/Previous PV
navigation, table filtering and closing, exact Readback staging, write
availability while paused, section toggles shared across PVs, window fitting
(growing, shrinking, the user-height floor and the empty reset) appearance
persistence, and dark-theme popups and disabled menu text (`menu-dark.png`).
`workspace-empty.png`, `workspace-one.png`, `workspace-sections.png`,
`workspace-light.png`, `workspace-dark.png`, `workspace-write.png` and
`workspace-compact.png` use synthetic UI fixtures with acquisition disabled.
They never contact an IOC.

Table tests cover natural, value and state sorting, quick filters and their
counts, click/Ctrl/Shift selection, text/CSV/name copies, keyboard navigation
and the age timer stopping when hidden. Other tests cover the range gauge and
Trend limit lines, write logging and Clear, confirmation (answered by the test),
pasted name lists, zoom of fonts and rows, and Open in QtStripTool through a
stand-in script given by `QTPROBE_STRIPTOOL`. Core tests check that history
stays within its count and the shared memory limit (including the split between
PVs and long values shortened to 1000 characters), and that write-log values are
limited. UI tests check that hidden inspectors and closed sections release plot
and table copies, that a paused inspector keeps them, and that old QtStripTool
configurations are pruned. Text sizing is checked with hinted and unhinted fonts
from 6 to 16 pt (remote X displays often render unhinted, with fractional
widths): plot axis labels, elided labels and the History timestamp column must
show their text in full. Enum trends are checked for state names, step drawing
in the inspector and the table, no limit lines or gauge, and time-in-state
statistics (core test with fixed receipt times, including a disconnect).
Numeric arrays and text waveforms hide Trend, add no numeric history samples
and leave the table sparkline empty, while scalars keep Trend.
Structure browser tests cover in-place updates (rows, expansion and
selection kept), default expansion of NT metadata, change marks on values and
on collapsed parents, the search, row and whole-structure copies, the
"more elements" row, CA PVs without Structure and release of hidden rows (UI,
synthetic trees). Core tests check its text and JSON forms and the dotted Info
paths derived from it. The IOC test reads an NTScalar and a QSRV group (an
NTEnum member and a plain array) over PVA. Core
tests cover record link parsing (PV links, constants, hardware and JSON links),
the record-type field lists and every diagnosis rule. The IOC test reads a
periodic calc with CP and forward links and a disabled record over CA and PVA,
and follows a link by clicking it; a UI test checks that following a link opens
or selects the target inspector. Core tests also cover precision display,
long-string decoding and text writes, alarm-limit detection, the bounded write
log and its CSV, and the QtStripTool configuration.

The multi-PV tests verify simultaneous table rows, exact 64-bit and array
readbacks, alarms/disconnects, shared history, independent inspector pause,
column alignment and narrow-window columns, filtering, closing, batch preflight
and read-only behavior. Text-drop tests cover whitespace-separated QtEDM
payloads, provider selection, duplicates, editors/tabs, copy actions, source
selection release, cancelled gestures, and rejection of malformed/file/move-only
drops. Table previews are saved as `workspace-all-pvs-{light,dark,compact}.png`.

For a real QtEDM middle-button gesture, with QtEDM already built, run:

```sh
make -C qtprobe test-qtedm-drop
```

This optional Linux test builds a small X11/XTest mouse driver and launches the
unmodified sibling `medm/bin/Linux-x86_64/qtedm`. The target starts its own
Xvfb display with `xvfb-run` (override with `XVFB_RUN`), so the synthetic
pointer input never reaches your desktop. Override
`QTEDM_TEST_PROGRAM=/path/to/qtedm` when needed. It checks drops on the welcome
page, name editor, inspector value, staged value editor, a table row and the
tab strip, including
duplicates and preservation of the staged value without writes. The source
inherits loopback-only test discovery settings; no production channels are used.
This case is skipped by the normal offscreen suite.

Legacy MEDM drops are checked with a small Motif program that starts a drag
the way MEDM does (`XmDragStart`, COMPOUND_TEXT, copy) and drops on the window.
It needs Motif, Xt and XTest development files:

```sh
make -C qtprobe test-motif
```

It covers a new PV, a duplicate, the provider selector and a refused name.
Driving the real MEDM binary with synthetic input does not work under Xvfb:
MEDM waits in its Motif clipboard copy before the drag starts.

CPU checks are opt-in because timings depend on the host and Qt platform:

```sh
make -C qtprobe benchmark-cpu
cd qtprobe
python3 tests/benchmark_ioc_cpu.py O.Linux-x86_64-qt5/benchmark_cpu
```

The first command measures an empty window, 100 static synthetic PVs, 100 PVs
updating at 10 Hz with 10,000 retained events each, and one inspector with a
full history, with no section and with Trend open. The helper reports JSON
containing process CPU time as a percentage of one core, elapsed time, and
delivered update counts. Its options also select History or a hidden window. The
Python runner needs Python 3 and `softIocPVA`; it owns a loopback-only
IOC and foreground CA repeater and checks 1/100 static CA PVs, 100 active CA
PVs, and 100 static/active PVA PVs. Set `EPICS_TEST_BIN` for another EPICS
installation. No PV writes are issued by the benchmark client. Correctness tests
additionally cover deferred rendering, idle timers, scrolling into stale table
rows, incremental history rows, peak/gap preservation in dense plots, and shared
CA/PVA delivery after peer removal.

The benchmarks also support native Windows builds from an initialized MSVC
shell (use the same Make variables and runtime paths as the normal build).
Select `O.Windows-x86_64-qt6/benchmark_cpu.exe` for the Python runner and set
`EPICS_TEST_BIN` to the EPICS `bin/windows-x64` directory. On Windows the helper
uses process kernel/user CPU time and peak working set; on Unix it uses process
CPU time and peak RSS. Both report CPU as a percentage of one core.
