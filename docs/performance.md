# QtProbe CPU checks

Measured on 2026-10-16 with Linux x86-64, an Intel Core Ultra 9 285K,
Qt 5.15.9/Fusion, EPICS 7.0.8 and the release `-O2` build. Windows were
1280 × 940 on Qt's offscreen platform. Each case warmed up for two seconds
and measured approximately four seconds of process CPU time, including worker
threads. Percentages refer to **one CPU core**, not the whole machine.

| Synthetic workload | Before | After |
| --- | ---: | ---: |
| Empty window | 0.09% | 0.07% |
| 100 static PVs | 0.17% | 0.07% |
| All PVs: 100 PVs at 10 Hz, 10,000 history events each | 8.65% | 3.43% |
| Inspector Overview: one PV at 10 Hz, 10,000 history events | 53.02% | 2.01% |

After the compact layout (same machine, same day), the synthetic checks
measured 0.07% for an empty window, 0.07% for 100 static PVs, 2.2% for the All
PVs table with 100 PVs at 10 Hz and 10,000 history events each, and, for one
PV at 10 Hz with 10,000 events, 0.65% with no section open, 0.82% with Trend
open (the former Overview), 0.97% with History open and 0.09% hidden. The
benchmark requests 1280 × 940, but the window then fits its content, so the
table shows fewer rows and the plots are shorter than in the earlier runs.
With the table's Age column, which ticks once a second while the table is
shown, 100 static PVs measured 0.12% and 100 PVs at 10 Hz 2.3%.

Memory: retained history is bounded by `--history` events per PV and by
`--history-memory` for all PVs. With 100 synthetic PVs and a 64 MB limit,
preloading 20,000 or 60,000 events per PV (2 or 6 million events) gave the
same 64 MB of history and the same 254 MB peak RSS, against 190 MB with no
history. The estimate tracks measured memory closely (64 MB of budget added
64 MB of RSS). The 190 MB baseline is 100 inspectors' widgets, which does not
grow over time; `--history-memory` controls the part that would.
`benchmark_cpu` reports `history_mb` and `max_rss_mb`, and accepts
`--history-memory`.

Real CA/PVA measurements used a separate local IOC with 100 static records and
100 records scanning at 10 Hz. Client CPU includes network handling and Qt;
it excludes the IOC process:

| Loopback workload | Before | After |
| --- | ---: | ---: |
| 1 static CA PV | 0.40% | 0.35% |
| 100 static CA PVs | 0.85% | 0.38% |
| 100 CA PVs at 10 Hz | 5.72% | 2.59% |
| 100 static PVA PVs | 0.77% | 0.08% |
| 100 PVA PVs at 10 Hz | 7.72% | 4.73% |

Active runs delivered approximately 3,900–4,100 updates during each four-second
measurement. The benchmark rejects a run that fails to connect or deliver data.
Additional synthetic checks at 10 Hz with 10,000 retained events measured
2.25% for the visible History page and 0.09% with the window hidden.

The full-history inspector exposed the main problems: it rebuilt its hidden
history/array/metadata tables and rasterized an expensive filled, antialiased
path on every update. The optimized version updates the active section, inserts
new history rows while retaining existing cells and selection, and uses a
pixel-column envelope for dense plots. Short traces keep their gradient and
antialiasing; dense traces use a thin polyline. The envelope retains endpoints,
extrema and connection breaks. Original samples, exact values, statistics,
JSON and CSV exports remain intact.

Idle GUI refresh timers are now single-shot and run only after a change.
Closed sections and table rows outside the scroll viewport catch up when shown;
hidden/minimized windows continue acquiring bounded history. The CA context
has one shared 25 ms pump, and channel delivery drains only when dirty.
PVA queues one wakeup when its bounded inbox becomes nonempty instead of
polling an empty inbox for every PV. Backend wakeups are marshalled to Qt's
thread, including CA receive-thread callbacks.

Run `make -C qtprobe benchmark-cpu` for the synthetic workloads. For real
loopback CA/PVA traffic, run from `qtprobe/`:

```sh
python3 tests/benchmark_ioc_cpu.py O.Linux-x86_64-qt5/benchmark_cpu
```

The runner creates its own static and 10 Hz records, loopback ports, IOC and
foreground repeater, opens read-only clients, checks delivered update counts,
and cleans up its processes. It never uses site PV names. `EPICS_TEST_BIN`
selects another EPICS installation. The benchmark executable also accepts
`--view trend`, `--view history`, `--view hidden`, `--screenshot path`, and
`--profile-events`. Event timings are inclusive and add diagnostic overhead;
leave profiling off for CPU comparisons. See `--help` for other workload
options.

These are local measurements, not timing thresholds in the test suite. Array
sizes, monitor rates, retained history and the platform's rendering backend
affect CPU use. Correctness tests cover deferred rendering, idle timers,
scroll catch-up, history ordering/selection/clear, dense plot extrema/gaps,
and shared delivery/reconnect/peer removal against real CA/PVA test records.
