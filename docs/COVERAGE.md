# Omarchy Control — coverage map

Planning baseline: November 5, 2026. Implementation state: November 6, 2026. Refer to [the implementation plan](PLAN.md) for the P0–P5 milestones and release gates.

## Current coverage

Generated from the catalog by `bin/coverage` into the [ledger](coverage-ledger.json). Reported separately on purpose: a helper launch is not a native editor.

| Route | Count |
| --- | --- |
| Native typed settings and pages | 97 |
| Omarchy panels | 23 |
| Guided setup routes | 334 |
| Specialist apps | 15 |
| Advanced config files | 22 |
| **Total destinations** | **491** |

| Inventory | Routed | Excluded with a reason | Unclassified |
| --- | --- | --- | --- |
| 433 public commands | 301 | 132 | 0 |
| 356 actionable menu rows | 356 | 0 | 0 |

| Validation | Count |
| --- | --- |
| Writable settings verified on real Hyprland, the real shell, and real scripts, in an isolated nested session | 50 of 64 |
| Settings with a live read on real hardware | 76 of 76 |
| Routes resolved to an installed executable, none launched | 389 of 390 |
| Writes performed on the real desktop session | 0; see [live checks](LIVE-CHECKS.md) |

The ledger only says `verified` for a recorded live write. Exclusions and their reasons are in `src/data/coverage-exclusions.json`. `omarchy-control coverage --live` re-reads Omarchy's commands, and a test fails on any new one with no destination.

The domain table below is the original plan, and each route it names now exists in the catalog. Firewall is status plus guided routes rather than a native editor, because reading UFW rules needs root and the GUI stays unprivileged.

## Domain coverage

“Initial route” means the planned P1 destination after its launch/read behavior is verified. “Integrated work” identifies the later native controls or additional administration work. Helper and advanced access remain valid final integration modes when they are the appropriate interface.

