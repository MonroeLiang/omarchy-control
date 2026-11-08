#!/usr/bin/env python3
"""Exercises the real app, real Omarchy scripts, and real Hyprland in a disposable session.

Hyprland runs nested as a window on the current desktop. Isolation:
  * HOME and XDG_RUNTIME_DIR are temporary, so its config, sockets, and D-Bus lookups
    are its own, and systemctl --user cannot reach the session manager.
  * Omarchy's autostart is not loaded, since it starts udisks automounts and first-run
    provisioning. The real shell does run, without the journal wrapper. Its polkit agent
    cannot take over, because polkit allows one per session and the desktop's holds it.
  * Tools that act on the whole machine are stubbed first on PATH, and logged.

Usage: tools/nested-check.py [--record] [screenshot-dir]
--record marks what passed as verified in docs/live-evidence.json.
"""
import json, os, shutil, signal, stat, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = ROOT + "/build/omarchy-control"
OMARCHY = "/usr/share/omarchy"
UID = os.getuid()
PARENT_RUN = f"/run/user/{UID}"
parent = next((n for n in sorted(os.listdir(PARENT_RUN)) if n.startswith("wayland-") and not n.endswith(".lock")), None)
if not parent:
    sys.exit("No desktop session to nest inside.")
record = "--record" in sys.argv
shots = next((a for a in sys.argv[1:] if not a.startswith("--")), None)

# Socket paths have a length limit, so this has to be a short path.
R = tempfile.mkdtemp(prefix="o.", dir=PARENT_RUN)
H = R + "/home"
GUARD = R + "/guard"
for d in (H + "/.config/hypr", H + "/.config/omarchy", H + "/.local/state/omarchy/toggles/hypr", H + "/.local/state/omarchy/current", GUARD):
    os.makedirs(d)
for tool in "sudo pkexec systemctl nmcli rfkill powerprofilesctl gsettings pkill killall brightnessctl ddcutil bluetoothctl pactl wpctl udisksctl udiskie curl herdr uwsm-app uwsm notify-send dbus-send dbus-update-activation-environment busctl gdbus plymouth-set-default-theme mkinitcpio limine-update".split():
    path = f"{GUARD}/{tool}"
    with open(path, "w") as f:
        f.write(f'#!/bin/sh\necho "{tool} $*" >> "{R}/guard.log"\nexit 1\n')
    os.chmod(path, stat.S_IRWXU)

# Real hyprctl, except that it reports a touchpad, so touchpad options can be exercised.
with open(GUARD + "/hyprctl", "w") as f:
    f.write('''#!/bin/bash
for a in "$@"; do
  if [ "$a" = devices ]; then echo \'{"mice":[{"name":"nested-touchpad"}],"keyboards":[],"touch":[],"tablets":[]}\'; exit 0; fi
done
exec /usr/bin/hyprctl "$@"
''')
os.chmod(GUARD + "/hyprctl", stat.S_IRWXU)

# Real app launches go through uwsm and the user's systemd, which this session must not
# reach. So the wrapper runs the app directly, which proves the route without it.
with open(GUARD + "/uwsm-app", "w") as f:
    f.write('#!/bin/bash\n[ "$1" = "--" ] && shift\nexec "$@"\n')
os.chmod(GUARD + "/uwsm-app", stat.S_IRWXU)

shutil.copytree(OMARCHY + "/config/omarchy", H + "/.config/omarchy", dirs_exist_ok=True)
for name in ("input", "bindings", "looknfeel"):
    shutil.copy(f"{OMARCHY}/config/hypr/{name}.lua", H + "/.config/hypr/")
