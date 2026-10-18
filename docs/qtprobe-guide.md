# QtProbe guide

## Getting started

QtProbe 0.1.0 inspects EPICS process variables over Channel Access (CA) and pvAccess
(PVA). Enter a name and press **Add** or Enter. Plain names use the selected
provider (CA initially). `ca://` and `pva://` prefixes always override it.
Type or paste several names separated by spaces or new lines to add them all.
The window starts small and fits what it shows. With one PV it shows the name,
live value, units, alarm state, a write box and section toggles, much like the
original Probe. Adding a second PV shows a tab strip: **All** lists every PV in
a compact table, and each PV has its own tab. Reopening an existing address
selects that PV. Close a PV with its tab's ×, its row's ×, Ctrl+W, or a
context menu. The application remembers the last 30 names, the open sections,
and the window's position and width.

## Build and launch

Run `make qtprobe` from the repository root, then launch
`bin/Linux-x86_64/qtprobe PV pva://PV2` (substitute your OS and architecture).
Qt 5.15 and Qt 6 are supported. QtProbe uses Fusion by default; Qt's standard
`-style` option chooses another installed style.

## Command-line reference

Options:

| Option | Meaning |
| --- | --- |
| `--provider ca` or `--provider pva` | Default for unprefixed names |
| `-w seconds`, `--timeout seconds` | Connection/read deadline; default 5, maximum 3600 |
| `--read-only` | Disable writes throughout the process |
| `--max-elements count` | Retain at most this many array values; default 16384 |
| `--history count` | Retain at most this many monitor events per PV; default 10000 |
| `--history-memory MB` | Memory limit for the retained events of all PVs together; default 256, range 16–65536 |
| `--info` or `-info` | Print one metadata snapshot per PV and exit without a display |
| `--json` | JSON output with `--info`; values remain exact strings |
| `--session file.qtp` | Open a saved PV workspace |
| `-h`, `--help`, `--version` | Command-line help/version without a display |

Count limits must be between 1 and 1000000. Invalid arguments exit with status
2. `--info` returns 1 if any PV is unavailable or its data/metadata times out,
and 0 on success. GUI discovery continues after the deadline and recovers when
an IOC returns. PVA requires an EPICS Base built with pvAccess and pvData.
The `--info` report alone goes to standard output; EPICS library diagnostics go
to standard error. When scripts capture `--info` output, run a caRepeater
service or make sure the `caRepeater` on `PATH` is from EPICS 7.0.4 or later:
an older repeater started on demand keeps the caller's output open.

## Workspace and appearance

The window fits its content. It grows when a section opens or a second PV is
added, and shrinks again when they close. If you resize the window yourself,
QtProbe keeps at least that height until the workspace is empty; a maximized
window is left alone. Content taller than most of the screen scrolls.

**All** (Ctrl+1) shows one row per PV: status dot, name, value, units,
provider, alarm/connection state, age and a sparkline of recent numeric
updates. Age is the time since QtProbe received the PV's last value; it keeps
counting while a PV is quiet, so a stalled PV stands out. Narrow windows hide
the provider and trend columns. Hover a name for its address and description,
or a value for its exact text, IOC time and whether it is a last observation.

Click a row to select it; Ctrl+click adds or removes rows and Shift+click
selects a range. Double-click a row, press Enter, or click its tab to inspect
it. With the table focused, the arrow keys, Page Up/Down, Home and End move the
selection (Shift extends it), Ctrl+A selects every shown row, Ctrl+C copies the
selection and Esc clears it; Down in the filter field moves into the table.
Right-click for Inspect, Copy rows (tab-separated name, exact value, units,
state and IOC time), Copy as CSV, Copy PV names, Open in QtStripTool, Reconnect
and Close. A tab's menu offers Copy PV address, Open in QtStripTool, Reconnect
and Close PV. The row of the PV shown in the inspector has an accent bar.