| ID / domain | Configuration surfaces to account for | Owners and sources | Initial route → integrated work |
| --- | --- | --- | --- |
| C01 Appearance | Installed/custom themes; install/update/remove themes; backgrounds; font family and CJK forms; global text size; theme sync; branding; theme overlays/templates | `omarchy theme`, `font`, `display text size`, `branding`; current theme state; user themes/backgrounds/themed templates | Existing appearance menus and pickers → P2 native selection and common edits; advanced templates retain file access |
| C02 Bar, dock, plugins | Bar position/background/visibility; five layout sections plus tray; widget placement/options; dock position/autohide/pins; plugin enable/disable/add/clone/remove/update; custom active implementations | `omarchy bar`, `dock`, `plugin`; `shell.json`; packaged/user manifests; `shell.toml` | Existing menus and plugin workflows → P2 layout editor and typed known widget options; P4 plugin maintenance |
| C03 Windows and workspaces | Layout defaults; tiling/scrolling/floating; workspace assignment; gaps/borders/corners; opacity/blur/shadow/animations; focus behavior; rules; scratchpads/minimize where supported | `omarchy hyprland`, toggles; Hyprland runtime options; `looknfeel.lua`, `hyprland.lua`, user modules | Existing appearance actions and config editor → P2 supported appearance setters; P3 owned rule/layout controls; custom Lua remains advanced |
| C04 Displays | Detect/identify outputs; mode/refresh; arrangement; rotation; mirror/extend; scaling; text size link; internal/external brightness; night-light temperature/schedule; VRR/HDR/color features only when supported | `omarchy monitor state`, `brightness`, Hyprland monitor APIs; `monitors.lua`, `hyprsunset.conf`; `omarchy restart hyprsunset` | Existing display panel and config editor → P3 preview/revert editor with runtime validation and hardware gating |
| C05 Keyboard and pointing | Keyboard layouts/variants/options; repeat; Num Lock; shortcuts/collisions; mouse sensitivity/acceleration; touchpad scrolling/tapping/dragging; gestures; touchscreen; per-device settings; keyboard backlight | `omarchy setup keyboard/input`, input/touch toggles, brightness keyboard; `input.lua`, `bindings.lua`; Fcitx; XCompose | Existing typing/setup/config workflows → P3 native common controls and binding capture with conflict handling |
| C06 Accessibility and dictation | Text size; contrast/motion links; keyboard access; supported accessibility helpers; dictation backend/mode/language/microphone/shortcuts; input methods | `omarchy display text size`, `default dictation`, dictation/voxtype routes; installed Superwhisper; tool-specific APIs | Cross-links and supported helper launch → P2 common accessibility controls; P4 typed dictation settings where its documented interface permits them |
| C07 Audio | Output/input devices; default routes; volume/mute; stream routing; profiles; Bluetooth audio; hardware tuning; restart diagnostics | PipeWire/WirePlumber state; `wpctl`, `pactl`; `omarchy audio`, `restart audio`; existing audio panel | Existing audio panel → P3 integrated devices, volume, mute, and default routing; specialist details retain a helper route |
| C08 Bluetooth | Radio state; scan/discovery; pair/trust/connect/disconnect/forget; paired-device status; input/audio profile links | BlueZ; `omarchy bluetooth`; existing Bluetooth panel | Existing panel → P3 common controls with pairing-agent and device-removal handling |
| C09 Printers, cameras, peripherals | Printer discovery/default/queues; scanner helper access; webcam selection/capabilities; supported hardware setup; device-specific controls; removable devices | Installed `system-config-printer`, CUPS, camera tools; `omarchy install camera`; matching hardware commands | Installed specialist apps/setup → P3 status and links; P4 vetted device-specific workflows. Do not assume a scanner or camera utility exists |
| C10 Networking | Wi-Fi/Ethernet; profiles; connect/disconnect; signal/status; captive portals; DHCP/static addresses; DNS; band; proxy/VPN workflows; QR sharing; optional diagnostics | NetworkManager; `omarchy network`, `dns`; network/Wi-Fi QR panels; `nmtui` | Existing panels and nmtui → P3 common connections/settings; advanced profiles/VPNs use available helpers until a safe editor exists |
| C11 Remote access and sharing | Tailscale; SSH server/agent; firewall status/rules; approved remote-desktop/game-streaming setup; file sharing; cloud-service helpers | Omarchy Tailscale/SSH/service routes and panels; UFW; installed service apps | Existing setup and helpers → P4 status, scoped actions, and firewall integration; auth/network consequences stay explicit |
| C12 Power and session | AC/battery profiles; battery status; idle timeouts; lock/screensaver; stay awake; suspend availability; lid behavior where supported; hibernation; logout/reboot/shutdown | Omarchy power/profile/toggle/system/hibernation routes; UPower/logind; `shell.json`; selected power plugin | Existing power panel and system menu → P2 profiles/idle; P4 supported suspend/hibernate/session administration |
| C13 Notifications and privacy | Do-not-disturb; notification settings; reminders; clipboard behavior/history controls where supported; microphone/camera links; capture/sharing behavior; crash capture | Notification/reminder/clipboard/capture routes and plugins; shell config; portal configuration | Existing menus/panels → P2 supported notification settings; P4 privacy controls and diagnostics. Never invent a universal permission switch unsupported by the platform |
| C14 Apps and defaults | Installed apps; approved package/AUR installs/removals; default browser/editor/terminal/agent/dictation; MIME associations; web apps; optional app suites; launch handlers | `omarchy default`, `install`, `remove`, `pkg`, `webapp`, `tui`; XDG desktop and MIME metadata | Existing install/remove/setup workflows → P2 defaults; P4 app catalog and job tracking, preserving Omarchy package policies |
| C15 Language, time, region | Desktop language; format locale; timezone; time synchronization; date/clock formats; weather location; calendar integration | `omarchy language`, `menu timezone`, `update time`, `weather`, `calendar`; locale registry; clock/weather settings | Existing language/time menus and helpers → P2 clock format; P4 locale/time/integration controls |
| C16 Accounts and authentication | Current account basics; password change; fingerprint; FIDO2; lock/sudo authentication; sudo policy; additional user/account administration where supported | `passwd`; Omarchy security/setup/remove/sudo routes; PAM/polkit-owned flows | Existing guided setup → P4 status and verified handoffs; additional user management needs a suitable supported privileged interface |
| C17 Storage and recovery | Disk/mount/capacity information; removable storage; encryption password flow; snapshots create/restore; personal-backup helper access; recovery guidance | `omarchy drive`, `snapshot`; GNOME Disks; Snapper/Limine; installed backup tools | GNOME Disks and Omarchy workflows → P4 status, snapshot workflow integration, and explicit backup scope |
| C18 Updates, boot, firmware | System/Omarchy updates; channels; package maintenance; firmware; kernel choice; reboot requirements; Plymouth; direct boot; hardware setup and recovery | `omarchy update`, `channel`, `kernel`, `plymouth`, `setup direct boot`, hardware routes; package locks | Existing terminal workflows → P4 coordinated status/jobs; dangerous boot/hardware operations stay guided and capability-gated |
| C19 Startup and services | Startup apps; user/system services and timers; enable/disable/start/stop/restart; failure state; environment/session settings | `autostart.lua`; XDG autostart; systemd; Omarchy restart commands; relevant user config | Supported config/service helper access → P4 native known service/startup management; system operations require proper authorization |
| C20 Developer environment | Terminal settings; editor/Tmux/Herdr preferences; Git; toolchains/mise; containers/databases; AI tool defaults/accounts; Windows VM; gaming helpers | Omarchy setup/install/remove/mise/agent/windows/games routes; shipped app config files; installed helpers | Existing setup and app preference workflows → P4 task-specific links and selected typed controls. Credentials remain with the owning tool |
| C21 Hooks and extensions | Theme/font/boot/update/battery hooks; custom menus; user plugins; theme templates; custom Lua; declared extension settings | `omarchy hook`, `plugin`; `~/.config/omarchy/hooks`, extensions, plugins, themed; user Lua imports | Show locations and supported editor/manage actions → P4 reviewed extension management; never infer editable controls from arbitrary code |
| C22 Diagnostics and resets | Versions/components; service/config errors; hardware summaries; logs; restart helpers; reset individual configs; factory reset; diagnostic export | `omarchy version`, `debug --no-sudo --print`, restart/refresh/reinstall/setup-reset routes; system logs | Read-only status plus reviewed helper routes → P4 redacted export/recovery center; consequential resets clearly separated from ordinary settings |
| C23 Capture and desktop tools | Screenshot/recording options; webcam overlays; OCR/QR; screenshot/share picker preferences; notes/calendar/reminders configuration; utility-specific preferences | Omarchy capture/share/notes/calendar/reminder routes; portal and installed app config | Existing tools and their configuration destinations → P4 common task settings; performing the utility task itself remains with its owner |
| C24 Control-center preferences | Search/pins; window state; change history retention; compatibility status; diagnostic export; owned overrides; CLI/agent interface | Application-owned XDG files and reviewed provider registry | P1 basic app preferences → P2 history/undo; P4 diagnostics; P5 support/coverage reporting |

