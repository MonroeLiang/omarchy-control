# What has and has not been verified

The plan requires recording what was actually tested. As of November 6, 2026.

## Verified

| Check | Evidence |
| --- | --- |
| 351 automated tests | `bin/test`, all passing on repeated runs, against fake commands in a temporary root. PATH there holds only fakes and plain file utilities, so a test cannot reach the desktop |
| 87 checks against real Hyprland 0.56.2, the real Omarchy shell, and real Omarchy scripts | `tools/nested-check.py`, details below |
| Package install, upgrade, and removal | `tools/package-check.sh`, the real package in a disposable pacman root, with real `pacman` under `fakeroot` |
| All 77 typed settings read correctly on real hardware | `omarchy-control doctor` on Omarchy 4.5.0-3; recorded in `live-evidence.json` |
| 391 of 392 routes resolve to an installed executable | Same run. `omarchy-notes` is not installed on that machine, and the row says so. None were launched |
| Curl installer | `tools/installer-check.sh` runs `install.sh` against real release files in a local mirror, installing the real package into a disposable pacman root. It refuses a tampered package or missing checksums before pacman runs, and a truncated download runs nothing |
| Release build | The steps in `.github/workflows/release.yml` ran in a clean `archlinux:base-devel` container: tests, package check, package build, checksums, installer check. The workflow then published v0.1.0 on GitHub |
| In-app updates | Fixture-tested with fake `curl` and `sudo`, including a real click through Check, Install, and Restart on the About page. See the real run below |
| Real install and update | On November 6, 2026, Jason installed v0.1.0 with the curl installer from GitHub, then updated to v0.1.1 through About: Check for updates, Install update with his password at `sudo`, and Restart now. He confirmed it all worked. The same paths also passed from the command line against GitHub, in a disposable pacman root |
| Icon packs through Omarchy's real scripts | `tools/icons-check.sh` runs the real `gsettings` on GLib's keyfile backend, the real `omarchy-theme-set-gnome` and `omarchy-hook`, and the real binary, under a throwaway `HOME`. A fixed choice survives theme changes with no window open, including overlapping switches, and following, reset, and undo all land on the right pack. It checks that the real value did not move. It models `omarchy-theme-set`'s ordering rather than running it |
| Coverage against the 4.5 inventory | 433 of 433 public commands and all 356 actionable menu rows have a destination or recorded reason |
| Accessibility tree | Every control reachable by Tab, on every page, reports a name through Qt's accessibility layer, and Tab scrolls to keep focus on screen. No screen reader has actually spoken it |
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

### One accidental write

On November 6, 2026, while auditing GSettings for icon packs, a test meant to be isolated reached the real session and left `org.gnome.desktop.interface icon-theme` set to a nonsense value for about a minute. It was run as `dbus-run-session -- env HOME=... gsettings set ...`. The `env` reaches only the child, and the bus started `dconf-service` with the real environment. The value was restored to the `Yaru-blue` recorded just before, and the other keys in that schema were checked. `tools/icons-check.sh` uses the keyfile backend instead, which has no bus or dconf involved, and fails if the real value moves. Restoring it took a detour: the running service compares a write against what it holds in memory, so writing the old value back did nothing until a different value was written first.

## Not verified

These need your real session or hardware. Jason decided on November 6, 2026 to run them himself, using the checklist below:

- **Icon packs on a real session:** which apps visibly change, and whether they need reopening. Login was checked by reading Omarchy's source, which sets the icon theme only at first run and on theme changes; it has not been observed. The steps are below.
- **14 writable settings:** brightness, night light, DNS, Wi-Fi band, three power profile rows, language, touchscreen, and the five Superwhisper rows. They read correctly on real hardware (touchscreen aside, where none was reported), and their writes are fixture-tested only.
- **Devices:** real sound routing, Bluetooth, Wi-Fi switching, and NetworkManager checkpoints, where polkit's real answer is unknown. Services and startup-app changes too.
- **Other helper routes,** especially installers and removals, which were deliberately not run. Their routes resolve.
- **Display hardware:** arranging real monitors, modes, rotation, hotplug. The recovery mechanism is proven, on one virtual output. The Displays page's states are drawn from the sandbox, including a real watchdog preview against fake displays; dragging with a real pointer, per-display brightness over DDC/CI, and unplugging a cable mid-edit are not yet observed. The steps are below.
- **The installed package on this machine,** in the real launcher with the real icon.
- **A screen reader speaking,** though the accessibility tree is checked.
- **Hardware not on the audit machine:** desktops, other GPUs.

## Languages

Danish, German, and Arabic are 100% translated: 701 interface and 1016 catalog strings. Those translations are machine-written and not reviewed by a native speaker. The other 47 Omarchy languages fall back to English per string, with the framework, plurals, and layout direction ready. The plan treats language-switch support and translation coverage as separate facts. Jason decided on November 6, 2026 to leave those languages on the English fallback rather than ship unreviewed machine translation.

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

## Icon packs

Stop at anything surprising, and use `omarchy-control history` and `undo`. This changes your real icons, and undo puts them back.

1. **Choose one:** open Appearance, then Icons, and pick a pack. Open Files. Its folders should change. Note any app that needs reopening, and whether the Omarchy launcher and dock change. They resolve app icons from their own list across all packs, so they are not expected to follow.
2. **Keep it:** with Omarchy Control closed, switch themes twice. The pack should stay. Log out and back in. It should still stay.
3. **Follow:** pick Follow desktop theme, then switch to `catppuccin`, which asks for `Yaru-purple`. Files should follow.
4. **Undo:** in History, undo each change, newest first. The first undo of all should remove `~/.config/omarchy/hooks/theme-set.d/90-omarchy-control-icons` and `~/.config/omarchy-control/icons.json`, and leave `gsettings get org.gnome.desktop.interface icon-theme` where it started.
5. **Import:** download a pack, extract it, then drag its folder onto the tiles, and separately use Import folder. Each should say Imported and change nothing until you press Use this pack. An imported pack can change launcher and dock icons even when it is not chosen, because their list reads every pack. To remove a pack, delete its folder from `~/.local/share/icons`.

## Displays page

Stop at anything surprising. Every layout change here is a preview that undoes itself unless you press Keep.

1. **Selection:** with two displays connected, click the one the window is not on. The inspector, the Screen heading, and the brightness row should all name it, and the desktop's focus should not move.
2. **Brightness:** with an external display selected, move Screen brightness. Only that display should change. A monitor without DDC/CI should show the row as unavailable, with the reason.
3. **Scale:** select the display the window is not on. Before the recovery check, the Scale row should say why it cannot change it right away. After it, choosing a scale should mark it Changed and show Changes not applied, with nothing moving yet.
4. **Drag:** drag a display next to another. Its edge should meet the other's. Preview layout, then let the countdown run out. The previous layout should come back, and the page should say Hyprland reports the layout from before the preview.
5. **Unplug:** stage a change for an external display, then unplug its cable. The page should say its changes were dropped, keep any others, and select a display that is still connected.

## Repeating the update check

This passed on November 6, 2026, and only needs a machine on the older version. `pacman` skips an identical version, so a locally built package has to go first.

1. Remove the local package with `sudo pacman -R omarchy-control`.
2. Install the older release with the curl line from the README, adding `| bash -s -- --version 0.1.0` in place of `| bash`.
3. Open About, then use Check for updates, Install update, and Restart now. About should then show the latest version.

To record a passing write as verified, add it to `docs/live-evidence.json` with `"status": "verified"` and what you did, then run `bin/coverage`. `tools/nested-check.py --record` does this for the nested checks.