Click the PV, Value, State or Age heading to sort: ascending, then descending,
then back to the order PVs were added. Names sort naturally (Row:2 before
Row:10). State puts disconnected and the most severe alarms first. Value, State
and Age re-sort at most once a second so rows do not jump with every update. The
filter matches names, providers and descriptions. **Alarms** and
**Disconnected** show counts and restrict the table to PVs in MINOR, MAJOR or
INVALID alarm, or to disconnected PVs; with both on, either kind is shown.
Adding a PV clears the filters. Ctrl+PgDown and Ctrl+PgUp step through All and
each PV; **Inspector** (Ctrl+2) returns to the selected PV. **All** stays pinned
at the left of the tab strip; PV tabs elide long names in the middle and scroll
beside it when there are many, so the table is one click away from any PV. While
the table is shown, the last inspected PV's tab is dimmed; click it to return to
that PV. The views share connections and history. Pausing an inspector's display
does not pause the table. Up to 100 distinct CA/PVA addresses can be open; the
same name with a different provider is a separate PV.

The inspector's first line shows a connection dot, the PV name with a copy
button, the alarm badge and the provider. Below are the description, the value
with units and display format, and one line with native type, element count,
access rights and IOC timestamp in UTC (hover it for the host). Connection
problems appear at the start of that line. Status is shown with text as well
as color.

