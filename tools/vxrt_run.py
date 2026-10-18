#!/usr/bin/env python3
"""vxrt run: starts a game on VXRuntime the way a player would (scripts/launch_vxrt_alyx.sh or launch_vxrt_game.sh,
with SteamVR and the encoder), but in roots of its own and without a headset (a simulated head pose and a trigger
pull every few seconds), measures it, and writes what it saw as evidence: runs/<run id>/result.json, sealed into
runs/ledger.jsonl.

    tools/vxrt_run.py thelab                       # a game's default scenario (see --list)
    tools/vxrt_run.py alyx chapter --quality 3k     # a scenario, with a quality preset
    tools/vxrt_run.py 450390 --seconds 90 --json   # by Steam app id; the result on stdout
    tools/vxrt_run.py --list | --check alyx | --validate runs/<id> | --verify-ledger

The result keeps apart what was observed (each value with the file it comes from and that file's SHA-256), what is
derived from observations (only by rule, against the game profile, games/<app id>.json), what is inferred (marked as a suspicion,
with the observations it rests on) and what is not known. A run that cannot be a fair measurement is refused before
it starts, with a typed reason (the user is playing, SteamVR is up, the roots are the user's, ...).

Exit status: 0 pass, 1 fail, 2 no baseline to compare with, 3 refused, 4 error.
"""
import socket, stat, tempfile
import argparse, json, math, mmap, os, re, shutil, signal, statistics, struct, subprocess, sys, threading, time, zlib
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vxrt_doctor as doctor                                    # noqa: E402
import vxrt_ledger as ledger                                    # noqa: E402
import vxrt_games as profiles_db                                # noqa: E402

REPO = doctor.REPO
DRIVE = os.path.expanduser(os.environ.get("VERANTYX_VR_DRIVE", "~/Verantyx_VR_Drive"))
USER_ROOTS = {os.path.join(DRIVE, "vxrt_root"), os.path.join(DRIVE, "vxrt_steam_overlay")}   # the player's: never used
HARNESS = os.path.join(DRIVE, "vxrt_harness")                  # this tool's roots (kept between runs: game settings, caches)
# Settings a run may set (AGENTS.md, "Settings you may tune"); recorded in the result. Anything else is refused.
TUNABLES = {"VXRT_VR_RENDER_SCALE", "VERANTYX_DISPLAY_SCALE", "VERANTYX_DISPLAY_HZ", "VERANTYX_STREAM_WIDTH", "VERANTYX_STREAM_HEIGHT",
            "VERANTYX_BITRATE_MBPS", "VERANTYX_HEVC_QUALITY", "VXRT_MSAA_MAX", "VXRT_MAX_ANISOTROPY", "VXRT_VR_SETTINGS",
            "VERANTYX_DECOUPLED_POSES", "VXRT_GPU_PROFILE", "VXRT_STATS", "VXRT_TRACE_CALLS"}

# Separate from tunables: these values can only restrict diagnostic networking/audio.
SAFETY_ENV = {"VERANTYX_LOOPBACK_ONLY": "1", "VERANTYX_AUDIO": "0", "VERANTYX_ALVR": "0", "VXRT_MUTE": "1"}

def safety_environment(values):
    values = dict(values or {})
    bad = [k for k, v in values.items() if k not in SAFETY_ENV or v != SAFETY_ENV[k]]
    if bad:
        raise ValueError("unsupported safety environment setting: " + ", ".join(sorted(bad)))
    return values

def network_diagnostic_config(enabled):
    if type(enabled) is not bool:
        raise ValueError("network_diagnostic must be a boolean")
    return {"enabled": enabled, "environment": {"VXRT_NET_DIAG": "1"} if enabled else {},
            "max_records_per_process": 128 if enabled else 0}

# Games the harness knows (games/<app id>.json, by key): how they start, the scenarios, the baselines. Other installed
# games are run with a default profile (the generic launcher, scenario "start").
def profiles():
    return {p["key"]: p for p in profiles_db.load_all().values()}

# ---- what is installed ------------------------------------------------------------------------------------------------
def installed():
    import vxrt_studio                                          # (game discovery lives there)
    return {g["appid"]: g for g in vxrt_studio.find_games()}

def resolve(game):
    """(key, profile, installed game) for a profile's key or a Steam app id."""
    inst = installed()
    p = profiles_db.by_key(str(game))
    if p:
        game_install = inst.get(p["appid"])
        if game_install and "exe" in p:
            path = profiles_db.executable(p, game_install["folder"])
            game_install = dict(game_install, exe=path) if path else None
        return p["key"], p, game_install
    if game in inst:
        return game, profiles_db.default_profile(game, inst[game]["name"]), inst[game]
    return game, None, None

# ---- preconditions ----------------------------------------------------------------------------------------------------
def pgrep(pattern):
    r = subprocess.run(["pgrep", "-fl", pattern], capture_output=True, text=True)
    return [line for line in r.stdout.splitlines() if line.strip()]

def listening(port):
    return subprocess.run(["lsof", "-nP", "-iTCP:%d" % port, "-sTCP:LISTEN"], capture_output=True).returncode == 0

def preconditions(spec, game, args, root, overlay, need_steam=True):
    """Checks that make a run a fair measurement; each failed one is a typed refusal."""
    checks = []
    def check(code, ok, detail):
        checks.append({"code": code, "ok": bool(ok), "detail": detail})
    check("game_installed", spec and game, "found in a Steam library" if game else "not installed (tools/vxrt_steam.py fetch or import) or unknown")
    busy = pgrep("[b]uild/vxrt") + pgrep("[w]ine.*(vrserver|hlvr)")
    check("user_not_playing", not busy, "no VXRuntime or Wine game/SteamVR running" if not busy else "running: " + "; ".join(b[:120] for b in busy[:3]))
    check("steamvr_not_running", not listening(27062), "port 27062 free" if not listening(27062) else "a SteamVR is listening on 27062")
    steam = pgrep("[S]team.AppBundle/Steam/Contents/MacOS/steam_osx")
    check("steam_running", steam or not need_steam, "Mac Steam is running" if steam else "Mac Steam is not running (sign in there first)")
    isolated = os.path.realpath(root) not in {os.path.realpath(p) for p in USER_ROOTS} and os.path.realpath(overlay) not in {os.path.realpath(p) for p in USER_ROOTS}
    check("roots_isolated", isolated, f"root {root}, overlay {overlay}")
    novr = [a for a in args if a.lower() in ("-novr", "+vr_enable", "-vr_disable")]
    check("vr_mode", not novr, "the game starts in VR" if not novr else "arguments turn VR off: " + " ".join(novr))
    return checks

