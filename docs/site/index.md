---
title: Documentation
description: Build QtProbe, inspect CA and PVA variables, and find the operator and command-line reference.
---

<div class="doc-eyebrow">EPICS PV INSPECTOR · QT 5 / QT 6</div>

# QtProbe documentation

<p class="doc-intro">Inspect live EPICS values, follow local trends, explore record diagnostics and browse native pvAccess structures.</p>

<div class="doc-paths">
  <a class="doc-path" href="./get-started/first-run.html"><span class="path-number">01 / LEARN</span><strong>Inspect your first PV →</strong><span>Add a channel and find its value, units and alarm state.</span></a>
  <a class="doc-path" href="./operate/workspace.html"><span class="path-number">02 / OPERATE</span><strong>Explore the workspace →</strong><span>Use the All table, inspector sections and appearance controls.</span></a>
  <a class="doc-path" href="./operate/records.html"><span class="path-number">03 / DIAGNOSE</span><strong>Understand a record →</strong><span>Read processing fields, follow links and inspect diagnostics.</span></a>
  <a class="doc-path" href="./reference/command-line.html"><span class="path-number">04 / LOOK UP</span><strong>Find a command-line option →</strong><span>Check providers, timeouts, history limits and headless reports.</span></a>
</div>

## Monitor CA and PVA together

The **All** table shows values, units, alarms, update ages and sparklines for
multiple PVs. [Build QtProbe](./get-started/install), then open the application
without PV arguments to enter a name yourself:

```sh
bin/Linux-x86_64/qtprobe --read-only
```

Substitute the executable directory for your platform. `--read-only` disables
writes for that process. Plain PV names use the selected provider; `ca://` and
`pva://` prefixes select one explicitly.

![QtProbe All table with demonstration CA and PVA values, alarms and sparklines](/images/qtprobe-overview.png)

All screenshots show demonstration data from a local IOC on macOS with Qt 6.

## Follow values and local history

The inspector combines the current value, alarm bands, trend plot, statistics
and adjustment controls. [Live values](./operate/values),
[adjustments](./operate/adjustments) and [history](./operate/history) explain
the readbacks, write syntax and retained samples.

![QtProbe inspector with trend, statistics and staged adjustment controls](/images/qtprobe-trend-controls.png)

## Inspect records and structures

The [record explorer](./operate/records) explains common processing problems.
The [structure browser](./operate/structures) displays PVA fields, values and
metadata, with search and copy actions.

![QtProbe record explorer explaining a disabled demonstration record](/images/qtprobe-record-diagnostics.png)

![QtProbe in dark appearance displaying a pvAccess structure](/images/qtprobe-pva-structure.png)

## Work with the companion applications

[Drag PV names](./operate/drag-and-drop) between QtProbe, QtEDM, legacy MEDM and
QtStripTool. Review [compatibility and current limits](./understand/compatibility)
when changing an operator launcher, or read the [test guide](./develop/testing)
and [documentation workflow](./develop/documentation) when contributing.

<a href="./downloads/qtprobe-guide.md" download>Download the original operator guide (Markdown)</a>.