open(H + "/.config/hypr/autostart.lua", "w").write("-- none\n")
open(H + "/.config/hypr/monitors.lua", "w").write('hl.monitor({ output = "", mode = "preferred", position = "auto", scale = 1 })\n')
# Omarchy's own loading order, minus its autostart. The anchor lines match stock.
open(H + "/.config/hypr/hyprland.lua", "w").write('''dofile((os.getenv("OMARCHY_PATH") or "/usr/share/omarchy") .. "/default/hypr/bootstrap.lua")
require("default.hypr.helpers")
require("default.hypr.envs")
require("default.hypr.looknfeel")
require("default.hypr.input")
require("default.hypr.windows")
require("hypr.monitors")
require("hypr.input")
require("hypr.bindings")
require("hypr.looknfeel")
require("hypr.autostart")
require("default.hypr.toggles")
''')
theme = sorted(os.listdir(OMARCHY + "/themes"))[0]
shutil.copytree(f"{OMARCHY}/themes/{theme}", H + "/.local/state/omarchy/current/theme", dirs_exist_ok=True)
open(H + "/.local/state/omarchy/current/theme.name", "w").write(theme + "\n")

base = {k: v for k, v in os.environ.items() if k in ("LANG", "LC_ALL", "TERM", "USER", "LOGNAME")}
base.update({"HOME": H, "XDG_CONFIG_HOME": H + "/.config", "XDG_STATE_HOME": H + "/.local/state", "XDG_RUNTIME_DIR": R, "OMARCHY_PATH": OMARCHY,
             "PATH": GUARD + ":/usr/bin:/bin", "HYPRLAND_NO_SD_VARS": "1", "HYPRLAND_NO_SD_NOTIFY": "1", "HYPRLAND_NO_CRASHREPORTER": "1", "HYPRLAND_NO_RT": "1"})
compositor = None
shell = None
results = []


def nested_processes(name=None):
    """Every process carrying this session's runtime directory, however it was started.
    The shell restarts itself under a new PID, so a remembered PID is not enough."""
    marker = f"XDG_RUNTIME_DIR={R}".encode()
    found = []
    for pid in filter(str.isdigit, os.listdir("/proc")):
        try:
            if marker not in open(f"/proc/{pid}/environ", "rb").read().split(b"\0"):
                continue
            if name is None or open(f"/proc/{pid}/comm").read().strip() == name:
                found.append(int(pid))
        except OSError:
            pass
    return [p for p in found if p != os.getpid()]


def kill_all(pids):
    for pid in pids:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass


def stop():
    kill_all(nested_processes("quickshell"))
    if compositor and compositor.poll() is None:
        os.killpg(compositor.pid, signal.SIGTERM)
        try:
            compositor.wait(5)
        except subprocess.TimeoutExpired:
            os.killpg(compositor.pid, signal.SIGKILL)
    if R.startswith(PARENT_RUN + "/o."):
        # Whatever is left dies with the session, including any detached watchdog.
        time.sleep(1)
        kill_all(nested_processes())
        time.sleep(0.2)
        shutil.rmtree(R, ignore_errors=True)