# ---- the simulated headset ------------------------------------------------------------------------------------------
SLOT = 88 + 4096 * 4096 * 8                                     # one frame slot of the shared file (driver_verantyx.cpp)
HANDS = 64 + 2 * SLOT                                           # struct SharedHands, after the two slots

def feed_pose(path, stop, press_every, controllers=True):
    """A slowly turning head and two hands, tracked, written into the VeRantyx shared frame file as the Mac encoder
    does with the Vision Pro's tracking; the right trigger is pulled for half a second every press_every seconds
    (SteamVR gives a game scene focus only after input)."""
    while not stop.is_set() and (not os.path.exists(path) or os.path.getsize(path) < HANDS + 300):
        stop.wait(0.5)
    if stop.is_set():
        return
    with open(path, "r+b") as f:
        m = mmap.mmap(f.fileno(), 0)
        start = time.time()
        while not stop.is_set():
            seq = struct.unpack_from("<i", m, 20)[0]
            struct.pack_into("<i", m, 20, seq + 1 if seq % 2 == 0 else seq)          # odd: being written
            t = time.time()
            a = 0.15 * math.sin(t * 1.3)
            struct.pack_into("<16f", m, HANDS, math.cos(a), 0, -math.sin(a), 0, 0, 1, 0, 0, math.sin(a), 0, math.cos(a), 0, 0.02 * math.sin(t), 1.6, 0, 1)
            struct.pack_into("<16f", m, HANDS + 64, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.2, 1.2, -0.4, 1)
            struct.pack_into("<16f", m, HANDS + 128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.2, 1.2, -0.4, 1)
            m[HANDS + 192] = int(controllers); m[HANDS + 193] = int(controllers)
            pressing = controllers and press_every > 0 and t - start > press_every and (t - start) % press_every < 0.5
            if not controllers:
                # Clear stale hand buttons/axes from prior runs as well as tracking flags.
                m[HANDS + 194:HANDS + 244] = bytes(50)
            m[HANDS + 195] = 255 if pressing else 0
            struct.pack_into("<d", m, HANDS + 244, time.monotonic())
            m[HANDS + 286] = 1
            struct.pack_into("<i", m, 20, struct.unpack_from("<i", m, 20)[0] + 1)    # even: done
            stop.wait(0.011)
        m.close()

def grab_frame(path, out):
    """The driver's latest frame (both eyes, as SteamVR handed it over) as a PNG; returns (width, height, mean
    luminance 0-255, fraction of pixels brighter than 16) or None."""
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        idx = struct.unpack_from("<i", m, 0)[0]
        base = 64 + max(idx, 0) * SLOT
        _, w, h, fmt = struct.unpack_from("<IIII", m, base)
        if not (0 < w <= 8192 and 0 < h <= 8192):
            return None
        px = bytes(m[base + 88: base + 88 + w * h * 4])
        m.close()
    bgra = fmt == 87
    rows = []
    for y in range(h):
        r = px[y * w * 4:(y + 1) * w * 4]
        if bgra:
            a = bytearray(r); a[0::4], a[2::4] = r[2::4], r[0::4]; r = bytes(a)
        rows.append(b"\0" + r)
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(out, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))
    lum, bright, n = 0, 0, 0
    for i in range(0, len(px), 4 * 97):                         # (a sample of pixels)
        l = (px[i] * 54 + px[i + 1] * 183 + px[i + 2] * 19) >> 8
        lum += l; bright += l > 16; n += 1
    return w, h, lum / max(n, 1), bright / max(n, 1)

def stream_client(stop, frames, dump=None, dump_cap=300 << 20):
    """Stands in for the Vision Pro app: says HELLO_VR to the encoder (UDP 9999 on this Mac) and records when each
    frame's last fragment arrives (frames: (time, sequence)). Over loopback, not Wi-Fi: it shows what the Mac sends,
    and how evenly, not what the radio does."""
    import socket
    sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    sock.settimeout(0.5)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    parts, last_hello, got = {}, 0.0, False
    chunks, out, written = {}, open(dump, "wb") if dump else None, 0    # (dump: the HEVC stream as received, Annex B)
    while not stop.is_set():
        if time.time() - last_hello > (5 if got else 1):
            try: sock.sendto(b"HELLO_VR", ("::1", 9999))
            except OSError: pass
            last_hello = time.time()
        try:
            data, _ = sock.recvfrom(65536)
        except (socket.timeout, OSError):
            continue
        if len(data) < 24 or data[:4] != b"NARV":                  # (magic "VRAN", little-endian)
            continue
        got = True
        seq, idx, total, offset, size = struct.unpack_from("<IIIII", data, 4)
        seen = parts.setdefault(seq, set())
        seen.add(idx)
        if out: chunks.setdefault(seq, {})[offset] = data[96:96 + size]
        if len(seen) == total:
            frames.append((time.time(), seq))
            del parts[seq]
            if out:
                frame = b"".join(v for _, v in sorted(chunks.pop(seq).items()))
                if written + len(frame) <= dump_cap: out.write(frame); written += len(frame)
            for old in [k for k in parts if k < seq - 120]: del parts[old]
            for old in [k for k in chunks if k < seq - 120]: del chunks[old]
    sock.close()
    if out: out.close()

def sample_gpu(stop, samples):
    while not stop.is_set():
        out = subprocess.run(["ioreg", "-r", "-d1", "-c", "IOAccelerator"], capture_output=True, text=True).stdout
        m = re.search(r'"Device Utilization %"=(\d+)', out)
        if m:
            samples.append((time.time(), int(m.group(1))))
        stop.wait(2)

# ---- reading what happened --------------------------------------------------------------------------------------------
def encoder_drop_delta(samples, window_s):
    if len(samples) < 2:
        return None
    # Early exits can leave fewer samples than the requested measurement window.
    count = min(len(samples), max(2, int(window_s / 5)))
    return samples[-1] - samples[-count]


def encoder_fps(path):
    if not os.path.exists(path):
        return []
    return [float(x) for x in re.findall(r"capture=([\d.]+)fps", open(path, errors="replace").read())]

def proxy_lines(root_c, since):
    """The capture proxy's lines (Half-Life: Alyx's openvr_api.dll) written since the run started."""
    path = os.path.join(root_c, "vr_openvr_capture_proxy.log")
    out = []
    if not os.path.exists(path):
        return out
    with open(path, errors="replace") as f:
        f.seek(max(0, os.path.getsize(path) - 4_000_000))
        for line in f:
            m = re.match(r"(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) ", line)
            if m and time.mktime(time.strptime(m.group(1), "%Y-%m-%d %H:%M:%S")) >= since - 1:
                out.append(line)
    return out