## Installed helper choices

Detection is evidence of installation only. P0/P1 must validate launch arguments, the actual desktop/session behavior, and meaningful availability checks. Prefer desktop-entry launching where available instead of guessing command-line flags.

| Task | Available on the audited machine | Planning consequence |
| --- | --- | --- |
| Network configuration | Omarchy network panel, `nmcli`, `nmtui` | Use the panel or nmtui; `nm-connection-editor` is not installed |
| Bluetooth | Omarchy Bluetooth panel, `bluetoothctl` | Do not depend on Blueman or Blueberry; both are absent |
| Sound | Omarchy audio panel, `wpctl`, `pactl` | pavucontrol and pwvucontrol are absent; a mixer handoff needs detection/installation or the existing panel |
| Printing | `system-config-printer`, CUPS tooling | Launch the installed printer settings workflow |
| Storage | `gnome-disks`, Snapper, `limine-snapper-restore` | Separate disk operations, system snapshot recovery, and personal backups; GParted is absent |
| Input methods | `fcitx5-configtool` and Omarchy typing setup | Reuse established layout/input-method setup |
| Dictation | `superwhisper` | Reuse its documented setup interface; detect other backends separately |
| Display/brightness | Omarchy display panel, `hyprctl`, `brightnessctl`, `ddcutil`, `hyprsunset` | Native arrangement needs the recovery work; nwg-displays is absent |
| Power | `powerprofilesctl`, `upower`, Omarchy power-profile commands | Use Omarchy setters to preserve remembered AC/battery preferences |
| Firewall | `ufw` | No `firewall-config` helper is installed; native status and a curated authorized workflow are needed |
| Diagnosis | `btop`, `fastfetch`, Omarchy debug commands | Existing tools supply useful handoffs; redact exported output |
| Authentication | `pkexec`; Omarchy shell's polkit-agent plugin | Use the existing authorization agent; do not add a competing prompt or run the entire GUI as root |
| Native app references | Hype, Monologue, Omacut, Signed | Compare actual packaging, keyboard behavior, and theme response during development |