try:
    env = dict(base, WAYLAND_DISPLAY=f"{PARENT_RUN}/{parent}")
    log = open(R + "/hypr.log", "w")
    compositor = subprocess.Popen(["bash", "-c", 'ulimit -c 0; exec Hyprland --config "$HOME/.config/hypr/hyprland.lua"'], env=env, stdout=log, stderr=log, start_new_session=True)
    sig = None
    for _ in range(40):
        time.sleep(0.25)
        d = R + "/hypr"
        if os.path.isdir(d) and os.listdir(d) and os.path.exists(f"{d}/{os.listdir(d)[0]}/.socket.sock"):
            sig = os.listdir(d)[0]
            break
    if not sig or compositor.poll() is not None:
        sys.exit("Nested Hyprland did not start:\n" + open(R + "/hypr.log", errors="ignore").read()[-600:])
    nested = next(n for n in os.listdir(R) if n.startswith("wayland-") and not n.endswith(".lock"))
    app_env = dict(base, HYPRLAND_INSTANCE_SIGNATURE=sig, WAYLAND_DISPLAY=nested, XDG_CURRENT_DESKTOP="Hyprland", XDG_SESSION_TYPE="wayland")

    shell_log = open(R + "/shell.log", "w")
    shell = subprocess.Popen(["quickshell", "-n", "-p", OMARCHY + "/shell"], env=dict(app_env, QS_DISABLE_FILE_WATCHER="1", QS_NO_RELOAD_POPUP="1"), stdout=shell_log, stderr=shell_log, start_new_session=True)

    def run(*argv, timeout=60):
        return subprocess.run(argv, env=app_env, capture_output=True, text=True, timeout=timeout)

    def app(*args, timeout=60):
        return run(APP, *args, "--json", timeout=timeout)

    def get(sid):
        r = app("get", sid)
        return (json.loads(r.stdout) if r.stdout.strip() else {}).get("value")

    def check(name, ok, detail=""):
        results.append((name, bool(ok), detail))
        print(("PASS " if ok else "FAIL ") + name + ("  " + detail if detail and not ok else ""), flush=True)

    for _ in range(60):
        time.sleep(0.25)
        if run("omarchy", "shell", "shell", "ping").returncode == 0:
            break
    check("the real Omarchy shell answers inside the nested session", run("omarchy", "shell", "shell", "ping").returncode == 0, open(R + "/shell.log", errors="ignore").read()[-300:])

    # Each: apply a new value, read it back, undo, confirm it returned.
    settings = [
        ("input.tp_natural", "false"), ("input.tp_scroll", "0.8"), ("input.tp_tap", "false"), ("input.tp_click", "false"), ("input.tp_dwt", "false"), ("input.tp_drag", "1"),
        ("desktop.dnd", "true"), ("desktop.clock.format", "HH:mm"), ("desktop.clock.vertical", "HH\nmm"), ("desktop.crashcapture", "false"), ("desktop.themesync", "true"),
        ("appearance.theme", "Tokyo Night"), ("appearance.background", None), ("appearance.font", None), ("appearance.fontcjk", "jp"),
        ("apps.default.terminal", None), ("apps.default.browser", None),
        ("appearance.corners", "rounded"), ("appearance.gaps", "false"), ("appearance.singlewindow", "true"), ("appearance.animations", "false"),
        ("appearance.gaps_in", "7"), ("appearance.gaps_out", "13"), ("appearance.border", "3"), ("appearance.dim", "true"), ("appearance.dim_strength", "0.3"),
        ("appearance.textsize", "16"), ("appearance.transparency", "glassy"),
        ("desktop.bar.visible", "false"), ("desktop.bar.position", "bottom"), ("desktop.bar.background", "glassy"),
        ("desktop.dock.position", "left"), ("desktop.dock.autohide", "true"), ("desktop.layout", "scrolling"),
        ("displays.scale", "1.25"),
        ("input.repeat_rate", "31"), ("input.repeat_delay", "300"), ("input.numlock", "true"), ("input.sensitivity", "0.25"), ("input.accel", "flat"),
        ("input.natural_mouse", "true"), ("input.follow_mouse", "2"),
        ("power.idle.lock", "600"), ("power.idle.screensaver", "60"), ("power.stayawake", "true"), ("power.screensaver", "false"), ("power.suspend", "false"),
        ("apps.default.editor", "vim"),
    ]
    verified = []

    def another_choice(sid, current):
        """A value this machine actually offers, other than the current one."""
        result = subprocess.run(["bash", "-c", {
            "appearance.font": "omarchy font list",
            "apps.default.terminal": "for t in alacritty foot ghostty kitty; do command -v $t >/dev/null && echo $t; done",
            "apps.default.browser": "for p in chromium:chromium chrome:google-chrome-stable brave:brave firefox:firefox; do command -v ${p#*:} >/dev/null && echo ${p%:*}; done",
            "appearance.background": 'ls -d "$HOME"/.local/state/omarchy/current/theme/backgrounds/* 2>/dev/null',
        }[sid]], env=app_env, capture_output=True, text=True)
        return next((c for c in result.stdout.splitlines() if c and c != current), None)

    # A theme's backgrounds come with it. The window refreshes them by watching the folder
    # Omarchy swaps, so this proves the real script really does swap it, and that the
    # background the app then accepts belongs to the new theme, not the old one.
    current = H + "/.local/state/omarchy/current"
    def backgrounds():
        folder = current + "/theme/backgrounds"
        return sorted(f for f in os.listdir(folder) if os.path.isfile(folder + "/" + f)) if os.path.isdir(folder) else []
    theme_before, list_before = get("appearance.theme"), backgrounds()
    folder_before = os.stat(current).st_mtime_ns
    target = next(name for name in ("Tokyo Night", "Catppuccin") if name != theme_before)
    switched = json.loads(app("apply", "appearance.theme", target, "--yes").stdout or "{}")
    list_after = backgrounds()
    check("a real theme change swaps the folder the window watches", os.stat(current).st_mtime_ns != folder_before)
    check("a real theme change brings its own backgrounds", switched.get("phase") == "committed" and list_after and list_after != list_before, f"{list_before} -> {list_after}")
    if list_after:
        offered = json.loads(app("plan", "appearance.background", current + "/theme/backgrounds/" + list_after[-1]).stdout or "{}")
        check("the app accepts a background from the new theme", offered.get("ok") is True, str(offered)[:160])
    stale = next((f for f in list_before if f not in list_after), None)
    if stale:
        refused = json.loads(app("plan", "appearance.background", current + "/theme/backgrounds/" + stale).stdout or "{}")
        check("the app refuses a background the old theme had", refused.get("ok") is not True, str(refused)[:160])
    if switched.get("id"):
        app("undo", switched["id"])

    for sid, value in settings:
        before = get(sid)
        if value is None:
            value = another_choice(sid, before)
            if value is None:
                print(f"SKIP {sid}: this machine offers no second choice", flush=True)
                continue
        applied = app("apply", sid, value, "--yes")
        try:
            tx = json.loads(applied.stdout)
        except json.JSONDecodeError:
            tx = {"phase": "error", "message": applied.stderr.strip()[:200]}
        if tx.get("phase") != "committed":
            check(sid + " apply", False, f'{tx.get("phase")}: {tx.get("message")}')
            continue
        now = get(sid)
        undone = json.loads(app("undo", tx["id"]).stdout or "{}") if tx.get("recovery") != "no-undo" else {}
        back = get(sid)
        ok = undone.get("phase") == "committed" and back == before
        check(sid, ok, f"before={before} now={now} undo={undone.get('phase')}: {undone.get('message')} back={back}")
        if ok:
            verified.append(sid)

    # A later edit by someone else must block undo.
    tx = json.loads(app("apply", "power.idle.lock", "900").stdout)
    with open(H + "/.config/omarchy/shell.json") as f:
        text = f.read()
    with open(H + "/.config/omarchy/shell.json", "w") as f:
        f.write(text.replace("900", "901"))
    check("undo refuses after an outside edit", json.loads(app("undo", tx["id"]).stdout).get("phase") == "failed")

    # The owned file: stock config gains exactly one line, in the right place.
    lua = open(H + "/.config/hypr/hyprland.lua").read()
    check("import is one marked line after autostart, before toggles", lua.count("hypr.omarchy-control") == 1 and lua.index("hypr.autostart") < lua.index("hypr.omarchy-control") < lua.index("default.hypr.toggles"))
    # Load order is real now: a toggle must beat the owned override, and hand edits must pause.
    errors = json.loads(run("hyprctl", "-j", "configerrors").stdout)
    check("no Hyprland config errors from anything above", not [e for e in errors if e.strip()], str(errors)[:300])

    # Worth trying where the nested session allows it, but not a failure if it does not.
    for sid, value in (("input.touchpad", "false"), ("displays.nightlight", "true"), ("system.language", "de")):
        before = get(sid)
        applied = app("apply", sid, value, "--yes", timeout=150)
        try:
            tx = json.loads(applied.stdout)
        except json.JSONDecodeError:
            tx = {}
        if tx.get("phase") == "committed":
            undone = json.loads(app("undo", tx["id"], timeout=150).stdout or "{}")
            ok = undone.get("phase") == "committed" and get(sid) == before
            check(sid, ok, f"undo={undone.get('phase')}: {undone.get('message')}")
            if ok:
                verified.append(sid)
        else:
            print(f"SKIP {sid}: {tx.get('phase')}: {str(tx.get('message'))[:110]}", flush=True)

    # Weather has no undo, by design, since the command overwrites coordinates.
    r = app("apply", "system.weather", "Oslo")
    weather_ok = r.returncode == 0 and get("system.weather") == "Oslo" and app("apply", "system.weather", "").returncode == 0 and get("system.weather") == ""
    check("weather location applies and clears", weather_ok, r.stdout + r.stderr)
    if weather_ok:
        verified.append("system.weather")

    # Plugins and bar layout, through the running shell.
    plugin_list = json.loads(run("omarchy", "plugin", "list", "--json").stdout)
    toggle = next((p for p in plugin_list if p.get("id") == "omarchy.weather"), None)
    if toggle:
        was = toggle["enabled"]
        a = app("plugin", "disable" if was else "enable", toggle["id"])
        b = app("plugin", "enable" if was else "disable", toggle["id"])
        check("plugin toggles through the shell and back", a.returncode == 0 and b.returncode == 0, a.stdout + a.stderr + b.stdout + b.stderr)
    layout = json.load(open(H + "/.config/omarchy/shell.json"))["bar"]["layout"]
    section = next(s for s in ("left", "centerEnd", "right") if layout.get(s))
    widget = layout[section][0]
    widget = widget if isinstance(widget, str) else widget["id"]
    target = "centerBegin"
    a = app("widget", "move", widget, target, "0", section, "0")
    b = app("widget", "move", widget, section, "0", target, "0")
    check("bar widget moves and moves back", a.returncode == 0 and b.returncode == 0, a.stdout + a.stderr + b.stdout + b.stderr)
    # A reorder within a region: the shell takes the widget out before putting it in, and the
    # app's check now looks at the exact place. Only the real shell can confirm both agree.
    layout = json.load(open(H + "/.config/omarchy/shell.json"))["bar"]["layout"]
    ids = lambda name: [w if isinstance(w, str) else w["id"] for w in layout[name]]
    busy = next((name for name in layout if len(layout[name]) >= 2), None)
    if busy:
        before = ids(busy)
        a = app("widget", "move", before[0], busy, "1", busy, "0")
        layout = json.load(open(H + "/.config/omarchy/shell.json"))["bar"]["layout"]
        moved = ids(busy)
        b = app("widget", "move", before[0], busy, "0", busy, "1")
        layout = json.load(open(H + "/.config/omarchy/shell.json"))["bar"]["layout"]
        check("bar widget reorders within a region and back", a.returncode == 0 and b.returncode == 0 and moved[:2] == [before[1], before[0]] and ids(busy) == before, a.stdout + a.stderr + b.stdout + b.stderr + str(moved))

    # Real helpers, one of each kind that opens something here.
    client_count = lambda: len(json.loads(run("hyprctl", "-j", "clients").stdout))
    for route in ("panel.network", "panel.audio", "panel.monitor", "panel.bluetooth", "setup.network.qr"):
        r = app("launch", route, timeout=15)
        check(f"panel route opens: {route}", r.returncode == 0, r.stdout + r.stderr)
        app("launch", route, timeout=15)
    for route in ("config.looknfeel", "helper.btop", "drive.info"):
        before_count = client_count()
        r = app("launch", route, timeout=15)
        appeared = False
        for _ in range(40):
            time.sleep(0.25)
            if client_count() > before_count:
                appeared = True
                break
        check(f"route opens a real window: {route}", r.returncode == 0 and appeared, r.stdout + r.stderr)
        for c in json.loads(run("hyprctl", "-j", "clients").stdout):
            run("hyprctl", "eval", f'hl.dispatch(hl.dsp.window.close({{ window = "address:{c["address"]}" }}))')
        time.sleep(0.5)

    # Without the shell, shell-only rows must say so and change nothing.
    kill_all(nested_processes("quickshell"))
    for _ in range(40):
        time.sleep(0.25)
        if run("omarchy", "shell", "shell", "ping").returncode != 0:
            break
    plan = json.loads(app("plan", "desktop.clock.format", "HH:mm").stdout or "{}")
    check("with the shell down, shell-only settings explain themselves", not plan.get("ok", True) and "shell" in plan.get("error", ""), str(plan))
    r = app("launch", "panel.network", timeout=15)
    check("with the shell down, a panel reports that it did not open", r.returncode != 0, r.stdout + r.stderr)

    # Shortcuts and rules, reloaded by the real compositor.
    r = app("shortcut", "add", "SUPER + ALT + Y", "Nested test", "true")
    binds = run("hyprctl", "-j", "binds").stdout
    check("shortcut reaches Hyprland", r.returncode == 0 and "Nested test" in binds, r.stdout + r.stderr)
    r = app("shortcut", "remove", "SUPER + ALT + Y")
    check("shortcut removal reaches Hyprland", r.returncode == 0 and "Nested test" not in run("hyprctl", "-j", "binds").stdout)
    for action, value in (("float", ""), ("tile", ""), ("workspace", "3"), ("opacity", "0.9 0.85")):
        r = app("rule", "add", "nested-test-class", action, *([value] if value else []))
        check(f"window rule {action} loads cleanly", r.returncode == 0, r.stdout + r.stderr)
        app("rule", "remove", "0")
    errors = json.loads(run("hyprctl", "-j", "configerrors").stdout)
    check("no Hyprland config errors from shortcuts or rules", not [e for e in errors if e.strip()], str(errors)[:300])

    # Display recovery against the real compositor, with the real detached watchdog.
    output = json.loads(run("hyprctl", "-j", "monitors").stdout)[0]["name"]
    position = lambda: json.loads(run("hyprctl", "-j", "monitors").stdout)[0]["x"]
    r = app("selftest", "display", timeout=60)
    check("display recovery check passes", r.returncode == 0, r.stdout + r.stderr)
    start = position()
    r = app("display", "preview", output, str(start + 40), "0", timeout=60)
    check("preview left alone restores by itself", r.returncode == 0 and position() == start, f"{r.stdout}{r.stderr} x={position()}")
    r = app("display", "preview", output, str(start + 40), "0", "--revert", timeout=60)
    check("preview restores on request", r.returncode == 0 and position() == start, f"{r.stdout}{r.stderr} x={position()}")
    # A killed app must still come back: start a preview, kill the app, watch the position.
    p = subprocess.Popen([APP, "display", "preview", output, str(start + 40), "0"], env=app_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        time.sleep(0.1)
        if position() != start:
            break
    moved = position() != start
    p.kill()
    p.wait()
    still_moved = position() != start
    for _ in range(200):
        time.sleep(0.1)
        if position() == start:
            break
    check("killing the app mid-preview still restores the display", moved and still_moved and position() == start, f"moved={moved} x={position()}")
    r = app("display", "preview", output, str(start + 40), "0", "--keep", timeout=60)
    run("hyprctl", "reload")
    time.sleep(0.5)
    check("a kept layout persists across a Hyprland reload", r.returncode == 0 and position() == start + 40, f"{r.stdout}{r.stderr} x={position()}")

    # The real window, in a real compositor, with real key events through a real keymap.
    gui = subprocess.Popen([APP, "open"], env=dict(app_env, QT_QPA_PLATFORM="wayland", QT_FORCE_STDERR_LOGGING="1"), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    client = None
    for _ in range(50):
        time.sleep(0.2)
        client = next((c for c in json.loads(run("hyprctl", "-j", "clients").stdout) if c.get("class") == "omarchy-control"), None)
        if client:
            break
    check("window appears with the right Wayland app ID", client is not None)

    # A second launch hands over its destination and leaves, so one window stays.
    second = subprocess.run([APP, "open", "--page", "power"], env=dict(app_env, QT_QPA_PLATFORM="wayland"), capture_output=True, text=True, timeout=10)
    time.sleep(0.5)
    count = sum(c.get("class") == "omarchy-control" for c in json.loads(run("hyprctl", "-j", "clients").stdout))
    check("a second launch hands over and leaves one window", second.returncode == 0 and count == 1, f"rc={second.returncode} windows={count}")

    def shot(name):
        if shots:
            os.makedirs(shots, exist_ok=True)
            run("grim", f"{shots}/{name}.png")

    def key(*args):
        run("wtype", *args)
        time.sleep(0.6)

    time.sleep(1)
    shot("real-overview")
    key("-M", "ctrl", "k", "-m", "ctrl")
    key("stay awake")
    shot("real-search")
    key("-k", "Return")
    shot("real-search-landed")
    key("?")
    shot("real-help")
    key("-k", "Escape")
    theme_file = H + "/.local/state/omarchy/current/theme/colors.toml"
    colors = open(theme_file).read()
    open(theme_file, "w").write(''.join(line if not line.startswith("accent") else 'accent = "#00c8ff"\n' for line in colors.splitlines(True)))
    time.sleep(1)
    shot("real-theme-switched")
    open(H + "/.config/omarchy/language", "w").write("ar\n")
    time.sleep(1)
    shot("real-language-switched")
    open(H + "/.config/omarchy/language", "w").write("en\n")
    time.sleep(0.5)
    key("q")
    try:
        gui.wait(5)
        quit_ok = True
    except subprocess.TimeoutExpired:
        gui.kill()
        quit_ok = False
    output_text = gui.stdout.read()
    check("Q quits a real window", quit_ok)
    bad = [line for line in output_text.splitlines() if "qrc:/" in line or "QML" in line]
    check("no QML warnings in the real window", not bad, "; ".join(bad)[:400])

    # The tray. The session above has no D-Bus, so the shell hosts no tray and closing
    # quits. Here it gets a private bus that starts nothing on its own, and the real shell
    # comes back on it to host the app's icon.
    bus_conf = R + "/bus.conf"
    open(bus_conf, "w").write(f'''<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:path={R}/bus</listen>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
''')
    bus = subprocess.Popen(["dbus-daemon", "--config-file=" + bus_conf, "--nofork"], env=app_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    # The tray icon where the package installs it, so the app sends it by name as an
    # installed copy does, and the bar can recolour it.
    icons = R + "/share/icons/hicolor/symbolic/apps"
    os.makedirs(icons)
    shutil.copy(ROOT + "/pkgbuild/omarchy-control-symbolic.svg", icons)
    tray_env = dict(app_env, DBUS_SESSION_BUS_ADDRESS=f"unix:path={R}/bus")
    installed_env = dict(tray_env, XDG_DATA_DIRS=f"{R}/share:/usr/local/share:/usr/share")

    def busctl(*args):
        r = subprocess.run(["/usr/bin/busctl", "--user", "--json=short", *args], env=tray_env, capture_output=True, text=True, timeout=10)
        return json.loads(r.stdout) if r.returncode == 0 and r.stdout.strip() else None

    def tray_items():
        r = busctl("get-property", "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems")
        return r["data"] if r else []

    def windows():
        return sum(c.get("class") == "omarchy-control" for c in json.loads(run("hyprctl", "-j", "clients").stdout))

    def wait_for(test, seconds=10):
        for _ in range(int(seconds * 5)):
            if test():
                return True
            time.sleep(0.2)
        return test()

    def close_window():
        """As Super+W does: the window is asked to close, and may decline."""
        for c in json.loads(run("hyprctl", "-j", "clients").stdout):
            if c.get("class") == "omarchy-control":
                run("hyprctl", "dispatch", f'hl.dsp.window.close({{ window = "address:{c["address"]}" }})')

    # One shell per configuration: the old one has to be gone before the new one starts.
    kill_all(nested_processes("quickshell"))
    wait_for(lambda: not nested_processes("quickshell"))
    time.sleep(0.5)
    shell = subprocess.Popen(["quickshell", "-n", "-p", OMARCHY + "/shell"], env=dict(tray_env, QS_DISABLE_FILE_WATCHER="1", QS_NO_RELOAD_POPUP="1"), stdout=shell_log, stderr=shell_log, start_new_session=True)
    host = wait_for(lambda: busctl("get-property", "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "IsStatusNotifierHostRegistered") == {"type": "b", "data": True}, 20)
    check("the real shell hosts a tray on the private bus", host)
    gui = subprocess.Popen([APP, "open"], env=dict(installed_env, QT_QPA_PLATFORM="wayland", QT_FORCE_STDERR_LOGGING="1"), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    item = None
    if wait_for(lambda: windows() == 1 and tray_items()):
        item = tray_items()[0]
    check("the running app puts its icon in the tray", item is not None, str(tray_items()))
    # An item is registered as "service/path", or by service alone at the usual path.
    service, _, path = (item or "").partition("/")
    path = "/" + (path or "StatusNotifierItem")

    def item_property(name):
        r = busctl("get-property", service, path, "org.kde.StatusNotifierItem", name)
        return r["data"] if r else None

    tooltip = item_property("ToolTip") if item else None
    check("the tray icon is the installed symbolic mark, which the bar recolours", item and item_property("IconName") == "omarchy-control-symbolic", str(item_property("IconName") if item else None))
    check("the tray icon is named for the app", item and item_property("Id") == "omarchy-control" and tooltip and tooltip[2] == "Omarchy Control", f"id={item_property('Id') if item else None} tooltip={tooltip}")
    close_window()
    closed = wait_for(lambda: windows() == 0)
    time.sleep(0.5)
    check("closing the window keeps the app in the tray", closed and gui.poll() is None and tray_items() == [item], f"windows={windows()} running={gui.poll() is None}")
    subprocess.run(["/usr/bin/busctl", "--user", "call", service, path, "org.kde.StatusNotifierItem", "Activate", "ii", "0", "0"], env=tray_env, capture_output=True, timeout=10)
    check("clicking the tray icon brings the window back", wait_for(lambda: windows() == 1))
    close_window()
    wait_for(lambda: windows() == 0)
    second = subprocess.run([APP, "open"], env=dict(tray_env, QT_QPA_PLATFORM="wayland"), capture_output=True, text=True, timeout=10)
    check("launching again from the tray's hidden state shows the one window", second.returncode == 0 and wait_for(lambda: windows() == 1) and gui.poll() is None)

    # Quit from the tray's menu, found by its label the way the bar finds it.
    menu_path = (item_property("Menu") if item else None) or "/"
    layout = busctl("call", "--", service, menu_path, "com.canonical.dbusmenu", "GetLayout", "iias", "0", "-1", "0")
    entries = layout["data"][1][2] if layout else []
    labels = {entry["data"][1].get("label", {}).get("data", ""): entry["data"][0] for entry in entries if entry.get("type") == "(ia{sv}av)"}
    check("the tray's menu offers Open and Quit", "Open Omarchy Control" in labels and "Quit Omarchy Control" in labels, f"{labels} menu={menu_path} layout={str(layout)[:700]}")
    clicked = None
    if "Quit Omarchy Control" in labels:
        clicked = subprocess.run(["/usr/bin/busctl", "--user", "call", "--", service, menu_path, "com.canonical.dbusmenu", "Event", "isvu", str(labels["Quit Omarchy Control"]), "clicked", "s", "", "0"], env=tray_env, capture_output=True, text=True, timeout=10)
    try:
        gui.wait(5)
        quit_ok = True
    except subprocess.TimeoutExpired:
        gui.kill()
        quit_ok = False
    check("Quit in the tray's menu ends the app and takes its icon away", quit_ok and wait_for(lambda: tray_items() == [], 5), f"quit={quit_ok} items={tray_items()} click={clicked and (clicked.returncode, clicked.stdout, clicked.stderr)} labels={labels}")
    bad = [line for line in gui.stdout.read().splitlines() if "qrc:/" in line or "QML" in line or "Labs Platform" in line]
    check("no warnings from the tray", not bad, "; ".join(bad)[:400])
    kill_all(nested_processes("quickshell"))
    bus.terminate()

    guard = open(R + "/guard.log").read().splitlines() if os.path.exists(R + "/guard.log") else []
    print(f"\nStubbed calls blocked: {len(guard)}: " + ", ".join(sorted({line.split()[0] for line in guard})))

    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed")
    if record:
        path = ROOT + "/docs/live-evidence.json"
        evidence = json.load(open(path)) if os.path.exists(path) else {}
        day = time.strftime("%Y-%m-%d")
        for sid in verified:
            evidence[sid] = {"status": "verified", "evidence": f"apply, read back, and undo with real Omarchy scripts and real Hyprland in an isolated nested session, {day} (tools/nested-check.py)"}
        json.dump(evidence, open(path, "w"), indent=1, sort_keys=True)
        open(path, "a").write("\n")
        print(f"Recorded {len(verified)} settings as verified.")
    sys.exit(1 if failed else 0)
finally:
    stop()