Toggle buttons under the write box open sections below the readback: **Trend**
(recent numeric plot and statistics), **Adjust** (limits, slider, step and
dial), **Info** (searchable metadata), **Array** (array PVs only), **History**
(monitor events), **Record** (processing fields and links; see [Record
explorer](#record-explorer)) and **Structure** (PVA PVs only; see [Structure
browser](#structure-browser)). Any combination can be open. The choice applies to
every open PV and is remembered for later launches. Trend appears only for
scalars; arrays and text waveforms use Array instead. Adjust appears only for
writable numeric scalars. **Reconnect**, **Stop** and **Pause** are the icons at
the right of the same row.

Use **View → Appearance** to choose Follow system, Light or Dark. The selection
is remembered for future launches. **View → Zoom in** (Ctrl++ or Ctrl+=), **Zoom
out** (Ctrl+−) and **Actual size** (Ctrl+0) scale all text from 70% to 200% for
control-room screens; the zoom is remembered too. Closing the last PV returns to
the compact welcome view.

The export button (download icon) beside **Add** offers the same snapshot,
history and session actions as File. It is enabled when a PV is open. Invalid
PV addresses produce a message below the entry field without opening a dialog.
Snapshot and history exports apply to the selected PV; sessions include every
open PV. A read-only session shows a **Read only** badge beside the entry field.

**Tools → Open in QtStripTool** (Ctrl+T) plots the selected table rows, or the
PV in the inspector, in QtStripTool. QtProbe writes a small configuration with
each PV's name, units, display range and precision to its cache folder and
starts QtStripTool with it; only the newest 20 of these files are kept.
QtStripTool plots up to ten Channel Access PVs; PVA PVs and any beyond ten are
skipped with a note in the status bar. QtProbe looks for `QTPROBE_STRIPTOOL`,
then `qtstriptool` beside `qtprobe`, then on `PATH`, then in a sibling StripTool
build tree.

## Dragging PVs from QtEDM and MEDM

In a running QtEDM display, hold the **middle mouse button** on a PV, drag it
outside the display into QtProbe, and release. A widget with several PVs can
provide several names in the same gesture. Drop onto the welcome area, the
table, the tabs, or the inspector, including its editors and tables. Each
distinct address opens once; an existing PV is selected. External PV drops open
channels and never stage or send a value write.

QtEDM exports whitespace-separated plain text. Newline/tab-separated text from
other programs works too. Explicit `ca://` and `pva://` prefixes take
precedence; unprefixed names use QtProbe's provider selector when the drag
enters. File/URL drops, unsupported providers, malformed lists and drops over
100 names are rejected. A group that exceeds the workspace's 100-PV capacity
adds nothing. Legacy Motif MEDM works the same way on X11: middle-drag a PV out
of an MEDM display and drop it anywhere on the QtProbe window. QtProbe speaks
MEDM's Motif drop protocol directly (without linking Motif) alongside Qt's drag
and drop, so QtEDM and MEDM can be used side by side. MEDM reports a refused
drop, such as an invalid name or a full workspace, as a failed transfer, and the
provider selector applies to MEDM names as it does to QtEDM names. MEDM drops
need the X11 (`xcb`) backend; they are unavailable on Wayland, macOS and
Windows. On Linux the source and target must be on the same X display.

PV names can be dragged out of QtProbe as well: drag table rows (the whole
selection, or one row with the middle button), middle-drag a tab, or drag the
PV name at the top of the inspector. The drag carries plain names for CA and
`pva://` names for PVA, separated by spaces, which QtEDM, QtStripTool and
another QtProbe accept.

## Live values and controls

The inspector shows connection state, value, units, alarm severity, IOC
timestamp in UTC, type and element count. Like the original Probe, the Native
format shows floating-point values with the PV's precision (PREC or
`display.precision`): a value written as 24.989 with PREC 3 shows as 24.989,
not 24.989000000000001. Integers, strings and values without a precision are
shown exactly. Hover the value for the exact text; right-click it for **Copy
value** (exact) or **Copy displayed text**. History, exports, copies and the
write editor always use exact values. Fixed, Scientific, Hex and Binary format
numbers as before; **Text** decodes a byte array as text.

Numeric scalars with a display range show a gauge under the value: the range
LOPR to HOPR (or `display.limitLow`/`limitHigh`), with LOW–HIGH warning bands
and LOLO/HIHI alarm bands where those limits are set, and a marker colored by
the alarm state. A value outside the range shows an arrow at that end. Hover the
gauge for the limits, including drive limits. CA reports unset alarm limits as
zero, so a pair of zero limits is treated as unset. The **Trend** plot draws
warning and alarm limits as dashed lines when they fall within the plotted
range.

CA character waveforms and PVA byte arrays that hold text are shown as text,
as are CA long-string fields such as `rec.DESC$` (names ending in `$`). Text
is detected when the bytes before the terminating NUL are printable; binary
buffers keep the numeric view, and **Text** forces decoding. The write box
accepts plain text for long strings and replaces the whole string; CA writes
and writes to `$` fields add the terminating NUL, and CA writes must fit the
field. If an array switches between text and byte values after you enter a
value, Write refuses it until you enter the value again.

A disconnected value is retained as a last observation and shown in gray. A
connected PV with no read access is also shown in gray. Metadata is in
**Info**; its copy button copies the full summary. **Reconnect** (circular
arrow) opens a fresh connection; **Stop** (square) releases subscriptions and
channels. **Pause** (two bars) holds the inspector's display while acquisition
and bounded history continue, and shows a Paused badge. Unpause to catch up.
Write availability continues to follow the actual connection even while the
display is paused; the tabs and table also continue to show the latest state.

## Adjusting values

The write box under the readback accepts native scalar values. **Write** or
Enter in the box sends the edit. Enum selection and the **Adjust** section's
slider, dial and step buttons only stage the edit; press Write to send it.
**Readback** stages the exact current value; it is disabled for truncated
arrays so an incomplete readback cannot silently replace the full array. It is
also disabled for a string that fills the 39 bytes a plain string field is
read with (CA DBR_STRING, or QSRV), because a longer field such as `DESC` or a
link may have been cut short; open it with `$` (for example `rec.DESC$`) to
read and write the exact text.
CA exposes 64-bit integer fields as doubles, so readbacks at or above 2^53 in
magnitude may be rounded. **Readback** and **Undo** are disabled for these
ambiguous values, including arrays, unknown record types and fields whose
type metadata has not arrived. Identified scalar `VAL` fields of standard
`ai`, `ao`, `calc`, `calcout` and `sub` records remain available because they
are actual doubles. Use `pva://` for exact 64-bit integer readbacks and Undo.
Explicit integer text, including quoted integers in JSON arrays, still writes
exactly over CA.
Numeric range controls initially use the IOC display limits and can be changed
locally. Changing these limits does not modify the IOC. Controls are disabled
when disconnected or while a write is pending. The write box and Adjust are
hidden in a read-only session and for PVs the server reports as not writable;
they stay in place during a disconnect. The IOC remains authoritative for
access and accepted limits. Write results and errors appear on the line below
the box. Adjust has Min, Max and Step fields, a slider between − and + step
buttons, and **Dial** reveals the optional dial. CA writes and Process use a
put with completion callback (unlike the Motif Probe's plain put): the result
line reports completion after the record finishes processing, and no other
write to that PV is sent meanwhile. If completion takes longer than 10 s (a
long motor move or another asynchronous record), the result line says the
record is still processing and QtProbe keeps waiting while the PV stays
connected; the result is reported when the server sends it. **Reconnect** or
**Stop** abandons the wait, and a disconnect during the write leaves its outcome
unknown: inspect readback and reconnect before another write is enabled. Stop
fields such as a motor's `STOP` are separate PVs and can be written from their
own inspector.
A failed CA array put may have changed earlier elements before the IOC rejected
a later one. QtProbe requests fresh readback, reports the potentially partial
write, disables Undo and keeps further writes disabled until **Reconnect**.
CA array writes are not transactional; a failure does not imply that the whole
array stayed unchanged.
After a pvAccess string or array put, QtProbe checks readback for server size
limits. Its completion message warns if the server kept fewer elements or
shortened a string, including individual elements of a string array.

**Undo** writes back the exact value QtProbe last received before your last
successful write from this inspector; its tooltip shows that value. A monitor
deadband (`MDEL`, or a `dbnd` filter in the name) can hide small changes made
by others since then; the confirmation's current value is that same value. Undo is itself a
write, so pressing it again restores the value it replaced. Undo sends the
saved elements as they were received, so enum states and byte arrays are
restored exactly. It is unavailable after a write that replaced a truncated
array (see `--max-elements`), because the full earlier value is not known, and
after a write whose outcome is unknown, because the PV may hold either value.
Undo is also disabled if the PV's type, scalar/array shape, state list or CA
array capacity changes. A scalar readback cannot restore a replacement array.
An earlier CA value that may hold a rounded 64-bit integer cannot be restored
with Undo; the tooltip explains this limit.

**Process** (the circled triangle beside Reconnect) writes 1 to the record's
PROC field, as `caput rec.PROC 1` does. It appears for Channel Access PVs when
the PROC field is connected and writable, and never in a read-only session.

**Tools → Confirm before writing** asks before each write, Undo and Process,
showing the new and current values as they are sent (enum states with their
labels, arrays with their element counts); the setting is remembered. If the PV's
type, states or array format changes while the question is open, the write is
refused; check the PV and enter the value again. **Tools → Write
log** lists every write and Process requested in this session, newest first,
with the time, PV, value before, value written and the result, including
failures and refusals. **Export CSV…** saves the log; **Clear** empties it. The
log holds the latest 1000 entries, with each value limited to 1000 characters,
and is not saved between sessions.

Integer writes are range checked and preserve 64-bit precision. Channel Access
serves 64-bit integer fields as doubles, so CA writes of values beyond 2^53 are
sent as integer text that the IOC parses exactly or rejects as out of range; a
CA readback of such a field is rounded, so use `pva://` to read it exactly. Numeric
adjustments are disabled for 64-bit integers beyond the exact range of double;
use the native value editor for those values. Hexadecimal
integers can use `0x`. Floating-point writes must be finite. Enum writes accept
a label or a valid index; when a label reads as a number, typed text selects
that label. The state list and **Readback** stage text that selects the chosen
state. CA strings hold at most 39 encoded bytes. Text writes and string-array
elements reject embedded NUL characters; use numeric byte arrays for binary data. Array writes
use JSON: `[1, 2, 3]` or `["one", "two"]`. Quote integers above 2^53−1, for
example `["18446744073709551615"]`, to avoid JSON numeric precision loss.
Array writes replace the value array; empty array writes are not supported.
A pvAccess server may shorten a string or array to its field size without an
error (QSRV keeps 39 bytes of a string field and NELM elements), so QtProbe
reads PVA string and array writes back and the result line says when the
server kept less than was sent.
Arbitrary PVA structures/unions are inspection only unless they contain a
supported scalar/array `value` or NTEnum `value.index`.

## PV information

Info includes provider, remote address, read/write access, structure ID, native
type, element count/capacity, alarm status/severity/message, timestamps and enum
choices, as in `cavget -info`. CA adds CTRL metadata (precision, units, display,
control and alarm limits), and optional `DESC` and `RTYP` record fields. Missing
optional fields do not prevent inspection. PVA displays the server's structured
metadata, including `display`, `control` and `valueAlarm` when present; up to
512 flattened fields are shown and array previews contain up to 16 elements.
Field names and values are searchable. pvAccess does not report access rights
to clients, so a writable PVA value always shows write access and an enabled
write box; the server applies its access security when you write, and a denied
write is reported on the result line.

## Structure browser

**Structure** shows a PVA PV's whole pvAccess structure as a tree, the way
`pvget -v` prints it: each field's name, type (`double`, `string[]`, a
structure ID such as `epics:nt/NTScalar:1.0`) and live value. It is available
for every PVA PV, including QSRV groups and other PVs whose value is not a
plain scalar or array; CA PVs do not have it. Nested structures, unions (the
selected member), structure arrays and union arrays all open as branches.
Collapsed structures show a one-line summary: an enum's `(index) state`, an
alarm's severity and message, a time stamp in UTC, or the field's own `value`.

Normative-type metadata (`alarm`, `timeStamp`, `display`, `control`,
`valueAlarm`) starts collapsed; other structures start open. Expanding and
collapsing is remembered for fields in the current structure while the PV is
open, including when the section is hidden. Fields removed by a schema change
are forgotten so this state stays bounded; if they return, they use the defaults
above. Updates change values in place, so the tree does not jump or close as
data arrives. A value that changed in the latest update is shown in the accent
color; a collapsed field shows its name in the accent color when something
inside it changed.

The search box matches field paths, types and values. Matching fields are
shown with their contents and the structures that contain them; clearing the
search restores your expansion. **Expand** and **Collapse** act on the whole
tree, and the copy button copies it as indented text. Right-click a field for
**Copy value** (an array's retained elements in full), **Copy field path**
(such as `value.position` or `items[2].name`), **Copy row** (path, type and
value, tab-separated), **Copy field with contents**, and expand or collapse
all below it. Ctrl+C copies the selected rows.

The tree lists up to 100 elements of each array and up to 4000 fields in all;
a muted row says how many more were received. Use **Array** for a PV's full
value. The browser is read-only; writes go to the PV's `value` field through
the write box. `--info --json` includes the same tree as `structure`, with
`fields`, `length` and `omitted` keys.

## Record explorer

**Record** shows the PV's record fields and explains why it may not be
updating. It reads `record.FIELD` over the PV's own provider (CA, or PVA from
QSRV), so it works on any IOC without extra software. Fields are grouped:

- **Processing**: SCAN, PINI, PHAS, EVNT, PRIO, PACT and TPRO.
- **Disable**: DISA, DISV, DISS and the SDIS link.
- **Alarm**: UDF, UDFS, STAT, SEVR and AMSG (EPICS 7.0.6 and later).
- **Device** and **Access**: DTYP, TSE, SIMM, DISP and ASG.
- **Links**: FLNK, TSEL and the links of the record type, read from RTYP: INP
  or OUT/DOL/OMSL for input and output records, INPA–INPL for calc, calcout,
  sel and sub, INPA–INPU and OUTA–OUTU for aSub, LNK0–LNKF for fanout and seq,
  and so on. Unknown record types show INP, OUT and DOL when present.
- **Monitors**: MDEL and ADEL for analog and integer records.

IOCs post no monitors for PACT and UDF, or for DISA read through SDIS, so
these fields are read again every two seconds. While the PV or its fields are
disconnected, their last values stay grayed and are not diagnosed.
Fields the record or IOC version does not have are dropped after the
connection timeout. Links are read as `FIELD$`, so long links are complete.
Hover a field for its meaning. A link that names a PV is underlined; click it
to open that PV (or select it if already open) and inspect it. Constants,
hardware addresses (`@…`, `#…`) and empty links are not clickable; JSON `ca`
and `pva` links open their `pv`. Hover a link for its modifiers (PP, NPP, CA,
CP, CPP, MS, NMS).

Above the fields, the diagnosis lists what explains a quiet or wrong value:
disabled (DISA equals DISV, and whether SDIS sets it), PACT stuck at 1,
UDF = 1 (noting PINI = NO), the alarm status and severity with a plain-language
reason (for example LINK: a link could not be read), DISP = 1 rejecting writes,
and simulation mode. It explains the SCAN mechanism (Passive, I/O Intr, Event
or periodic). For a periodic record with no new value for more than three
periods, it notes that monitors post only when the value changes by more than
MDEL, so a steady value looks stalled. Problems are marked ⚠.

The field channels exist only while the section is open on the inspector
being shown; they close when you switch PVs or close the section. Servers that
do not publish record fields (portable servers, or names that are not IOC
records) show a note instead.

## Array inspection

The **Array** section, offered for array PVs, shows up to 2048 retained values
and an index plot. Snapshot JSON contains
all retained values. CA uses actual-length subscriptions so waveforms report
their valid element count rather than padded capacity. Both CA and PVA receive
the server's update but copy only the retained prefix into QtProbe. The
retention limit does not limit network traffic. CA's `EPICS_CA_MAX_ARRAY_BYTES`
setting also applies to received CA messages.
PVA capacity describes the received buffer; it is not assumed to be the
server's maximum writable array length.

## History and statistics

History is local to each open PV, with native value, IOC timestamp, alarm
severity, and connection transitions. **Trend** plots it with statistics;
**History** lists the events. Statistics use finite numeric scalar samples
(arrays have none) and report population standard deviation.

Enum PVs (mbbi/mbbo, bi/bo and PVA NTEnum) are plotted as states, not numbers:
each state holds until the next event, so the trace steps between levels instead
of sloping, the axis shows every state name (thinned when they do not fit), and
nothing is shaded under the line. Their statistics show the number of events and
state changes and the share of time spent in each state while QtProbe was
watching, measured from event receipt times so an old or skewed IOC clock cannot
distort it. The table's sparklines step the same way. The trend's horizontal
axis is monitor event index; the CSV retains exact IOC timestamps and client
receipt times for time analysis. Disconnects create gaps. Dense plots render a
screen-column envelope that retains endpoints and extrema; they leave the
underlying samples, statistics and export unchanged. History has two hard
limits, so QtProbe can run for days without growing: at most `--history` events
per PV (10000 by default), and an estimated `--history-memory` for all PVs
together (256 MB by default). Each PV may use an equal share of the memory
limit; when a PV adds an event beyond either limit, its oldest events are
dropped, and the newest event is always kept. A busy PV therefore keeps a
shorter time span than a quiet one. Values longer than 1000 characters, such as
long strings, are stored shortened with "…" in history and in its CSV; the live
value, snapshot JSON and Copy value stay exact. The History table shows the
newest 1000 events; the plot, statistics and CSV use the retained history.
**Clear** in the History section resets it. Fast PVA updates can be coalesced
between UI polls; Info reports coalescing for the latest batch. This history is
an inspection aid, not an archival logger.

Rendering is deferred and coalesced: closed inspector sections and table rows
outside the scroll viewport catch up when shown, and idle views do not run
periodic refresh timers. A hidden inspector, or a closed section, releases its
copies of the plot and table data and rebuilds them when shown again, so memory
does not grow with the number of PVs inspected; a paused inspector keeps what it
shows. The one exception to the timer rule is the table's Age column, which
ticks once a second while the table is shown and stops when it is hidden or
minimized. Hidden/minimized windows continue acquiring bounded history. CA
shares one context pump across the workspace; PVA wakes Qt only when its bounded
inbox has new work. These display optimizations do not issue extra network reads
or writes.

## Export and sessions

File exports the current snapshot as JSON and retained history as CSV using
atomic saves. Sessions (`.qtp`) save PV addresses and reopen them as tabs and
table rows; they contain no values to replay or write. Session opens validate
every address before adding PVs. Read-only mode is a launch option and applies
to sessions.

## Help and manuals

**Help → Help Topics** or **F1** opens the same searchable help browser used by
the other Qt ports. The sidebar selects a topic, and the document scrolls to
that section. Scrolling and search keep the topic selection in sync. Reopening
Help reuses its existing window. The guide is embedded, so it works offline.

Find in help searches the entire guide without regard to case. Every occurrence
is highlighted. Use the arrow buttons, Enter / Shift+Enter, or F3 / Shift+F3 to
move between matches; search wraps at either end. Ctrl+F focuses the search
field, and Esc closes Help. A separate Keyboard Shortcuts menu entry opens the
[shortcut reference](#keyboard-shortcuts).

**User Manual**, in the menu or help browser, opens the guide in your default
browser. By default, QtProbe prepares an offline HTML copy in its application
cache. Set `QTPROBE_HELP_PATH` to an installed manual file or a site URL to open
that manual instead, for example:

```sh
QTPROBE_HELP_PATH=/path/to/qtprobe-manual.html qtprobe
```

**About QtProbe** follows the companion applications' layout: animated banner,
version, author credits, Qt/EPICS versions and an Open help button. Click the
illustration or press Space to scan its decorative signal. The illustration
does not connect to a PV. Open help closes About before showing the guide.

## Keyboard shortcuts

Use Command instead of Ctrl on macOS where a platform shortcut applies.

| Where | Keys | Action |
| --- | --- | --- |
| Main window | F1 | Open Help Topics |
| Main window | Ctrl+O | Focus the PV name field |
| Main window | Ctrl+L | Focus the PV name field |
| Main window | Ctrl+W | Close the selected PV |
| Main window | Ctrl+1 | Show the All PVs table |
| Main window | Ctrl+2 | Inspect the selected PV |
| Main window | Ctrl+PgDown / Ctrl+PgUp | Next / previous tab |
| Main window | Ctrl+T | Open the selected PVs in QtStripTool |
| Main window | Ctrl++ or Ctrl+= / Ctrl+− / Ctrl+0 | Zoom in / out / actual size |
| PV table | Up / Down, Page Up / Page Down, Home / End | Move the selection (Shift extends it) |
| PV table | Enter or double-click | Inspect the selected PV |
| PV table | Ctrl+A / Ctrl+C / Esc | Select all shown rows / copy them / clear the selection |
| Table filter | Down | Move into the table |
| Main window | Ctrl+Q | Quit QtProbe |
| PV name field | Enter | Open the named PV |
| Write box | Enter | Write the edit when writing is available |
| Help | Ctrl+F | Focus Find in help |
| Help | Enter or F3 | Next search match |
| Help | Shift+Enter or Shift+F3 | Previous search match |
| Help | Esc | Close the help window |
| About banner | Space or Enter | Play the illustrative scan |

## Troubleshooting

- **No connection:** check the PV spelling and selected provider. Explicit
  `ca://` and `pva://` prefixes override the provider selector. The normal EPICS
  CA/PVA address-list environment settings apply. GUI discovery continues after
  a connection timeout; use Reconnect to begin a fresh connection.
- **Built without pvAccess:** rebuild against EPICS Base with pvAccess and
  pvData installed. CA-only builds report this diagnostic for PVA addresses.
- **Write is disabled:** check read-only mode, connection state, access rights
  and whether a write is pending (a slow record keeps a write pending until it
  finishes). A write whose outcome is unknown (a disconnect during the write,
  or Reconnect/Stop while it was pending) requires checking readback and
  reconnecting before another write is enabled. Large 64-bit
  integers use the exact value editor rather than slider/dial adjustments.
- **Fewer array values shown:** the array table and plot show the first 2048
  retained values; `--max-elements` controls retention. JSON export includes the
  retained prefix. See [Array inspection](#array-inspection).
- **Display appears stopped:** check Pause and Stop. Pause holds the display
  while acquisition continues; Stop releases the channel.

## Compatibility and current limits

The Motif implementation is preserved in `probe/`; `make probe` builds its
original executable. Its historical EPICS makefile is available through
`make -C probe LEGACY_EPICS_BUILD=1` in a configured extensions tree.
QtProbe preserves the core workflow of entering a PV, monitoring, stopping,
adjusting, reviewing history/info and choosing a format, in a similarly small
window with an inline write box and Adjust/History/Info toggles. It adds PVA,
arrays, an open-PV workspace with a live table of readbacks, QtEDM PV drops,
sessions, export, read-only mode and a headless metadata command.
The Qt layout differs from Motif and does not consume Xt resources or its
font options. Numeric display supports Native, Fixed, Scientific, Hex and
Binary; nonintegral values remain native in integer formats. Dial controls are
Qt controls rather than the legacy digit wheels. CA character arrays holding
text and `$` long-string fields are shown and written as text; binary byte
arrays keep the numeric view. PVA unions and
structure arrays can be browsed in Structure but are not editable. PVA writes do not request
record processing beyond the server's normal put behavior.

Linux Qt 5 was the initial acceptance baseline. The README records subsequent
native Windows validation with MSVC and Qt 6.11.2, including CA and PVA. Consult
the current release audit for the exact platforms and build variants tested;
results apply to those configurations. Production IOC interoperability requires
site validation. No production PV writes are part of the test suite.