def connected_at(vrserver_log, appid, since):
    """When vrserver saw the game connect as steam.app.<appid>, in seconds after the start, or None."""
    if not os.path.exists(vrserver_log):
        return None
    for line in open(vrserver_log, errors="replace"):
        if f"appkey=steam.app.{appid} " in line or line.rstrip().endswith(f"appkey=steam.app.{appid}"):
            m = re.match(r"\w{3} (\w{3} \d+ \d{4} \d\d:\d\d:\d\d)", line)
            t = time.mktime(time.strptime(m.group(1), "%b %d %Y %H:%M:%S")) if m else None
            if t is None or t >= since - 1:
                return round(t - since, 1) if t else 0.0
    return None

def input_bindings(vrserver_log, appid, since):
    """What SteamVR set up for the game's controllers in this run: the binding files it loaded for steam.app.<appid>
    (per controller type) and whether the game declared SteamVR Input actions. None without the log."""
    if not os.path.exists(vrserver_log):
        return None
    app, files, manifest = f"steam.app.{appid}", [], False
    for line in open(vrserver_log, errors="replace"):
        m = re.match(r"\w{3} (\w{3} \d+ \d{4} \d\d:\d\d:\d\d)", line)
        if not m or time.mktime(time.strptime(m.group(1), "%b %d %Y %H:%M:%S")) < since - 1:
            continue
        b = re.search(r"Successfully loaded binding file '([^']+)' for app '([^']+)'", line)
        if b and b.group(2) == app:
            files.append(b.group(1).replace("\\", "/").rsplit("/", 1)[-1])
        if "SetActionManifestPath" in line and app in line:
            manifest = True
    return {"binding_files": sorted(set(files)), "uses_steamvr_input_actions": manifest}

# How far a game got, lowest to highest. "playable" is never derived from a run: only a person in the headset says so
# (tools/vxrt_playtest.py).
LEVELS = ["starts", "connects", "draws", "runs", "controls", "playable"]

def level(ev):
    """The highest level the run's observations show (None: it did not even start)."""
    reached = None
    checks = [("starts", not ev.value("exited_early") or ev.value("connected_after_s") is not None),
              ("connects", ev.value("connected_after_s") is not None),
              ("draws", (ev.value("frame_bright_fraction") or 0) > 0.05),
              ("runs", not ev.value("exited_early") and ev.value("fps_median") not in (None, 0) and not ev.value("unhandled_exceptions")),
              ("controls", bool((ev.value("controller_bindings") or {}).get("binding_files")))]
    for name, ok in checks:
        if not ok:
            break
        reached = name
    return reached

def game_log_facts(path, exe):
    """Counts from the game's own lines of its log (not SteamVR's processes it started)."""
    facts = {"unhandled": 0, "signals": 0, "unimplemented": {}, "managed_exceptions": 0, "first_problem": None, "exit": None}
    if not path or not os.path.exists(path):
        return facts
    for n, line in enumerate(open(path, errors="replace"), 1):
        other = re.match(r"\[vxrt ([^\]:]+):\d+\]", line)
        if other and other.group(1).lower() != exe.lower():
            continue
        problem = None
        if "unhandled exception" in line or "unwind did not reach" in line:
            facts["unhandled"] += 1; problem = line
        elif re.search(r"signal \d+ at ", line):
            facts["signals"] += 1; problem = line
        elif "called an unimplemented function:" in line:
            fn = line.split("called an unimplemented function:", 1)[1].strip()
            facts["unimplemented"][fn] = facts["unimplemented"].get(fn, 0) + 1; problem = line
        elif re.match(r"[A-Za-z_][\w.]*Exception: ", line):
            facts["managed_exceptions"] += 1
            if facts["managed_exceptions"] == 1: problem = line
        elif re.search(r"MessageBox: Network error: Unable to initialise network\. Please check your connection\.$", line.rstrip()):
            problem = line
        m = re.search(r"(ExitProcess\(\d+\) from \S+)", line)
        if m: facts["exit"] = m.group(1)
        if problem and not facts["first_problem"]:
            facts["first_problem"] = {"line": n, "text": problem.strip()[:300]}
    return facts

def median(xs):
    return round(statistics.median(xs), 1) if xs else None

# ---- the evidence ---------------------------------------------------------------------------------------------------------
def rel(path):
    return os.path.relpath(path, REPO)

class Evidence:
    def __init__(self):
        self.observed, self.derived, self.inferred, self.unknown, self.limits = [], [], [], [], []
    def observe(self, metric, value, artifact=None, **extra):
        item = {"id": "o%d" % (len(self.observed) + 1), "metric": metric, "value": value}
        if artifact and os.path.exists(artifact):
            item["artifact"] = rel(artifact); item["sha256"] = ledger.sha256_file(artifact)
        item.update(extra)
        self.observed.append(item)
        return item["id"]
    def ref(self, metric):
        return next((o["id"] for o in self.observed if o["metric"] == metric), None)
    def value(self, metric):
        return next((o["value"] for o in self.observed if o["metric"] == metric), None)

def baseline_for(key, scenario, quality):
    """(name, baseline, its place) from the game's profile, or (None, None, None)."""
    p = profiles_db.by_key(str(key))
    table = p["baselines"] if p else {}
    name = f"{scenario}@{quality}" if quality else scenario
    return (name, table[name], f"games/{p['appid']}.json#{name}") if name in table else (None, None, None)

def derive(ev, base_name, base, src=None):
    """Claims that follow from observations by rule, each naming the observations and the baseline it used. Reaching
    SteamVR and running the whole scenario are asked of every game, with a baseline or not."""
    base = base or {}
    src = src or "tools/vxrt_run.py (every game)"
    fps = ev.value("fps_median")
    if "min_fps" in base:
        ev.derived.append({"id": "d1", "claim": "fps_meets_baseline", "holds": fps is not None and fps >= base["min_fps"],
                           "rule": f"fps_median >= {base['min_fps']}", "from": [ev.ref("fps_median")], "baseline": src})
    if base.get("non_black"):
        ev.derived.append({"id": "d2", "claim": "frame_not_black", "holds": bool(ev.value("frame_bright_fraction") and ev.value("frame_bright_fraction") > 0.05),
                           "rule": "frame_bright_fraction > 0.05", "from": [ev.ref("frame_bright_fraction")], "baseline": src})
    if base.get("connected", True):
        ev.derived.append({"id": "d3", "claim": "game_connected_to_steamvr", "holds": ev.value("connected_after_s") is not None,
                           "rule": "connected_after_s is not null", "from": [ev.ref("connected_after_s")], "baseline": src})
    ev.derived.append({"id": "d4", "claim": "ran_for_the_whole_scenario", "holds": not ev.value("exited_early"),
                       "rule": "exited_early is false", "from": [ev.ref("exited_early")], "baseline": src})
    if ev.ref("controller_bindings") and ev.value("connected_after_s") is not None:
        ev.derived.append({"id": "d5", "claim": "controller_bindings_loaded", "holds": bool((ev.value("controller_bindings") or {}).get("binding_files")),
                           "rule": "SteamVR loaded a binding file for the game's controllers", "from": [ev.ref("controller_bindings")], "baseline": src})

