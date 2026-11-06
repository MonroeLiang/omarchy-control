# What has and has not been verified

The plan requires recording what was actually tested. As of November 6, 2026.

## Verified

| Check | Evidence |
| --- | --- |
| 244 automated tests | `bin/test`, all passing on repeated runs, against fake commands in a temporary root. PATH there holds only fakes and plain file utilities, so a test cannot reach the desktop |
| 82 checks against real Hyprland 0.56.2, the real Omarchy shell, and real Omarchy scripts | `tools/nested-check.py`, details below |
| Package install, upgrade, and removal | `tools/package-check.sh`, the real package in a disposable pacman root, with real `pacman` under `fakeroot` |
| All 76 typed settings read correctly on real hardware | `omarchy-control doctor` on Omarchy 4.5.0-3; recorded in `live-evidence.json` |
| 389 of 390 routes resolve to an installed executable | Same run. `omarchy-notes` is not installed on that machine, and the row says so. None were launched |
| Coverage against the 4.5 inventory | 433 of 433 public commands and all 356 actionable menu rows have a destination or recorded reason |
| Accessibility tree | Every control reachable by Tab, on every page, reports a name through Qt's accessibility layer. No screen reader has actually spoken it |
| Layouts | Real QML on every page: dark and light, sharp and rounded, Arabic and German at the largest text size, 620px wide, and this machine's live data (read-only, config files confirmed untouched) |
| Independent adversarial review | 33 findings on the code that changes things, covering data loss, false success, and semantic mistakes against Omarchy's scripts. All were fixed, and the concrete ones have regression tests |

### What the nested session proves

`tools/nested-check.py` runs Hyprland and the real Omarchy shell as a window on the desktop, with a temporary `HOME` and runtime directory, and without Omarchy's autostart. Tools that act on the whole machine (`sudo`, `systemctl`, `nmcli`, `pactl`, `pkill`, and others) are stubbed first on PATH, and logged. An isolated runtime directory was confirmed unable to reach the desktop's own shell.

- **50 of 64 writable settings verified:** see the ledger entries with status `verified`. Nearly all went through apply, read back, and undo, including theme, font, background, notifications, clock format, and touchpad options. This found real bugs that fakes could not: Omarchy scripts that exit 1 after succeeding when the shell is down, and a touchpad Lua key spelled differently from its `getoption` name.
- **Display recovery:** the recovery check passes against real Hyprland, using the real detached watchdog. A preview left alone restores itself, restores on request, and restores after the app is killed mid-preview. A kept layout survives a Hyprland reload.
- **Owned Lua:** one marked import in the right place, no config errors from overrides, shortcuts, or window rules, and undo refuses after an outside edit.
- **Shell:** plugin toggles and bar widget moves work through it. Five panel routes answer over IPC. With the shell killed, shell-only settings and panels report that plainly.
- **Helpers:** the config editor, a TUI, and a terminal workflow each open a real window.
- **A real window:** correct Wayland app ID, no QML warnings. Real key events through a real keymap drive Ctrl+K, search, Enter, `?`, Esc, and Q. A second launch hands over and leaves one window. The accent color and Arabic mirroring follow file changes live.

It is a nested session: one virtual output and no real devices. The touchpad reported to it is simulated, while the options set on it are real.

## Not verified

These need your real session or hardware. Jason decided on November 6, 2026 to run them himself, using the checklist below:

- **14 writable settings:** brightness, night light, DNS, Wi-Fi band, three power profile rows, language, touchscreen, and the five Superwhisper rows. They read correctly on real hardware (touchscreen aside, where none was reported), and their writes are fixture-tested only.
- **Devices:** real sound routing, Bluetooth, Wi-Fi switching, and NetworkManager checkpoints, where polkit's real answer is unknown. Services and startup-app changes too.
- **Other helper routes,** especially installers and removals, which were deliberately not run. Their routes resolve.
- **Display hardware:** arranging real monitors, modes, rotation, hotplug. The recovery mechanism is proven, on one virtual output.
- **The installed package on this machine,** in the real launcher with the real icon.
- **A screen reader speaking,** though the accessibility tree is checked.
- **Hardware not on the audit machine:** desktops, other GPUs.

## Languages

Danish, German, and Arabic are 100% translated: 426 interface and 983 catalog strings. Those translations are machine-written and not reviewed by a native speaker. The other 47 Omarchy languages fall back to English per string, with the framework, plurals, and layout direction ready. The plan treats language-switch support and translation coverage as separate facts. Jason decided on November 6, 2026 to leave those languages on the English fallback rather than ship unreviewed machine translation.

## First live run

These go from harmless to consequential. Stop at anything surprising, and use `omarchy-control history` and `undo`.

```sh
bin/install
omarchy-control doctor
omarchy-control open
```

1. **Look:** switch themes from the Omarchy menu with the window open. The window should follow. Do the same with `omarchy language da`, then back with `omarchy language en`.
2. **Reversible settings:**
   ```sh
   omarchy-control apply power.stayawake true
   omarchy-control apply power.stayawake false
   omarchy-control apply desktop.bar.position bottom   # Check the bar moved
   omarchy-control apply desktop.bar.position top
   ```
3. **Idle timers, the plan's first path:** set Lock after in the window, check `shell.json` kept its other keys, then press Ctrl+Z.
4. **Owned Lua:** change Key repeat rate. Approve the prompt, then check `git diff`-style that `hyprland.lua` gained exactly one line. Choose Use default, and `omarchy-control.lua` should lose the value.
5. **Display recovery:**
   ```sh
   omarchy-control selftest display
   ```
   This moves your first display 8 pixels for three seconds, so windows may shift very slightly, and then it should move back on its own. It is already proven on nested Hyprland; this proves it on your hardware, and unlocks the Arrangement editor. Then preview a harmless layout change on the Displays page, and let it time out without pressing Keep.
6. **Wi-Fi switch:** with a second saved network nearby, connect and let the timer run out without pressing Keep. The previous connection should return.

To record a passing write as verified, add it to `docs/live-evidence.json` with `"status": "verified"` and what you did, then run `bin/coverage`. `tools/nested-check.py --record` does this for the nested checks.