A missing helper should offer a supported installation route where one is known. Installation is an explicit user action, never a side effect of opening a settings page. A discovered executable is not sufficient to authorize arbitrary arguments.

## Source-of-truth map

| Surface | Read/write ownership strategy |
| --- | --- |
| `/usr/share/omarchy/{bin,config,default,themes,shell}` | Read-only package-owned reference; never write here for local settings |
| `~/.config/hypr/*.lua`, including custom imports | Read effective state through Hyprland; use supported commands or an explicitly integrated owned override; advanced access for ambiguous custom logic |
| `~/.config/hypr/hyprsunset.conf` | Night-light config; its own service reload, independent of Hyprland validation |
| `~/.config/hypr/xdph.conf`, `~/.config/xdg-desktop-portal/`, share-picker config | Portal/share behavior; verify the owning component's apply/restart semantics |
| `~/.config/omarchy/shell.json` | Preserve unknown keys, selected plugin IDs, clone restore data, widget instance identity, and per-instance options; merge only intended changes |
| Active theme `colors.toml` / `shell.toml`; user `~/.config/omarchy/shell.toml` | Theme files provide live app styling; persistent customization belongs in user overrides/themes/templates |
| `~/.config/omarchy/{themes,backgrounds,themed,branding,extensions,hooks,plugins}` | Respect user ownership and executable content; use appropriate management commands and safe file handling |
| `~/.config/omarchy/defaults`, language/font-related state, XDG handlers | Prefer owning Omarchy setters; account for commands that install, reload, or launch apps |
| `~/.local/state/omarchy/` | Observe effective toggle/profile/current-theme state; use owning setters instead of treating all state files as normal editable config |
| Terminal/editor/Tmux/Herdr/Git/app configs | Integrate known keys or open the owner's configuration; preserve unrelated content and theme-managed sections |
| NetworkManager, BlueZ, PipeWire/WirePlumber, systemd, UPower, logind | Prefer supported service interfaces and state events; distinguish runtime state from persistence |
| `/etc/` and root-owned storage/auth/boot settings | Existing service authorization or verified terminal workflow; no unrestricted privileged editor |

## Coverage accounting and maintenance

During P0/P1, convert this domain map into a machine-readable ledger at setting/workflow granularity. Each row must contain:

`id, domain, source references, searchable labels, applicable capability, read method, integration mode, write or launch method, apply/restart semantics, recovery class, supported versions, implementation status, validation evidence, known limitation`.

Suggested statuses are `unclassified`, `planned`, `implemented`, `verified`, and `excluded-with-reason`. Integration mode is separate: `native`, `omarchy-panel`, `omarchy-wizard`, `external-helper`, or `advanced-config`. Do not count an implemented but untested provider as verified.

For each release:

1. Capture all public and hidden command metadata. Split bundled subcommands into user workflows where appropriate. Hidden commands remain internal unless a reviewed user-facing workflow needs them.
2. Reconcile stock menu nodes, dynamic providers, user extension identifiers, default config files, and every `*manifest.json`, including bar widget manifests. Catalog first-party settings not exposed in a menu, and enumerate relevant service settings separately.
3. Map duplicate entry points to one setting/workflow. For example, a default browser choice may occur in the CLI, menu, and XDG handler data.
4. Assign new entries to a domain and review their behavior. Never execute menu conditions or invoke unknown commands simply to classify them.
5. Inventory supported keys in JSON/TOML and reviewed Lua helpers. Track unknown/custom logic as an explicit advanced route, not a complete inferred settings schema. Plugin manifests currently describe identity/kinds/entry points; they do not imply a universal typed settings schema.
6. Verify native controls and actual helper launches, including absence, permission denial, and shell-not-running cases. Re-read state after a change before recording success.
7. Report separately: verified reachable workflows, verified native editors, verified helper/wizard routes, advanced-only routes, exclusions, and outstanding gaps. Never merge these into a misleading “everything supported” percentage.
8. On Omarchy/package upgrades, compare catalog signatures and source fingerprints, refresh capabilities, and quarantine incompatible writes until reviewed. Tests must catch new unclassified first-party user-facing entries.

The release definition is zero unclassified in-scope workflows for the tested Omarchy baseline, with a clear route for each. This does not imply that every Arch package preference or every expression in a user's Lua program has a bespoke editor.

## Reproducing the audit

The inventory was captured using read-only command metadata, selected package queries, helper-path detection, packaged menu/config/plugin files, and counts of user plugin manifests. The relevant discovery commands are:

```bash
omarchy version
omarchy commands --json
omarchy commands --json --all
pacman -Qi omarchy-settings
pacman -Q omarchy hyprland quickshell qt6-base qt6-declarative qt6-svg qt6-wayland
rg --files /usr/share/omarchy/config /usr/share/omarchy/shell/plugins
```

`command -v <helper>` establishes executable availability. Parse stock menu nodes as data and include dynamic provider references; do not run their actions or conditions. Include both `manifest.json` and `*.manifest.json` when counting plugins/widgets.

The JSON snapshot includes SHA-256 fingerprints of the important packaged sources, no private configuration contents or device/account/network identifiers, and the capture time. Re-running discovery may produce different counts after updates. No settings-change command or helper launch was part of this audit.

## Initial command-group routing

The appendix below assigns every discovered command group a planning destination. Mixed groups need per-command review; this routing is not an execution allowlist or a claim of full settings coverage. All route metadata remains available in the JSON inventory.

