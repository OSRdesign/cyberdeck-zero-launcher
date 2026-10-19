#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""memprobe: memory and CPU of resident apps, for study 026 phase P-a (items 3, 4 and 6).

  python3 memprobe.py --apps SPEC[,SPEC...] [options]

  SPEC is a launcher app id (its Exec= is read from /usr/share/APPLaunch/applications/<id>.desktop) or NAME=COMMAND
  (a shell command), e.g.  --apps cyberdeck-app-template,system-monitor,serial-console,viz1090

For every app on its own: started in its own process group, then measured running (after --settle s), frozen
(SIGSTOP of the group), thawed and closed: MemAvailable, swap and zram use, load, and per process RSS / PSS /
PSS_anon / PSS_file / swap from /proc/<pid>/smaps_rollup, plus the CPU of the group over --cpu-window s running and
frozen. Then the resident series: app 1 started and frozen, app 2 started (1 frozen + 1 running), ... up to
--resident apps, which answers "how many apps fit". --tcp-port N logs the kernel queues of the TCP connections on
that port in every phase (viz1090: 30005, does readsb tolerate a frozen viz1090?). --oom-test checks raising and
lowering oom_score_adj without privilege (P-a item 6).

Nothing is left stopped: every exit path sends SIGCONT then SIGTERM (SIGKILL 3 s later) to the groups it started,
and a guardian process does the same if memprobe itself is killed. Like fbhandoff, it refuses to run while the
launcher owns the panel (the apps draw full screen) unless --force, or --stop-launcher stops the launcher and starts
it again at the end.