def infer(ev):
    """Suspicions, never facts: each says what it rests on and how sure it is."""
    def add(claim, basis, refs, confidence="low"):
        ev.inferred.append({"id": "i%d" % (len(ev.inferred) + 1), "claim": claim, "basis": basis, "from": [r for r in refs if r], "confidence": confidence})
    fps, gpu = ev.value("fps_median"), ev.value("gpu_utilization_median")
    failed = {d["claim"] for d in ev.derived if not d["holds"]}
    if "fps_meets_baseline" in failed and gpu is not None:
        if gpu >= 90:
            add("GPU-bound", f"fps {fps} below baseline with the GPU {gpu}% busy", [ev.ref("fps_median"), ev.ref("gpu_utilization_median")])
        elif gpu < 60:
            add("limited by the CPU, synchronisation or pacing rather than the GPU", f"fps {fps} below baseline with the GPU only {gpu}% busy",
                [ev.ref("fps_median"), ev.ref("gpu_utilization_median")])
    if ev.value("connected_after_s") is None and ev.value("exited_early"):
        add("the game stopped before it reached SteamVR", "it exited early and vrserver never saw it connect; see first_problem",
            [ev.ref("exited_early"), ev.ref("connected_after_s"), ev.ref("first_problem")], "medium")
    if ev.value("connected_after_s") is not None and "frame_not_black" in failed:
        add("the game connects but nothing it draws reaches the compositor (rendering or texture sharing)", "connected, frame black",
            [ev.ref("connected_after_s"), ev.ref("frame_bright_fraction")])
    if (ev.value("managed_exceptions") or 0) > 100:
        add("the game's scripts fail every frame; the first exception is the one to look at", f"{ev.value('managed_exceptions')} managed exceptions",
            [ev.ref("managed_exceptions"), ev.ref("first_problem")], "medium")

def validate(result):
    """Problems with a result: claims resting on observations that are not there, derived claims that do not follow
    from the recorded values, artifacts that changed. An empty list: valid."""
    problems = []
    obs = {o["id"]: o for o in result.get("observed", [])}
    for kind in ("derived", "inferred"):
        for item in result.get(kind, []):
            refs = item.get("from") or []
            if not refs or any(r not in obs for r in refs):
                problems.append(f"{kind} {item.get('id')} ({item.get('claim')}) rests on no recorded observation: {refs}")
    for o in obs.values():
        if o.get("artifact") and o.get("sha256"):
            p = os.path.join(REPO, o["artifact"])
            if not os.path.exists(p) or ledger.sha256_file(p) != o["sha256"]:
                problems.append(f"observation {o['id']} ({o['metric']}): {o['artifact']} changed or gone")
    # Recorded values against what they come from: a median against its samples, the samples against the file.
    for o in obs.values():
        if o["metric"] == "fps_median" and o.get("samples") is not None:
            if median(o["samples"]) != o["value"]:
                problems.append(f"observation {o['id']} (fps_median): {o['value']} is not the median of its samples ({median(o['samples'])})")
            p = os.path.join(REPO, o.get("artifact", ""))
            text = open(p, errors="replace").read() if o.get("artifact") and os.path.exists(p) else ""
            recorded = {float(x) for x in re.findall(r"(?:capture|submit_fps)=([\d.]+)", text)}
            missing = [x for x in o["samples"] if float(x) not in recorded]
            if missing:
                problems.append(f"observation {o['id']} (fps_median): samples not in {o.get('artifact')}: {missing[:5]}")
    # Re-derive: the same rules over the recorded values must give the same answers.
    if result.get("observed"):
        ev = Evidence(); ev.observed = list(obs.values())
        name, base, src = baseline_for(result["game"], result["scenario"], result.get("quality"))
        if base or not result.get("baseline"):
            derive(ev, name, base, src)
            again = {d["claim"]: d["holds"] for d in ev.derived}
            for d in result.get("derived", []):
                if d["claim"] in again and again[d["claim"]] != d["holds"]:
                    problems.append(f"derived {d['id']} ({d['claim']}) says {d['holds']}, the recorded values give {again[d['claim']]}")
        if "level" in result and level(ev) != result["level"]:
            problems.append(f"level says {result['level']}, the recorded values give {level(ev)}")
    return problems

# ---- a run ----------------------------------------------------------------------------------------------------------------------
def git_state():
    head = subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "-C", REPO, "status", "--porcelain", "--untracked-files=no"], capture_output=True, text=True).stdout.strip())
    return head, dirty

# Restricted diagnostics use a new protocol, never the text/action Clip client.
def control_environment(env):
    return {k: v for k, v in env.items() if not k.startswith("VXRT_CONTROL")}

RENDER_APIS = ("D3D11CreateDevice", "D3D11CreateDeviceAndSwapChain", "IDXGIFactory.CreateSwapChain",
               "IDXGIFactory2.CreateSwapChainForHwnd", "IDXGISwapChain.Present", "IDXGISwapChain1.Present1")
RENDER_STAGES = ("present_test", "present_no_drawable", "present_drawable_committed", "d3d12_delegate")

RENDER_ACTIVITY = ("work_commit_main", "work_commit_pre", "window_callback_entered", "window_callback_completed")

def validate_render_counters(value):
    if (not isinstance(value, dict) or value.get("scope") != "selected_process_including_loaded_dlls"
        or value.get("openvr_submit") != "not_instrumented" or type(value.get("overflow")) is not bool
        or not isinstance(value.get("apis"), list) or len(value["apis"]) != len(RENDER_APIS)):
        return None
    integer = lambda n: type(n) is int and 0 <= n <= 0xffffffffffffffff
    if any(not integer(value.get(k)) for k in RENDER_STAGES + RENDER_ACTIVITY):
        return None
    valid, cpu = value.get("process_cpu_valid"), value.get("process_cpu_ns")
    if type(valid) is not bool or (valid and not integer(cpu)) or (not valid and cpu is not None):
        return None
    apis = []
    for api, row in zip(RENDER_APIS, value["apis"]):
        if not isinstance(row, dict) or row.get("api") != api:
            return None
        keys = ("entered", "completed", "success", "failure")
        if any(not integer(row.get(k)) for k in keys):
            return None
        hr = row.get("last_hresult")
        if hr is not None and (type(hr) is not int or not 0 <= hr <= 0xffffffff):
            return None
        apis.append(dict(api=api, last_hresult=hr, **{k: row[k] for k in keys}))
    return dict(scope=value["scope"], openvr_submit="not_instrumented", overflow=value["overflow"],
                apis=apis, process_cpu_valid=valid, process_cpu_ns=cpu,
                **{k: value[k] for k in RENDER_STAGES + RENDER_ACTIVITY})

