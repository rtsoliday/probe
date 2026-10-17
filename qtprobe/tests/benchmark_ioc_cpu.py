"""Opt-in Linux benchmark of a read-only GUI against an owned loopback IOC.

Run from qtprobe/: python3 tests/benchmark_ioc_cpu.py O.../benchmark_cpu.
All records, ports, settings and child processes belong to this invocation.
"""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def ports():
    for base in range(22000 + os.getpid() % 4000 * 4, 65000, 4):
        held = []
        try:
            for port in range(base, base + 4):
                for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
                    sock = socket.socket(socket.AF_INET, kind)
                    held.append(sock)
                    sock.bind(("127.0.0.1", port))
            return base
        except OSError:
            pass
        finally:
            for sock in held:
                sock.close()
    raise RuntimeError("No isolated loopback ports available")


def main():
    binary = str(Path(sys.argv[1]).resolve())
    epics = Path(os.environ.get("EPICS_TEST_BIN", "/usr/local/oag/base/bin/linux-x86_64"))
    base = ports()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen",
               EPICS_CA_AUTO_ADDR_LIST="NO", EPICS_CA_ADDR_LIST=f"127.0.0.1:{base}",
               EPICS_CA_SERVER_PORT=str(base), EPICS_CA_REPEATER_PORT=str(base + 1),
               EPICS_CAS_INTF_ADDR_LIST="127.0.0.1", EPICS_CAS_BEACON_ADDR_LIST=f"127.0.0.1:{base + 1}",
               EPICS_PVA_AUTO_ADDR_LIST="NO", EPICS_PVA_ADDR_LIST="127.0.0.1",
               EPICS_PVA_SERVER_PORT=str(base + 2), EPICS_PVA_BROADCAST_PORT=str(base + 3),
               EPICS_PVAS_INTF_ADDR_LIST="127.0.0.1")
    prefix = f"QtProbePerf:{os.getpid()}:"
    ioc = repeater = None
    with tempfile.TemporaryDirectory(prefix="qtprobe-cpu-") as directory:
        temp = Path(directory)
        env["XDG_CONFIG_HOME"] = str(temp / "config")
        records = []
        for i in range(100):
            records.append(f'record(ao, "{prefix}static:{i}") {{ field(VAL,"12.5") field(DESC,"Static benchmark") field(PINI,"YES") }}')
            records.append(f'record(calc, "{prefix}active:{i}") {{ field(SCAN,".1 second") field(INPA,"{prefix}active:{i} NPP") field(CALC,"A+1") field(DESC,"10 Hz benchmark") }}')
        db = temp / "benchmark.db"
        db.write_text("\n".join(records))
        with (temp / "ioc.log").open("w+") as log:
            try:
                repeater = subprocess.Popen([str(epics / "caRepeater"), "-v"], env=env, stdout=log, stderr=log)
                ioc = subprocess.Popen([str(epics / "softIocPVA"), "-d", str(db)], env=env, stdin=subprocess.PIPE, stdout=log, stderr=log)
                time.sleep(0.5)
                if ioc.poll() is not None:
                    raise RuntimeError("Test IOC exited during startup")
                for provider, kind, count in [("ca", "static", 1), ("ca", "static", 100), ("ca", "active", 100),
                                               ("pva", "static", 100), ("pva", "active", 100)]:
                    result = subprocess.run([binary, "--provider", provider, "--pvs", str(count), "--prefix", prefix + kind + ":"],
                                            env=env, text=True, capture_output=True, timeout=40)
                    if result.returncode:
                        raise RuntimeError(f"Benchmark failed ({result.returncode}): {result.stdout}\n{result.stderr}")
                    data = json.loads(result.stdout)
                    data["workload"] = kind
                    if kind == "active" and data["delivered_updates"] < count:
                        raise RuntimeError("IOC updates were not delivered")
                    print(json.dumps(data), flush=True)
            except Exception:
                log.flush()
                print((temp / "ioc.log").read_text(), file=sys.stderr)
                raise
            finally:
                stop(ioc)
                stop(repeater)


if __name__ == "__main__":
    main()
