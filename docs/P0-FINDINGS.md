# P0 findings

What the compatibility work established against Omarchy 4.5.0-3 and Hyprland 0.56.2 on November 5–6, 2026, and what each finding changed. Commands were read from source, not inferred from their names.

## Write strategy by provider

| Surface | Strategy | Why |
| --- | --- | --- |
| Theme, background, font, text size, corners, transparency, bar, dock, layout, DNS, language, power profiles | Omarchy's own command, verified by reading back | These commands own hooks and multi-file side effects |
| Idle timers | Scoped edit of `shell.json` with a shell reload | No setter exists; `omarchy-debug-idle` and the idle service read these keys |
| Input, gaps, borders, dimming, displays, custom shortcuts | Owned `omarchy-control.lua` | No setter exists, and user files must stay untouched |
| Audio, Bluetooth, Wi-Fi | Omarchy route or `pactl`/`nmcli`, verified separately | Omarchy's device commands ignore failures, so exit status proves nothing |
| Pairing, joining a secured network, profile editing | Handoff to the panel or `nmtui` | They need an agent or secrets this app must not hold |
| Updates, installs, removal, firmware, snapshots, auth, reset | Handoff to the guided flow | Plan section 6 |

## Command semantics that shaped adapters

| Command | Finding | Consequence |
| --- | --- | --- |
| `omarchy default browser/editor/terminal` | Launches an installer when the app is missing | Only installed apps are choices; installing is a separate action |
| `omarchy default agent` | Sets, installs if needed, and launches | An explicit action that says so, never a plain setter |
| `omarchy default dictation` | Opens an installer | Same |
| `omarchy toggle bar on` | Hides the bar | Mapped to "Show the bar" with inverted values, covered by a test |
| `omarchy toggle suspend/screensaver/nightlight/crash capture/theme sync` | Toggle only | Read first, and run only when the state differs |
| `omarchy font set` | Feeds the name to `sed`, restarts the shell | Value must be in the live font list; restart is disclosed |
| `omarchy bluetooth device`, `omarchy audio … set default` | Swallow failures with `\|\| true` | Success is established by querying BlueZ or PipeWire |
| `omarchy powerprofiles set ac X` | Applies X now even on battery | Disclosed in the row description |
| `omarchy weather location` | Contacts a service when unset | Read the state file instead, so reading never touches the network |
| `omarchy update available` | Contacts mirrors | An explicit action, never a background check, so Overview says "No problems found" rather than "up to date" |
| `omarchy network password` | Prints a secret | Deliberately unreachable from the app |
| `omarchy network band` | Prints `band`, `available`, and `selected` rows | The setting reads the `selected` row |
| `omarchy dns Custom` | Reads servers from stdin | Custom DNS is shown but goes through the terminal route, and a switch away from it offers no undo |
| `omarchy weather location --set` | Overwrites saved coordinates | Marked as no undo, with the file backed up in case a check fails |
| `omarchy hyprland monitor scaling` | Rounds to a clean scale, ignores `eval`'s result, persists to `monitors.lua` | Verifies the result is close to the request, records what Hyprland settled on, and is refused when a saved layout would override it |
| `/usr/share/omarchy/version` | Reads `4.0.0.alpha` on 4.5 | Version comes from `omarchy version` |
| `omarchy-shell-config commit` | Rewrites `shell.json` with `jq -S` | Key order is not stable across Omarchy's own writes, so fingerprints are taken immediately before a change |

## Lua ownership

`hyprland.lua` loads the user's files, then toggles, then any custom lines. The import goes directly after `require("hypr.autostart")`, so it beats stock user files and defaults, while Omarchy's toggles and later custom lines still win. When something later wins, verification catches the mismatch, rolls back, and says so. A config without that anchor line is left alone, and those settings point to advanced access.

**Observed live, read-only:** every `getoption` key in the catalog exists on Hyprland 0.56.2. `configerrors -j` returns `[""]` when clean. Gap options answer in CSS form (`"5 5 5 5"`).

**Tried and rejected:**

- Restoring a backup automatically after a crash. An independent review showed this could overwrite an edit made after the crash. Writes are atomic, so a file that holds neither the old nor the new content is someone's later edit. Recovery now reports and points at the backup, and overwrites nothing.
- `Hyprland --verify-config` is not usable as a validator. On this version it segfaults on invalid Lua and reports unrelated errors under a temporary `HOME`. Validation is `reload` plus a comparison of `configerrors` against the pre-change baseline.
- A headless Hyprland cannot start: `CBackend::create() failed` without a seat. Nesting works instead, as a window under the running compositor. It needs a short runtime path, because Hyprland's socket path has to fit the 108-character limit. `tools/nested-check.py` uses this. The first probes left a few core dumps from the temporary Hyprland, which may have raised crash notifications at the time. The running session was not affected.

**Why stock autostart stays out of the nested session:** it would start `udiskie` automounting, first-run provisioning, and `dbus-update-activation-environment`, all of which reach beyond a temporary `HOME`. The real shell does run there. Its polkit agent cannot take over, since polkit allows one per session and the desktop's holds it, and from an isolated runtime directory `qs` finds no instance of the desktop's shell.

**Found only on real scripts:**

- Several Omarchy setters end with `omarchy-notification-send`, which exits 1 when the shell is down, so the script exits 1 after the change has landed. Exit status is not treated as the last word; the read-back decides.
- `omarchy bar set` needs the running shell, though `omarchy bar position` does not. Those rows now say so.
- `omarchy font cjk` can take well over 15 seconds, since it restarts the shell.
- `getoption` calls it `input:touchpad:tap-to-click`, but the Lua key is `tap_to_click`, and the dashed form is a config error.
- Theme and font changes restart the shell under a new PID.

## Display recovery

The watchdog is a separate detached process. It is armed before the first change, and it restores on timeout or request. Fixture tests cover timeout, keep, explicit revert, a killed parent process, an unplugged output, all remembered outputs gone, and a refused restore.

All of this has also run against real Hyprland in a nested session, including killing the app mid-preview. That is one virtual output, so real monitors remain unproven. Under the plan's gate, arranging displays therefore stays locked in the app until `omarchy-control selftest display` passes on the machine itself. A pass is recorded per Hyprland version.

The check has to make a real change, because re-applying the same layout looks identical whether a restore works or not. So it moves the first display 8 pixels, requires Hyprland to report the move, and then requires the watchdog to put it back unprompted. A test also runs this end to end with a stateful fake compositor that ignores changes, which must fail the check. `hyprctl eval` exits nonzero on a Lua error (exit 7 observed), so its exit status can be trusted.

## Decisions from plan section 9

- **Structured CLI output:** `plugin list --json`, `commands --json`, `toggle idle status`, `toggle nightlight --status`, and `hyprctl -j` are structured. Others are single-line values, adapted as read commands and pinned by tests.
- **Third-party plugin settings:** no universal schema exists, so plugins get lifecycle actions only.
- **Privileged helper:** none was needed. `omarchy dns`, `omarchy language`, and `systemctl` already reach polkit.
- **Locale catalogs:** the framework covers all 50 registered languages. Translated strings are tracked by `tools/i18n-report.py`.
- **Upstream drift:** `omarchy-control coverage --live` fails on any new first-party command without a destination. `src/data/compat.json` pins the hash of each of the 32 commands behind a typed write, so a changed script pauses just its settings until reviewed. Omarchy's version number alone would be too coarse.
- **Superwhisper:** typed settings use its documented CLI, which starts its service if it is not running. Reading those rows therefore can start the service, on machines where it is installed.