class WindowMetadata:
    schedule = (30, 40, 50)

    def __init__(self, enabled, render=False):
        if type(render) is not bool:
            raise ValueError("render_diagnostic must be a boolean")
        self.window_enabled, self.render_enabled = enabled, render
        self.enabled = enabled or render
        if type(enabled) is not bool:
            raise ValueError("window_metadata must be a boolean")
        self.config = {"enabled": enabled, "mode": "metadata_only" if enabled else "off",
                       "read_only": True, "text_capture": False, "max_samples": 3 if enabled else 0,
                       "schedule_seconds": list(self.schedule) if enabled else [],
                       "target_selector": "exact_realpath_main_process", "child_excluded": True,
                       "request_count": 0, "success_count": 0, "status": "not_started" if enabled else "off"}
        self.render_config = dict(self.config, enabled=render, mode="render_counters" if render else "off",
                                  max_samples=3 if render else 0, schedule_seconds=list(self.schedule) if render else [],
                                  scope="selected_process_including_loaded_dlls", openvr_submit="not_instrumented",
                                  status="not_started" if render else "off")
        self.directory = self.path = None
        self.socket_identity = self.directory_identity = None
        self.rows = []

    def prepare(self, env, target):
        if not self.enabled:
            return
        self.directory = tempfile.mkdtemp(prefix="vxclip-", dir="/private/tmp")
        self.path = os.path.join(self.directory, "control.sock")
        st = os.lstat(self.directory)
        self.directory_identity = (st.st_dev, st.st_ino)
        mode = "metadata_render" if self.window_enabled and self.render_enabled else "render" if self.render_enabled else "metadata"
        env.update(VXRT_CONTROL_DIAG=mode, VXRT_CONTROL_TARGET=os.path.realpath(target),
                   VXRT_CONTROL_DIAG_SOCKET=self.path)
        for config in (self.config, self.render_config):
            if config["enabled"]:
                config["status"] = "prepared"

    def owns_directory(self):
        try:
            st = os.lstat(self.directory)
            return (stat.S_ISDIR(st.st_mode) and (st.st_dev, st.st_ino) == self.directory_identity
                    and os.path.realpath(self.directory) == self.directory)
        except (OSError, TypeError):
            return False

    def remember_socket(self):
        if self.socket_identity is not None or not self.path or not self.owns_directory():
            return
        try:
            st = os.lstat(self.path)
            if stat.S_ISSOCK(st.st_mode) and st.st_uid == os.getuid():
                self.socket_identity = (st.st_dev, st.st_ino)
        except OSError:
            pass

    def sample(self):
        # Never persist arbitrary server fields, even when a stale/malicious endpoint replies.
        counts = ("total_windows", "visible_windows", "enabled_windows", "child_windows", "minimized_windows")
        deadline = time.monotonic() + 2
        if not self.owns_directory():
            return {"status": "identity_mismatch"}
        try:
            st = os.lstat(self.path)
            if not stat.S_ISSOCK(st.st_mode) or st.st_uid != os.getuid():
                return {"status": "unavailable"}
            identity = (st.st_dev, st.st_ino)
            if self.socket_identity is not None and identity != self.socket_identity:
                return {"status": "identity_mismatch"}
            self.socket_identity = identity
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(max(.001, deadline - time.monotonic()))
                client.connect(self.path)
                client.sendall(b"metadata\n")
                data = b""
                while b"\n" not in data:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError()
                    client.settimeout(remaining)
                    chunk = client.recv(4096 - len(data))
                    if not chunk or len(data) + len(chunk) >= 4096:
                        return {"status": "invalid_response"}
                    data += chunk
                value = json.loads(data.split(b"\n", 1)[0])
                if (not isinstance(value, dict) or value.get("status") != "ok" or value.get("schema_version") != 1
                    or type(value.get("pid")) is not int or value["pid"] <= 0
                    or (self.window_enabled and any(type(value.get(k)) is not int or not 0 <= value[k] <= 16384 for k in counts))):
                    return {"status": "invalid_response"}
                row = {"status": "ok", "pid": value["pid"]}
                if self.window_enabled:
                    row.update(error_category="unknown", **{k: value[k] for k in counts})
                if self.render_enabled:
                    render = validate_render_counters(value.get("render"))
                    if render is None:
                        return {"status": "invalid_response"}
                    row["render"] = render
                return row
        except (TimeoutError, socket.timeout):
            return {"status": "timeout"}
        except (OSError, ValueError, TypeError):
            return {"status": "unavailable"}

    def collect(self, stop, started):
        for index, offset in enumerate(self.schedule, 1):
            while time.monotonic() < started + offset and not stop.is_set():
                self.remember_socket()  # identity only; no extra state requests
                stop.wait(min(.1, max(0, started + offset - time.monotonic())))
            self.remember_socket()
            if stop.is_set():
                break
            row = self.sample()
            row.update(sample_index=index, elapsed_seconds=round(time.monotonic() - started, 3))
            self.rows.append(row)
        for config in (self.config, self.render_config):
            if config["enabled"]:
                config.update(request_count=len(self.rows), success_count=sum(r["status"] == "ok" for r in self.rows),
                              status="completed" if len(self.rows) == 3 else "stopped")

    def cleanup(self):
        # No recursive removal: an unexpected replacement is left alone.
        if self.path and self.owns_directory():
            try:
                st = os.lstat(self.path)
                if stat.S_ISSOCK(st.st_mode) and (st.st_dev, st.st_ino) == self.socket_identity:
                    # Only remove the endpoint this sampler actually observed.
                    os.unlink(self.path)
            except OSError:
                pass
        if self.directory:
            try:
                st = os.lstat(self.directory)
                if (st.st_dev, st.st_ino) == self.directory_identity and stat.S_ISDIR(st.st_mode):
                    os.rmdir(self.directory)
            except OSError:
                pass

    def seal(self, run_dir, evidence):
        for config, metric in ((self.config, "window_metadata"), (self.render_config, "render_diagnostic")):
            if not config["enabled"]:
                continue
            if metric == "window_metadata":
                rows = [{k: v for k, v in row.items() if k != "render"} for row in self.rows]
            else:
                rows = [{k: v for k, v in row.items() if k in ("status", "pid", "sample_index", "elapsed_seconds", "render")} for row in self.rows]
            path = os.path.join(run_dir, metric + ".jsonl")
            with open(path, "w") as output:
                for row in rows:
                    output.write(json.dumps(row, sort_keys=True) + "\n")
            evidence.observe(metric, rows, path, note="fixed read-only diagnostic; not application frame or authentication evidence")
            config["artifact"] = rel(path)