| Group | Public / hidden entries | Planning destination | Treatment |
| --- | --- | --- | --- |
| `agent` | 11 / 27 | C20, C22 | Defaults, account workflows, and diagnostics; separate internal credential helpers. |
| `apply` | 0 / 5 | Internal | Provisioning/apply internals; expose only through a reviewed owning workflow. |
| `ascii` | 1 / 0 | Utility | Rendering utility; branding preferences belong to C01. |
| `audio` | 9 / 0 | C07 | Audio controls and read-only state; verify routing semantics. |
| `bar` | 1 / 1 | C02 | Bar layout and widget options; preserve active custom bar ownership. |
| `battery` | 2 / 1 | C12 | Battery status and capability detection. |
| `bluetooth` | 2 / 0 | C08 | Radio and device operations; pair through the appropriate agent. |
| `boot` | 0 / 1 | C18 / Internal | Inspect boot workflow use; no generic internal command button. |
| `branding` | 2 / 1 | C01 | About and screensaver branding; internal render helpers stay internal. |
| `brightness` | 5 / 0 | C04, C05 | Display and keyboard brightness; hardware-specific adapters. |
| `calendar` | 2 / 0 | C15, C23 | Calendar workflow/configuration; service internals stay internal. |
| `capture` | 6 / 3 | C13, C23 | Capture preferences and tool launches; exclude low-level helpers from settings navigation. |
| `channel` | 2 / 0 | C18 | Update channel selection and current state. |
| `chromium` | 0 / 3 | C14, C20 / Internal | Inspect browser integration helpers; surface only meaningful preferences. |
| `clipboard` | 0 / 3 | C13 / Internal | Privacy/configuration destinations; clipboard mechanics remain internal. |
| `cmd` | 2 / 3 | Internal | Executable availability probes, not user settings. |
| `crash` | 1 / 1 | C22 | Crash notification preference; diagnostics internals remain internal. |
| `debug` | 2 / 0 | C22 | Read-only diagnostics; always use the documented non-sudo print mode. |
| `default` | 5 / 0 | C14, C20 | Default tools; inspect install/launch side effects for every setter. |
| `dev` | 11 / 1 | Advanced development | Omarchy contributor workflows; documented exclusion from ordinary machine settings. |
| `dictation` | 3 / 1 | C06 | Backend selection/configuration links; recording action remains with its owner. |
| `disk` | 1 / 0 | C17, C22 | Optional disk diagnostic; explicit launch, never automatic benchmarking. |
| `display` | 1 / 0 | C01, C04, C06 | Global text sizing with multiple affected components. |
| `dns` | 1 / 0 | C10 | DNS configuration; inspect scope and authorization. |
| `dock` | 1 / 0 | C02 | Position, visibility policy, and pinned entries. |
| `done` | 0 / 1 | Internal | Workflow presentation helper, not a setting. |
| `drive` | 3 / 0 | C17 | Disk information and guided encryption-password workflow. |
| `file` | 1 / 0 | Internal | File selector helper, not an independent setting. |
| `font` | 4 / 0 | C01, C15 | Font family and CJK glyph preferences. |
| `games` | 2 / 0 | C14, C20 | Gaming environment installation/setup helpers. |
| `git` | 0 / 1 | C20 / Internal | Git integration; expose setup preferences through reviewed workflows. |
| `hibernation` | 3 / 0 | C12, C18 | Capability check and guided configuration affecting swap/boot. |
| `hook` | 2 / 0 | C21 | Hook discovery/installation; installed code remains user-managed. |
| `hw` | 36 / 6 | C09, C18, C22 | Hardware-matched setup and recovery; never offer all model-specific routines indiscriminately. |
| `hyprland` | 24 / 6 | C03, C04, C05 | Split persistent settings from window actions and internal runtime helpers. |
| `i18n` | 0 / 1 | C15 / Internal | Translation internals; public language preference uses language routes. |
| `input` | 0 / 1 | C05 / Internal | Input helpers; setup routes own normal configuration. |
| `install` | 45 / 1 | C09, C14, C20 | Curated application/device/development setup; verify dependencies and prompts. |
| `installed` | 0 / 2 | Internal | Installed-state queries supporting capability discovery. |
| `kernel` | 1 / 0 | C18 | Kernel discovery and guided boot selection. |
| `language` | 1 / 0 | C15 | Language and formatting; internal seeding is not a normal setting. |
| `launch` | 22 / 2 | Integration infrastructure | Reviewed app/helper launches; most routes are not settings themselves. |
| `lifecycle` | 0 / 1 | Internal | Desktop lifecycle internals; keep behind owning operations. |
| `locale` | 0 / 1 | C15 / Internal | Locale plumbing; invoke only through the validated language workflow. |
| `menu` | 12 / 1 | Cross-domain navigation | Existing panels/menus and dynamic providers; input helpers remain internal. |
| `migrate` | 2 / 0 | C18 / Internal | Update-owned migrations, not arbitrary repeatable settings actions. |
| `mise` | 1 / 1 | C20 | Development tool setup; implementation helpers stay internal. |
| `monitor` | 1 / 0 | C04 | Structured display-panel state where verified. |
| `n1x` | 1 / 0 | C09, C22 | Hardware-specific probe; show only when applicable. |
| `network` | 6 / 1 | C10 | State, band, captive portal, QR, and diagnostics; protect secret-bearing operations. |
| `notes` | 2 / 0 | C23 | Tool preferences/access; note-editing mechanics are outside control-center scope. |
| `notification` | 5 / 1 | C13 | Preferences and user actions; internal event emission is not a setting. |
| `nvim` | 2 / 0 | C20 | Editor setup and guided refresh. |
| `openclaw` | 1 / 0 | C20 | Existing onboarding workflow; do not duplicate authentication. |
| `osd` | 1 / 0 | C02, C13 / Internal | OSD component/configuration; event emission remains internal. |
| `pkg` | 9 / 1 | C14, C18 | Package workflows and detection; preserve package-manager locking. |
| `plugin` | 8 / 1 | C02, C21 | Plugin catalog and maintenance; manifests are not a general settings schema. |
| `plymouth` | 7 / 0 | C01, C18 | Boot appearance with privileged apply semantics. |
| `power` | 1 / 0 | C12 | Power capability detection. |
| `powerprofiles` | 3 / 0 | C12 | Remembered AC/battery preferences; startup initialization stays internal. |
| `provision` | 0 / 4 | Internal | Provisioning operations; not generic user settings. |
| `refresh` | 12 / 0 | C22 | Reset-to-default workflows with scope, backups, and confirmation. |
| `reinstall` | 3 / 0 | C22 | Guided recovery operations; never a silent repair step. |
| `reminder` | 1 / 0 | C13, C23 | Existing reminder workflow and settings. |
| `remove` | 34 / 1 | C14, C16, C20 | Curated removals; distinguish packages, credentials, and security configuration. |
| `restart` | 15 / 1 | C19, C22 | Component-specific recovery; explain interrupted services. |
| `screensaver` | 1 / 0 | C12 | Screensaver preferences and existing launch behavior. |
| `security` | 0 / 1 | C16 / Internal | Authentication internals; existing guided setup remains authoritative. |
| `setup` | 10 / 0 | C05, C16, C18, C20, C22 | Split keyboard, authentication, Git, boot, and factory-reset workflows. |
| `share` | 1 / 0 | C11, C23 | Explicit sharing helper; no background upload. |
| `shell` | 1 / 1 | Integration infrastructure | Verified IPC for panels and status; internal shell management stays internal. |
| `show` | 2 / 0 | Internal | Workflow presentation helpers, not settings. |
| `sleep` | 0 / 1 | C12 / Internal | Session/sleep internals; use owning user-facing actions. |
| `snapshot` | 1 / 0 | C17 | Create/restore system snapshots; distinguish scope from personal backups. |
| `state` | 0 / 1 | Internal | State bookkeeping; do not expose an arbitrary state-file editor. |
| `sudo` | 2 / 1 | C16 / Internal | Guided sudo policy where appropriate; keepalive/auth plumbing stays internal. |
| `system` | 4 / 5 | C12, C22 | Session actions and system state; internal operations require individual review. |
| `tailscale` | 2 / 0 | C11 | Existing transfer/network workflows; Tailscale panel supplies additional settings. |
| `theme` | 17 / 23 | C01 | Themes/backgrounds and management; regeneration/cache helpers remain internal. |
| `toggle` | 15 / 1 | C02, C03, C05, C12, C13 | Map each flag to a typed setting; inspect inverted semantics and toggle-only races. |
| `transcode` | 2 / 0 | Utility | Media transformation belongs to its tool; optional preference links under C23. |
| `tui` | 3 / 0 | C14 | Terminal-app installation/removal workflows. |
| `update` | 14 / 9 | C18, C15 | System maintenance plus time synchronization; internal stages stay update-owned. |
| `upgrade` | 1 / 0 | C18 / Historical workflow | Version-specific migration; expose only if compatibility checks establish applicability. |
| `upload` | 0 / 1 | C22 / Internal | Diagnostic upload is a separate explicit action; local export is the default. |
| `version` | 3 / 1 | C22 | Version and package/channel diagnostics. |
| `voxtype` | 2 / 0 | C06 | Optional dictation configuration and model management. |
| `weather` | 3 / 0 | C15 | Weather location/status; rendering helpers remain internal. |
| `webapp` | 5 / 1 | C14 | Web app registration/removal and launch-handler preferences. |
| `windows` | 2 / 0 | C20 | Existing Windows VM/setup helpers, with their own requirements. |