Results: <out-dir>/memprobe-<host>-<date>.txt (the table) and .json (everything). Standard library only.
"""
import argparse
import json
import os
import re
import shlex
import signal
import socket
import subprocess
import sys
import time

APPS_DIR = "/usr/share/APPLaunch/applications"
MB = 1024.0 * 1024.0
FLOOR_MB = 64  # study 026 D6: the launcher closes the LRU frozen app under this


# ------------------------------------------------------------------------------------------------ parsers (pure)
def parse_meminfo(text):
    """{'MemTotal': kB, ...}"""
    out = {}
    for line in text.splitlines():
        m = re.match(r"(\w+):\s+(\d+)", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def parse_smaps_rollup(text):
    """{'Rss': kB, 'Pss': kB, 'Pss_Anon': kB, ...}"""
    out = {}
    for line in text.splitlines():
        m = re.match(r"(\w+):\s+(\d+) kB", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def parse_status(text):
    out = {}
    for line in text.splitlines():
        m = re.match(r"(VmRSS|RssAnon|RssFile|RssShmem|VmSwap):\s+(\d+) kB", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def parse_stat(text):
    """(comm, state, pgrp, utime+stime ticks) from /proc/<pid>/stat"""
    rp = text.rfind(")")
    comm = text[text.find("(") + 1:rp]
    f = text[rp + 2:].split()
    return comm, f[0], int(f[2]), int(f[11]) + int(f[12])


def parse_zram(text):
    f = text.split()
    if len(f) < 3:
        return {}
    return {"orig_mb": int(f[0]) / MB, "compr_mb": int(f[1]) / MB, "used_mb": int(f[2]) / MB}


def parse_desktop_exec(text):
    """Exec= of a .desktop entry, field codes (%f, %U...) removed"""
    for line in text.splitlines():
        if line.startswith("Exec="):
            return re.sub(r"\s*%[a-zA-Z]", "", line[5:]).strip()
    return None


def parse_tcp(text, port):
    """rows of /proc/net/tcp{,6} with the port on either side: (local, remote, state, tx_queue, rx_queue)"""
    rows = []
    for line in text.splitlines()[1:]:
        f = line.split()
        if len(f) < 5:
            continue
        lp, rp = int(f[1].split(":")[1], 16), int(f[2].split(":")[1], 16)
        if port not in (lp, rp):
            continue
        tx, rx = (int(v, 16) for v in f[4].split(":"))
        rows.append({"local_port": lp, "remote_port": rp, "state": f[3], "tx_queue": tx, "rx_queue": rx})
    return rows


def fit_estimate(baseline_avail_mb, deltas_mb, floor_mb=FLOOR_MB):
    """how many more apps of the measured mean cost fit above the floor"""
    costs = [d for d in deltas_mb if d is not None and d > 0]
    if not costs or baseline_avail_mb is None:
        return None
    mean = sum(costs) / len(costs)
    return {"mean_cost_mb": round(mean, 1), "floor_mb": floor_mb,
            "apps_that_fit": int(max(0, baseline_avail_mb - floor_mb) // mean)}


# ------------------------------------------------------------------------------------------------ /proc readers
def read(path, default=""):
    try:
        with open(path, "r", errors="replace") as f:
            return f.read()
    except OSError:
        return default


def system_snapshot():
    mi = parse_meminfo(read("/proc/meminfo"))
    z = parse_zram(read("/sys/block/zram0/mm_stat"))
    load = read("/proc/loadavg").split()
    return {
        "mem_total_mb": mi.get("MemTotal", 0) / 1024.0,
        "mem_avail_mb": mi.get("MemAvailable", 0) / 1024.0,
        "mem_free_mb": mi.get("MemFree", 0) / 1024.0,
        "cached_mb": mi.get("Cached", 0) / 1024.0,
        "shmem_mb": mi.get("Shmem", 0) / 1024.0,
        "swap_used_mb": (mi.get("SwapTotal", 0) - mi.get("SwapFree", 0)) / 1024.0,
        "zram": z,
        "load1": float(load[0]) if load else None,
    }


def group_pids(pgid):
    out = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        st = read(f"/proc/{d}/stat")
        if not st:
            continue
        try:
            _comm, state, pgrp, _t = parse_stat(st)
        except (IndexError, ValueError):
            continue
        if pgrp == pgid and state not in ("Z", "X"):
            out.append(int(d))
    return sorted(out)


def group_snapshot(pgid):
    procs = []
    tot = {"rss_mb": 0.0, "pss_mb": 0.0, "pss_anon_mb": 0.0, "pss_file_mb": 0.0, "swap_mb": 0.0}
    for pid in group_pids(pgid):
        comm, state, _pg, _t = parse_stat(read(f"/proc/{pid}/stat", "0 (?) X 0 0 0 0 0 0 0 0 0 0 0 0"))
        sm = parse_smaps_rollup(read(f"/proc/{pid}/smaps_rollup"))
        st = parse_status(read(f"/proc/{pid}/status"))
        p = {"pid": pid, "comm": comm, "state": state, "rss_mb": sm.get("Rss", st.get("VmRSS", 0)) / 1024.0,
             "pss_mb": sm.get("Pss", 0) / 1024.0, "pss_anon_mb": sm.get("Pss_Anon", 0) / 1024.0,
             "pss_file_mb": sm.get("Pss_File", 0) / 1024.0, "swap_mb": sm.get("Swap", st.get("VmSwap", 0)) / 1024.0,
             "rss_anon_mb": st.get("RssAnon", 0) / 1024.0, "rss_file_mb": st.get("RssFile", 0) / 1024.0}
        procs.append(p)
        for k in tot:
            tot[k] += p[k]
    return {"procs": procs, **tot}


def group_ticks(pgid):
    out = {}
    for pid in group_pids(pgid):
        try:
            comm, _s, _pg, t = parse_stat(read(f"/proc/{pid}/stat"))
            out[pid] = (comm, t)
        except (IndexError, ValueError):
            pass
    return out


def cpu_window(pgids, seconds, tcp_port=None, tcp_log=None, label=""):
    """CPU of each group over the window, % of one core (per process and total)"""
    hz = os.sysconf("SC_CLK_TCK")
    before = {pg: group_ticks(pg) for pg in pgids}
    t0 = time.monotonic()
    while time.monotonic() - t0 < seconds:
        time.sleep(min(1.0, max(0.0, seconds - (time.monotonic() - t0))))
        if tcp_port and tcp_log is not None:
            tcp_log.append({"t": round(time.monotonic(), 2), "phase": label, "rows": tcp_rows(tcp_port)})
    dt = time.monotonic() - t0
    out = {}
    for pg in pgids:
        after = group_ticks(pg)
        per = {}
        for pid, (comm, t) in after.items():
            t_before = before[pg][pid][1] if pid in before[pg] else 0  # started during the window
            per[f"{comm}:{pid}"] = round(100.0 * (t - t_before) / hz / dt, 2)
        out[pg] = {"per_process": per, "total_pct": round(sum(per.values()), 2), "window_s": round(dt, 1)}
    return out


def tcp_rows(port):
    return parse_tcp(read("/proc/net/tcp"), port) + parse_tcp(read("/proc/net/tcp6"), port)


# ------------------------------------------------------------------------------------------------ launcher
def find_launcher():
    pids = []
    for d in os.listdir("/proc"):
        if d.isdigit() and read(f"/proc/{d}/comm").strip() in ("cyberdeckzero", "APPLaunch"):
            pids.append(int(d))
    return pids


def launcher_service(verb):
    sc = os.environ.get("MTA_SYSTEMCTL") or "systemctl"
    return subprocess.call([sc, "--user", verb, "cyberdeckzero.service"])


def launcher_env(pid):
    env = {}
    raw = read(f"/proc/{pid}/environ")
    for item in raw.split("\0"):
        if "=" in item and (item.startswith("APPLAUNCH_") or item.startswith("LV_")):
            k, v = item.split("=", 1)
            env[k] = v
    return env


def load_env_file(path):
    env = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[7:]
            if "=" in line:
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip('"').strip("'")
    return env


# ------------------------------------------------------------------------------------------------ the probe
class Probe:
    def __init__(self, args, out_base):
        self.a = args
        self.out_base = out_base
        self.groups = {}  # name -> Popen
        self.launcher_stopped = False
        self.state_path = out_base + ".state"
        self.log_path = out_base + "-apps.log"
        self.data = {"tool": "memprobe", "version": 1, "host": socket.gethostname(), "started": time.ctime(),
                     "args": vars(args), "states": [], "apps": {}, "resident": [], "tcp": [], "notes": []}
        self.env = dict(os.environ)
        self.guard_w = None

    # -- bookkeeping shared with the guardian
    def save_state(self, clean=False):
        tmp = self.state_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump({"pgids": [p.pid for p in self.groups.values()], "launcher_stopped": self.launcher_stopped,
                       "clean": clean}, f)
        os.replace(tmp, self.state_path)

    def start_guardian(self):
        r, w = os.pipe()
        pid = os.fork()
        if pid == 0:  # the guardian
            os.close(w)
            os.setsid()
            for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT, signal.SIGPIPE):
                signal.signal(s, signal.SIG_IGN)
            while True:
                try:
                    if not os.read(r, 1):
                        break
                except InterruptedError:
                    continue
                except OSError:
                    break
            try:
                with open(self.state_path) as f:
                    st = json.load(f)
            except (OSError, ValueError):
                os._exit(0)
            if st.get("clean"):
                os._exit(0)
            for pg in st.get("pgids", []):
                end_group(pg)
            if st.get("launcher_stopped"):
                launcher_service("start")
            try:
                with open(self.out_base + ".txt", "a") as f:
                    f.write(f"memprobe guardian: ended groups {st.get('pgids')}, launcher started "
                            f"{bool(st.get('launcher_stopped'))}\n")
            except OSError:
                pass
            os._exit(0)
        os.close(r)
        os.set_inheritable(w, False)
        self.guard_w = w
        self.guard_pid = pid

    def say(self, msg):
        print(msg, flush=True)

    def snap(self, label, pg_names=()):
        s = {"label": label, "t": round(time.monotonic(), 1), "system": system_snapshot(), "groups": {}}
        for n in pg_names:
            p = self.groups.get(n)
            if p:
                s["groups"][n] = group_snapshot(p.pid)
        if self.a.tcp_port:
            s["tcp"] = tcp_rows(self.a.tcp_port)
        self.data["states"].append(s)
        sysd = s["system"]
        grp = " ".join(f"{n}: RSS {g['rss_mb']:.1f} PSS {g['pss_mb']:.1f} (anon {g['pss_anon_mb']:.1f}) swap "
                       f"{g['swap_mb']:.1f}" for n, g in s["groups"].items())
        self.say(f"  [{label}] MemAvailable {sysd['mem_avail_mb']:.1f} MB, swap {sysd['swap_used_mb']:.1f} MB, "
                 f"load {sysd['load1']}  {grp}")
        return s

    def start_app(self, name, cmd):
        log = open(self.log_path, "a")
        log.write(f"\n==== {time.ctime()} start {name}: {cmd}\n")
        log.flush()
        # a simple command replaces the shell (the app leads its group, as under the launcher's runner)
        run = cmd if re.search(r"[;&|<>`$()\n]", cmd) else "exec " + cmd
        p = subprocess.Popen(run, shell=True, start_new_session=True, stdin=subprocess.DEVNULL, stdout=log,
                             stderr=subprocess.STDOUT, env=self.env)
        log.close()
        self.groups[name] = p
        self.save_state()
        self.say(f"  started {name} (pgid {p.pid}): {cmd}")
        return p

    def settle(self, name):
        sec = self.a.settle_for.get(name, self.a.settle)
        if name in self.a.prompt_for:
            self.say(f"  >>> {self.a.prompt_for[name]}  ({sec:.0f} s)")
        time.sleep(sec)

    def freeze(self, name):
        p = self.groups[name]
        try:
            os.killpg(p.pid, signal.SIGSTOP)
        except ProcessLookupError:
            self.data["notes"].append(f"{name} was gone when it should be frozen")
            return
        t0 = time.monotonic()
        while time.monotonic() - t0 < 1.0:
            states = [read(f"/proc/{pid}/stat").rpartition(")")[2].split()[:1] for pid in group_pids(p.pid)]
            if all(s and s[0] in ("T", "t") for s in states):
                break
            time.sleep(0.01)

    def thaw(self, name):
        try:
            os.killpg(self.groups[name].pid, signal.SIGCONT)
        except ProcessLookupError:
            self.data["notes"].append(f"{name} was gone when it should be thawed")

    def close(self, name):
        p = self.groups.pop(name, None)
        if p is not None:
            end_group(p.pid, p)
            try:
                p.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
        self.save_state()

    def close_all(self):
        for n in list(self.groups):
            self.close(n)

    def alive(self, name):
        p = self.groups.get(name)
        return p is not None and p.poll() is None

    # -- phases
    def single(self, name, cmd):
        a = self.a
        self.say(f"\n== {name} alone")
        rec = {"cmd": cmd}
        self.start_app(name, cmd)
        self.settle(name)
        if not self.alive(name):
            self.say(f"  {name} exited during the settle time (see {self.log_path})")
            rec["error"] = "exited before the measurement"
            self.close(name)
            self.data["apps"][name] = rec
            return
        rec["running"] = self.snap(f"{name} running", [name])
        self.say(f"  CPU while running, {a.cpu_window:.0f} s...")
        rec["cpu_running"] = cpu_window([self.groups[name].pid], a.cpu_window, a.tcp_port, self.data["tcp"],
                                        f"{name} running")[self.groups[name].pid]
        self.freeze(name)
        time.sleep(2)
        rec["frozen"] = self.snap(f"{name} frozen", [name])
        self.say(f"  CPU while frozen, {a.cpu_window:.0f} s...")
        rec["cpu_frozen"] = cpu_window([self.groups[name].pid], a.cpu_window, a.tcp_port, self.data["tcp"],
                                       f"{name} frozen")[self.groups[name].pid]
        self.thaw(name)
        time.sleep(3)
        rec["thawed_alive"] = self.alive(name)
        rec["thawed"] = self.snap(f"{name} thawed", [name])
        self.close(name)
        time.sleep(3)
        rec["closed"] = self.snap(f"{name} closed")
        self.data["apps"][name] = rec
        self.say(f"  {name}: CPU running {rec['cpu_running']['total_pct']} %, frozen {rec['cpu_frozen']['total_pct']} %"
                 f", alive after thaw {rec['thawed_alive']}")

    def resident(self, apps):
        n = min(self.a.resident, len(apps))
        self.say(f"\n== resident series: {n} apps, the earlier ones frozen")
        base = self.snap("resident 0 (none)")
        prev = base["system"]["mem_avail_mb"]
        names = []
        for i, (name, cmd) in enumerate(apps[:n]):
            self.start_app(name, cmd)
            self.settle(name)
            names.append(name)
            s = self.snap(f"resident {i + 1} ({name} running, {i} frozen)", names)
            avail = s["system"]["mem_avail_mb"]
            self.data["resident"].append({"n": i + 1, "app": name, "mem_avail_mb": avail,
                                          "delta_mb": round(prev - avail, 1), "alive": self.alive(name)})
            prev = avail
            if self.alive(name):
                self.freeze(name)
        time.sleep(2)
        self.snap(f"resident {n} (all frozen)", names)
        self.close_all()
        time.sleep(3)
        self.snap("after the resident series")

    def oom_test(self):
        self.say("\n== oom_score_adj without privilege")
        p = subprocess.Popen(["sleep", "60"], start_new_session=True)
        path = f"/proc/{p.pid}/oom_score_adj"
        res = {"initial": int(read(path, "0").strip() or 0), "writes": []}
        for v in (700, 900, 700, res["initial"], res["initial"] - 100):
            try:
                with open(path, "w") as f:
                    f.write(str(v))
                ok, err = True, None
            except OSError as e:
                ok, err = False, e.strerror
            res["writes"].append({"value": v, "ok": ok, "error": err, "now": int(read(path, "0").strip() or 0)})
        p.kill()
        p.wait()
        self.data["oom_test"] = res
        for w in res["writes"]:
            self.say(f"  write {w['value']}: {'ok' if w['ok'] else 'refused (' + str(w['error']) + ')'}, now {w['now']}")


def end_group(pgid, popen=None):
    """SIGCONT then SIGTERM, SIGKILL after 3 s: a stopped process acts on SIGTERM only once continued"""
    try:
        os.killpg(pgid, signal.SIGCONT)
        os.killpg(pgid, signal.SIGTERM)
    except ProcessLookupError:
        return
    except PermissionError:
        return
    t0 = time.monotonic()
    while time.monotonic() - t0 < 3:
        if popen is not None:
            popen.poll()  # reap the leader, else its zombie keeps the group alive
        try:
            os.killpg(pgid, 0)
        except ProcessLookupError:
            return
        time.sleep(0.1)
    try:
        os.killpg(pgid, signal.SIGKILL)
    except ProcessLookupError:
        pass


# ------------------------------------------------------------------------------------------------ report
def table(data):
    rows = []
    base = None
    for s in data["states"]:
        sy = s["system"]
        if base is None:
            base = sy["mem_avail_mb"]
        g = s["groups"]
        rss = sum(v["rss_mb"] for v in g.values())
        pss = sum(v["pss_mb"] for v in g.values())
        anon = sum(v["pss_anon_mb"] for v in g.values())
        swap = sum(v["swap_mb"] for v in g.values())
        z = sy.get("zram") or {}
        rows.append((s["label"], sy["mem_avail_mb"], base - sy["mem_avail_mb"], sy["swap_used_mb"],
                     z.get("orig_mb"), z.get("compr_mb"), sy["load1"], rss if g else None, pss if g else None,
                     anon if g else None, swap if g else None))
    head = (f"{'state':<46} {'avail':>7} {'d_avail':>7} {'swap':>6} {'zr_orig':>7} {'zr_comp':>7} {'load1':>5} "
            f"{'RSS':>6} {'PSS':>6} {'anon':>6} {'gswap':>6}")
    out = [head, "-" * len(head)]

    def f(v, w, nd=1):
        return f"{'-':>{w}}" if v is None else f"{v:>{w}.{nd}f}"
    for r in rows:
        out.append(f"{r[0][:46]:<46} {f(r[1], 7)} {f(r[2], 7)} {f(r[3], 6)} {f(r[4], 7)} {f(r[5], 7)} {f(r[6], 5, 2)} "
                   f"{f(r[7], 6)} {f(r[8], 6)} {f(r[9], 6)} {f(r[10], 6)}")
    out.append("(MB; d_avail = MemAvailable drop since the first line; RSS..gswap = the measured app groups)")
    out.append("")
    for name, a in data["apps"].items():
        if "running" not in a:
            out.append(f"{name}: {a.get('error')}")
            continue
        g = a["running"]["groups"].get(name, {})
        procs = ", ".join(f"{p['comm']} {p['pss_mb']:.1f}" for p in g.get("procs", []))
        out.append(f"{name}: RSS {g.get('rss_mb', 0):.1f} MB, PSS {g.get('pss_mb', 0):.1f} MB (anon "
                   f"{g.get('pss_anon_mb', 0):.1f}) [{procs}]; CPU running {a['cpu_running']['total_pct']} % "
                   f"{a['cpu_running']['per_process']}, frozen {a['cpu_frozen']['total_pct']} %; alive after thaw "
                   f"{a.get('thawed_alive')}")
    if data["resident"]:
        out.append("")
        out.append("resident series (MemAvailable used by each start): " + ", ".join(
            f"{r['n']}:{r['app']} {r['delta_mb']:.1f} MB" for r in data["resident"]))
        first = data["states"][0]["system"]["mem_avail_mb"] if data["states"] else None
        fe = fit_estimate(first, [r["delta_mb"] for r in data["resident"]])
        if fe:
            data["fit"] = fe
            out.append(f"fit: with MemAvailable {first:.0f} MB at the start and a {fe['floor_mb']} MB floor, "
                       f"{fe['apps_that_fit']} apps of the measured mean cost ({fe['mean_cost_mb']} MB each) fit")
    if data.get("tcp"):
        out.append("")
        out.append("tcp (port %s): " % data["args"].get("tcp_port"))
        for t in data["tcp"]:
            rows = "; ".join(f"{r['local_port']}->{r['remote_port']} st {r['state']} tx {r['tx_queue']} rx "
                             f"{r['rx_queue']}" for r in t["rows"]) or "no connection"
            out.append(f"  {t['phase']:<28} {rows}")
    if data.get("oom_test"):
        o = data["oom_test"]
        out.append("")
        out.append(f"oom_score_adj (initial {o['initial']}): " + ", ".join(
            f"{w['value']}={'ok' if w['ok'] else 'refused'}" for w in o["writes"]))
    for n in data.get("notes", []):
        out.append("note: " + n)
    return "\n".join(out)


# ------------------------------------------------------------------------------------------------ main
def resolve_apps(spec, apps_dir=APPS_DIR):
    out = []
    for item in [s for s in spec.split(",") if s.strip()]:
        item = item.strip()
        if "=" in item:
            name, cmd = item.split("=", 1)
            out.append((name.strip(), cmd.strip()))
            continue
        text = read(os.path.join(apps_dir, item + ".desktop"))
        cmd = parse_desktop_exec(text)
        if not cmd:
            raise SystemExit(f"memprobe: no {apps_dir}/{item}.desktop with an Exec= line (use NAME=COMMAND)")
        out.append((item, cmd))
    return out


def kv_list(values, cast=str):
    out = {}
    for v in values or []:
        k, _, val = v.partition("=")
        out[k] = cast(val)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apps", required=True)
    ap.add_argument("--settle", type=float, default=8, help="seconds after a start before measuring (default 8)")
    ap.add_argument("--settle-for", action="append", metavar="NAME=S", help="per-app settle time")
    ap.add_argument("--prompt-for", action="append", metavar="NAME=TEXT", help="tell the user what to do after start")
    ap.add_argument("--cpu-window", type=float, default=30, help="CPU measurement window, s (default 30)")
    ap.add_argument("--resident", type=int, default=4, help="apps in the resident series (default 4)")
    ap.add_argument("--skip-single", action="store_true")
    ap.add_argument("--skip-resident", action="store_true")
    ap.add_argument("--tcp-port", type=int)
    ap.add_argument("--oom-test", action="store_true")
    ap.add_argument("--out-dir", default=os.path.join(os.path.expanduser("~"), "mta-results"))
    ap.add_argument("--env-file")
    ap.add_argument("--no-launcher-env", action="store_true")
    ap.add_argument("--stop-launcher", action="store_true")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    a.settle_for = kv_list(a.settle_for, float)
    a.prompt_for = kv_list(a.prompt_for)
    apps = resolve_apps(a.apps)

    os.makedirs(a.out_dir, exist_ok=True)
    out_base = os.path.join(a.out_dir, f"memprobe-{socket.gethostname().split('.')[0]}-"
                                       f"{time.strftime('%Y%m%d-%H%M%S')}")
    probe = Probe(a, out_base)
    lp = find_launcher()
    if lp and not a.no_launcher_env:
        probe.env.update({k: v for k, v in launcher_env(lp[0]).items() if k not in os.environ})
    if a.env_file:
        probe.env.update(load_env_file(a.env_file))
    if lp and not (a.force or a.stop_launcher):
        print("memprobe: refusing to run: the launcher owns the panel (the apps draw full screen).\n"
              "  Stop it yourself:   systemctl --user stop cyberdeckzero   (and start it again afterwards)\n"
              "  or add --stop-launcher (memprobe stops it and starts it again at the end); --force runs anyway.",
              file=sys.stderr)
        return 3

    probe.save_state()
    probe.start_guardian()

    def on_signal(sig, _frame):
        raise KeyboardInterrupt(f"signal {sig}")
    for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(s, on_signal)

    reason = "done"
    try:
        if lp and a.stop_launcher:
            probe.launcher_stopped = True
            probe.save_state()
            print("memprobe: stopping the launcher (systemctl --user stop cyberdeckzero)", flush=True)
            launcher_service("stop")
            for _ in range(50):
                if not find_launcher():
                    break
                time.sleep(0.1)
            time.sleep(2)
        print(f"memprobe: {len(apps)} app(s): {', '.join(n for n, _ in apps)}; results {out_base}.txt", flush=True)
        probe.snap("baseline")
        if not a.skip_single:
            for name, cmd in apps:
                probe.single(name, cmd)
        if not a.skip_resident:
            probe.resident(apps)
        if a.oom_test:
            probe.oom_test()
    except KeyboardInterrupt as e:
        reason = f"interrupted ({e})"
        probe.data["notes"].append(reason)
        print(f"\nmemprobe: {reason}: cleaning up", flush=True)
    finally:
        for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            signal.signal(s, signal.SIG_IGN)
        probe.close_all()
        if probe.launcher_stopped:
            print("memprobe: starting the launcher again", flush=True)
            launcher_service("start")
            probe.launcher_stopped = False
        probe.save_state(clean=True)
        if probe.guard_w is not None:
            os.close(probe.guard_w)
            os.waitpid(probe.guard_pid, 0)
        probe.data["end"] = reason
        txt = table(probe.data)
        with open(out_base + ".txt", "a") as f:
            f.write(txt + "\n")
        with open(out_base + ".json", "w") as f:
            json.dump(probe.data, f, indent=1)
        try:
            os.remove(probe.state_path)
        except OSError:
            pass
        print("\n" + txt)
        print(f"\nmemprobe: wrote {out_base}.txt and .json")
    return 0 if reason == "done" else 1


if __name__ == "__main__":
    sys.exit(main())