def run(game, scenario=None, quality="", seconds=None, extra_args=(), press_every=15.0, root=None, overlay=None,
        trace=False, need_steam=True, quiet=False, settings=None, stream=False, safety_env=None, network_diagnostic=False, window_metadata=False, render_diagnostic=False):
    windows = WindowMetadata(window_metadata, render_diagnostic)
    diagnostic = network_diagnostic_config(network_diagnostic)
    safety_env = safety_environment(safety_env)
    key, spec, inst = resolve(game)
    scenario = scenario or (spec or {}).get("default_scenario", "start")
    sc = (spec or {}).get("scenarios", {}).get(scenario)
    input_mode = (sc or {}).get("input", "simulated_controllers")
    if input_mode == "keyboard_mouse":
        press_every = 0
    args = list((sc or {}).get("args", [])) + list(extra_args)
    seconds = seconds or (sc or {}).get("seconds", 150)
    root = root or os.path.join(HARNESS, "root")
    overlay = overlay or os.path.join(HARNESS, "overlay")
    started = time.time()
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + f"-{key}-{scenario}" + (f"-{quality}" if quality else "")
    run_dir = os.path.join(REPO, "runs", run_id)
    logs = os.path.join(run_dir, "logs")
    os.makedirs(logs, exist_ok=True)
    head, dirty = git_state()
    result = {"run_id": run_id, "game": key, "appid": (spec or {}).get("appid"), "scenario": scenario, "quality": quality or None,
              "arguments": args, "seconds": seconds, "started": datetime.fromtimestamp(started, timezone.utc).isoformat(),
              "environment": {"commit": head, "uncommitted_changes": dirty, "root": root, "overlay": overlay,
                              "machine": doctor.machine_info(), "python": sys.version.split()[0],
                              # Parent snapshot; network_diagnostic below replaces inherited VXRT_NET_DIAG.
                              "inherited": {k: v for k, v in control_environment(os.environ).items() if k.startswith(("VXRT_", "VERANTYX_"))}}}
    say = (lambda *a: None) if quiet else (lambda *a: print(*a, file=sys.stderr))

    settings = dict((spec or {}).get("settings", {}), **(settings or {}))      # (the profile's defaults, then the run's)
    result["settings"] = settings
    result["network_diagnostic"] = diagnostic
    result["window_diagnostic"] = windows.config
    result["render_diagnostic"] = windows.render_config
    result["input_mode"] = input_mode
    result["safety_environment"] = dict({"VXRT_MUTE": "1", "VERANTYX_AUDIO": "0"}, **safety_env)
    checks = preconditions(spec, inst, args, root, overlay, need_steam)
    bad = sorted(set(settings) - TUNABLES)
    checks.append({"code": "settings_allowed", "ok": not bad, "detail": "only AGENTS.md's tunables" if not bad else "not tunable: " + ", ".join(bad)})
    result["preconditions"] = checks
    if sc is None and spec:
        checks.append({"code": "scenario_known", "ok": False, "detail": f"{scenario}: not one of {sorted(spec['scenarios'])}"})
    if not all(c["ok"] for c in checks):
        result["status"] = "refused"
        result["refused"] = [c["code"] for c in checks if not c["ok"]]
        return finish(result, run_dir, say)

    build = subprocess.run(["make", "-C", os.path.join(REPO, "VXRuntime"), "build/vxrt"], capture_output=True, text=True)
    if build.returncode:
        open(os.path.join(logs, "build.log"), "w").write(build.stdout + build.stderr)
        result["status"] = "refused"; result["refused"] = ["vxrt_builds"]
        result["preconditions"].append({"code": "vxrt_builds", "ok": False, "detail": "make failed: logs/build.log"})
        return finish(result, run_dir, say)

    env = dict(control_environment(os.environ), VXRT_VR_ROOT=root, VXRT_VR_OVERLAY=overlay, VXRT_VR_LOG_DIR=logs, VXRT_MUTE="1", VERANTYX_AUDIO="0")
    # Only the explicit fixed diagnostic option may enable this runtime logging.
    env.pop("VXRT_NET_DIAG", None)
    env.update(diagnostic["environment"])
    if quality: env["VXRT_VR_QUALITY"] = quality
    else: env.pop("VXRT_VR_QUALITY", None)
    if trace: env["VXRT_TRACE"] = "1"
    env.update({k: str(v) for k, v in settings.items()})
    env.update(result["safety_environment"])
    if spec["launcher"] == "alyx":
        cmd = [os.path.join(REPO, "scripts", "launch_vxrt_alyx.sh")] + args
        game_log = os.path.join(logs, "vxrt-alyx.log"); exe = "hlvr.exe"
    else:
        cmd = [os.path.join(REPO, "scripts", "launch_vxrt_game.sh"), inst["exe"]] + args
        exe = os.path.basename(inst["exe"]); game_log = os.path.join(logs, "vxrt-%s.log" % os.path.splitext(exe)[0])
    result["command"] = " ".join(cmd)
    result["exe"] = exe
    windows.prepare(env, inst["exe"])
    shared = os.path.join(root, "drive_c", "vr_shared_frame.dat")
    stop, gpu, streamed = threading.Event(), [], []
    threads = [threading.Thread(target=feed_pose, args=(shared, stop, press_every, input_mode != "keyboard_mouse"), daemon=True),
               threading.Thread(target=sample_gpu, args=(stop, gpu), daemon=True)]
    if stream:
        threads.append(threading.Thread(target=stream_client, args=(stop, streamed, os.path.join(run_dir, "stream.hevc")), daemon=True))
    metadata_thread = None
    if windows.enabled:
        metadata_thread = threading.Thread(target=windows.collect, args=(stop, time.monotonic()), daemon=True)
        threads.append(metadata_thread)
    say(f"vxrt run {run_id}: {result['command']} for {seconds} s")
    try:
        with open(os.path.join(logs, "launcher.log"), "w") as out:
            p = subprocess.Popen(cmd, env=env, stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
    except BaseException:
        windows.cleanup()
        raise
    for t in threads: t.start()
    exited_at = None
    try:
        while time.time() - started < seconds:
            if p.poll() is not None:
                exited_at = time.time(); break
            time.sleep(1)
        frame = grab_frame(shared, os.path.join(run_dir, "frame_end.png"))
        fps_capture = encoder_fps(os.path.join(logs, "hardware-encoder.log"))
        proxy = proxy_lines(os.path.join(root, "drive_c"), started)
    finally:
        stop.set()
        if metadata_thread:
            metadata_thread.join()  # bounded socket deadline; no writer survives evidence sealing
        windows.cleanup()
        try:
            os.killpg(p.pid, signal.SIGTERM)                  # the launcher's cleanup stops the game and SteamVR
            p.wait(30)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            pass
        try: os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        time.sleep(1)
        for line in pgrep("[U]nityCrashHandler64.exe --attach"):   # (none ran before: the precondition)
            os.kill(int(line.split()[0]), signal.SIGKILL)
    ended = time.time()

    ev = Evidence()
    vrlog = os.path.join(overlay, "logs", "vrserver.txt")
    if os.path.exists(vrlog): shutil.copy(vrlog, os.path.join(logs, "vrserver.txt"))
    proxy_log = os.path.join(logs, "capture_proxy.log")
    if proxy: open(proxy_log, "w").writelines(proxy)
    gpu_csv = os.path.join(run_dir, "gpu.csv")
    open(gpu_csv, "w").write("".join(f"{t - started:.1f},{v}\n" for t, v in gpu))

    window = max(10.0, min(60.0, seconds / 2))                # measured: the last part of the run (after loading)
    submit = [float(x) for line in proxy for x in re.findall(r"submit_fps=([\d.]+)", line)]
    recent_capture = fps_capture[-max(1, int(window / 5)):]
    recent_submit = submit[-max(1, int(window / 5)):]
    ev.observe("exited_early", exited_at is not None, os.path.join(logs, "launcher.log"),
               **({"exited_after_s": round(exited_at - started, 1)} if exited_at else {}))
    ev.observe("connected_after_s", connected_at(os.path.join(logs, "vrserver.txt"), spec["appid"], started), os.path.join(logs, "vrserver.txt"))
    ev.observe("controller_bindings", input_bindings(os.path.join(logs, "vrserver.txt"), spec["appid"], started), os.path.join(logs, "vrserver.txt"),
               note="binding files SteamVR loaded for the game's controllers in this run (the VeRantyx controllers are Touch controllers)")
    if recent_submit:
        ev.observe("fps_median", median(recent_submit), proxy_log, source="the game's Submit calls (capture proxy), 5 s averages",
                   samples=recent_submit, window_s=window)
    else:
        ev.observe("fps_median", median(recent_capture), os.path.join(logs, "hardware-encoder.log"),
                   source="frames the encoder took from SteamVR's compositor, 5 s averages", samples=recent_capture, window_s=window)
    ev.observe("capture_fps_series", fps_capture, os.path.join(logs, "hardware-encoder.log"))
    if stream:                                               # what reached the stand-in headset, in the measured window
        frames_csv = os.path.join(run_dir, "streamed_frames.csv")
        open(frames_csv, "w").write("".join(f"{t - started:.4f},{q}\n" for t, q in streamed))
        recent = [(t, q) for t, q in streamed if t >= ended - window - 5 and t <= ended - 5]
        gaps = [round((b[0] - a[0]) * 1000, 1) for a, b in zip(recent, recent[1:])]
        span = recent[-1][0] - recent[0][0] if len(recent) > 1 else 0
        ev.observe("streamed_fps", round((len(recent) - 1) / span, 1) if span else None, frames_csv, window_s=window,
                   source="complete frames at a stand-in headset over loopback")
        ev.observe("streamed_interval_p95_ms", sorted(gaps)[int(len(gaps) * 0.95)] if gaps else None, frames_csv,
                   note="95th percentile of the time between frames: steady 60 fps is 16.7 ms")
        ev.observe("streamed_sequence_skips", sum(1 for a, b in zip(recent, recent[1:]) if b[1] != a[1] + 1), frames_csv,
                   note="frames the encoder numbered but the stand-in did not get complete")
        drops = [int(x) for x in re.findall(r"encoderDrops=(\d+)", open(os.path.join(logs, "hardware-encoder.log"), errors="replace").read())]
        ev.observe("encoder_drops_in_window", encoder_drop_delta(drops, window),
                   os.path.join(logs, "hardware-encoder.log"), note="captured frames the encoder had no room for")
    ev.observe("gpu_utilization_median", median([v for t, v in gpu if t >= ended - window - 5]), gpu_csv, window_s=window)
    if frame:
        w, h, lum, bright = frame
        png = os.path.join(run_dir, "frame_end.png")
        ev.observe("frame_size", [w, h], png)
        ev.observe("frame_mean_luminance", round(lum, 1), png)
        ev.observe("frame_bright_fraction", round(bright, 3), png)
    else:
        # (the shared file is the driver's, rewritten by every run: named, not hashed as this run's artifact)
        ev.observe("frame_bright_fraction", None, note="no frame in the shared file", looked_at=rel(shared))
    facts = game_log_facts(game_log, exe)
    ev.observe("unhandled_exceptions", facts["unhandled"], game_log)
    ev.observe("native_signals_outside_program", facts["signals"], game_log)
    ev.observe("unimplemented_calls", facts["unimplemented"], game_log)
    ev.observe("managed_exceptions", facts["managed_exceptions"], game_log)
    ev.observe("first_problem", facts["first_problem"], game_log)
    ev.observe("exit", facts["exit"], game_log)
    windows.seal(run_dir, ev)  # appended: existing observation IDs retain their positions

    base_name, base, base_src = baseline_for(key, scenario, quality)
    result["baseline"] = base_src
    derive(ev, base_name, base, base_src)
    infer(ev)
    ev.unknown += [
        {"item": "which of the functions VXRuntime lacks the game would call later", "reason": "only the calls made in this run are seen (unimplemented_calls)"},
        {"item": "frame-to-frame timing (stutter)", "reason": "fps are 5-second averages"},
        {"item": "whether the picture is right", "reason": "only checked for not being black; look at frame_end.png"},
        {"item": "audio", "reason": "the game is muted (VXRT_MUTE=1)"},
        {"item": "behaviour with a real headset and hands", "reason": ("head pose only is simulated; no simulated controllers; physical keyboard/mouse and AVP are untested" if input_mode == "keyboard_mouse" else "the head pose and the hands are simulated; the right trigger is pulled every %g s" % press_every)},
        {"item": "whether the game reacts to the controllers and can be played", "reason": "bindings loading is all a run sees: a person decides 'playable' (tools/vxrt_playtest.py)"},
        {"item": "run-to-run variation", "reason": "one run; compare settings in alternating runs (A B B A)"},
    ]
    ev.limits += ["no debugger under Rosetta (lldb does not attach): use VXRT_TRACE_CALLS and the logs",
                  "other programs on the Mac change GPU numbers (check uptime/top)"]
    result.update({"observed": ev.observed, "derived": ev.derived, "inferred": ev.inferred, "unknown": ev.unknown,
                   "limits": ev.limits, "duration_s": round(ended - started, 1)})
    result["status"] = "fail" if not all(d["holds"] for d in ev.derived) else "pass" if base else "no_baseline"
    result["level"] = level(ev)
    return finish(result, run_dir, say)

def finish(result, run_dir, say):
    result["validation"] = validate(result)
    path = os.path.join(run_dir, "result.json")
    with open(path, "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    row = ledger.append({"run_id": result["run_id"], "game": result["game"], "scenario": result["scenario"], "quality": result.get("quality"),
                         "status": result["status"], "commit": result["environment"]["commit"], "result": rel(path),
                         "result_sha256": ledger.sha256_file(path)})
    result["ledger"] = {"row": row["i"], "sha": row["sha"]}
    say(summary(result))
    return result

def summary(r):
    lines = [f"{r['run_id']}: {r['status']}" + (f" (refused: {', '.join(r['refused'])})" if r.get("refused") else "") +
             (f", level {r['level']}" if r.get("level") else "")]
    for c in r.get("preconditions", []):
        if not c["ok"]: lines.append(f"  precondition {c['code']}: {c['detail']}")
    for o in r.get("observed", []):
        if o["metric"] in ("capture_fps_series",): continue
        lines.append(f"  observed {o['id']} {o['metric']} = {json.dumps(o['value'], ensure_ascii=False)[:160]}")
    for d in r.get("derived", []):
        lines.append(f"  derived {d['id']} {d['claim']}: {'yes' if d['holds'] else 'NO'} ({d['rule']})")
    for i in r.get("inferred", []):
        lines.append(f"  inferred {i['id']} ({i['confidence']}): {i['claim']}")
    if r.get("validation"): lines.append("  validation: " + "; ".join(r["validation"]))
    lines.append(f"  result: runs/{r['run_id']}/result.json")
    return "\n".join(lines)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game", nargs="?", help="alyx, thelab, earth, or a Steam app id")
    ap.add_argument("scenario", nargs="?")
    ap.add_argument("--quality", choices=["", "2.5k", "3k", "max"], default="")
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--args", nargs=argparse.REMAINDER, default=[], help="more game arguments (last)")
    ap.add_argument("--press-every", type=float, default=15.0, help="pull the right trigger every N seconds (0: never)")
    ap.add_argument("--root"); ap.add_argument("--overlay")
    ap.add_argument("--trace", action="store_true", help="VXRT_TRACE=1 (slower: more in the log)")
    ap.add_argument("--render-diagnostic", action="store_true", help="three fixed D3D11/DXGI counter snapshots in the selected main process; no textures or OpenVR hooks")
    ap.add_argument("--window-metadata", action="store_true", help="three read-only window metadata samples; no text or input")
    ap.add_argument("--network-diagnostic", action="store_true", help="fixed network API diagnostic: returns/errors and caller offsets, at most 128 records per process, no argument payloads")
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE", help="a tunable setting (AGENTS.md), repeatable")
    ap.add_argument("--safety-env", action="append", default=[], metavar="NAME=VALUE", help="restrict diagnostic networking/audio (fixed safe values only), repeatable")
    ap.add_argument("--no-steam", action="store_true", help="do not require Mac Steam (games that do not use it)")
    ap.add_argument("--stream", action="store_true", help="also receive the video stream as a stand-in headset and measure it")
    ap.add_argument("--json", action="store_true", help="the result on stdout")
    ap.add_argument("--list", action="store_true"); ap.add_argument("--check", action="store_true", help="preconditions only")
    ap.add_argument("--baselines", action="store_true", help="every scenario with a baseline in the game profiles (of the given game, or all installed)")
    ap.add_argument("--validate", metavar="RUN_DIR"); ap.add_argument("--verify-ledger", action="store_true")
    a = ap.parse_args()
    if a.verify_ledger:
        r = ledger.verify(); print(json.dumps(r, ensure_ascii=False, indent=2)); sys.exit(0 if r["ok"] else 1)
    if a.validate:
        problems = validate(json.load(open(os.path.join(a.validate, "result.json"))))
        print(json.dumps({"valid": not problems, "problems": problems}, ensure_ascii=False, indent=2)); sys.exit(0 if not problems else 1)
    if a.list:
        inst = installed()
        for key, spec in profiles().items():
            print(f"{key:10} {spec['appid']:7} {'installed' if spec['appid'] in inst else 'not installed':14} {spec['status']['state']:9} scenarios: {', '.join(spec['scenarios'])}")
        for appid, g in inst.items():
            if appid not in {s['appid'] for s in profiles().values()}:
                print(f"{appid:10} {appid:7} installed      no profile scenarios: start ({g['name']})")
        return
    if a.baselines:                                            # (make -C VXRuntime vr-check)
        inst, worst = installed(), 0
        for key, spec in profiles().items():
            if (a.game and a.game not in (key, spec["appid"])) or spec["appid"] not in inst:
                continue
            for name in spec["baselines"]:
                scenario, _, quality = name.partition("@")
                r = run(key, scenario, quality, a.seconds, a.args, a.press_every, a.root, a.overlay, a.trace, not a.no_steam,
                        settings=dict(kv.split("=", 1) for kv in a.set), safety_env=dict(kv.split("=", 1) for kv in a.safety_env),
                        network_diagnostic=a.network_diagnostic, window_metadata=a.window_metadata, render_diagnostic=a.render_diagnostic)
                code = {"pass": 0, "fail": 1, "no_baseline": 2, "refused": 3}.get(r["status"], 4)
                if code == 3: sys.exit(3)                      # (refused: the rest would be too)
                worst = max(worst, code)
        sys.exit(worst)
    if not a.game:
        ap.error("which game? (--list)")
    if a.check:
        key, spec, inst = resolve(a.game)
        root, overlay = a.root or os.path.join(HARNESS, "root"), a.overlay or os.path.join(HARNESS, "overlay")
        checks = preconditions(spec, inst, a.args, root, overlay, not a.no_steam)
        print(json.dumps(checks, ensure_ascii=False, indent=2)); sys.exit(0 if all(c["ok"] for c in checks) else 3)
    try:
        r = run(a.game, a.scenario, a.quality, a.seconds, a.args, a.press_every, a.root, a.overlay, a.trace, not a.no_steam, quiet=a.json,
                settings=dict(kv.split("=", 1) for kv in a.set), stream=a.stream,
                safety_env=dict(kv.split("=", 1) for kv in a.safety_env), network_diagnostic=a.network_diagnostic, window_metadata=a.window_metadata, render_diagnostic=a.render_diagnostic)
    except Exception as e:                                    # (an error in the harness itself, not a game's)
        print(f"vxrt run: error: {e}", file=sys.stderr); sys.exit(4)
    if a.json:
        print(json.dumps(r, ensure_ascii=False, indent=2))
    sys.exit({"pass": 0, "fail": 1, "no_baseline": 2, "refused": 3}.get(r["status"], 4))

if __name__ == "__main__":
    main()
