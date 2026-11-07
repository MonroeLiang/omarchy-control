# Icon theme selection and import plan

Status: implemented through I2, with I3's automated and visual checks done. What is left of I3 needs a real session, and is listed under [Icon packs](LIVE-CHECKS.md#icon-packs) in the live checks. Created and built November 6, 2026, against Omarchy 4.5.0-3. Where this plan and [what the audit found](#what-the-audit-found) differ, the audit is what shipped.

Add an **Icons** section to Appearance so a user can preview and select a complete installed icon pack. Follow with local folder import through a file chooser or drag and drop. Users obtain their own packs; browsing this section never downloads software or installs packages.

This is a separate feature plan alongside [the main implementation plan](PLAN.md). It covers application, folder, and file icon themes across verified desktop consumers. The app's bundled interface glyphs retain their existing design.

## What the audit found

I0 read the installed scripts and tested against GLib's keyfile backend. It settled the plan's open questions as follows.

| Question | Finding | What shipped |
| --- | --- | --- |
| What applies icons on Omarchy? | `omarchy-theme-set-gnome` sets one GSettings key from `icons.theme`, or `Yaru-blue` when the theme has none. Omarchy ships no GTK `settings.ini`, and sets `QT_QPA_PLATFORMTHEME=gtk3`, so Qt follows GTK | GSettings is the one required consumer |
| Other consumers | GTK `settings.ini`, `qt5ct`, `qt6ct`, and `kdeglobals` are absent on the stock install | Each is updated only if it already names an icon theme, and `qt*ct` only if Qt is told to use it. None is ever created. A reapply leaves a file alone if it was edited by hand |
| Launcher and dock | `AppLibrary.qml` builds its own index of app and device icons across every installed pack, keeping the first match, and prefers that to a themed lookup | Not a supported consumer, and the row says so. An imported pack can change them even when it is not chosen |
| Does login reset it? | No. It is set at first run and on theme changes, and dconf keeps it | No login integration. Not yet observed on a real login |
| Hook ordering | `omarchy-theme-set` runs the hook after its retint batch, with the batch lock released, and always runs it, even when that batch's retints were skipped | The hook gets the theme name and does nothing if a newer theme has replaced it, since that switch's hook is still to come |
| An unset key | `gsettings get` reports the schema default, which cannot be told from the same value set on purpose. `dconf read` prints nothing | Recorded in the journal. Undo uses `gsettings reset` for a key that was never set. With another backend, or without dconf, it restores the value as a set one |
| Themes naming packs that do not exist | `vantablack` asks for `Yaru-gray` and `white` for `Yaru-grey`, and neither is installed | Reported as in use but not installed, and left alone |

Three smaller departures from this plan:

- The picker is `src/IconPackTiles.qml`, hosted by the row like the theme tiles, and not `BlockIcons.qml`, so it keeps the row's pin and details actions and search can land on it.
- A pack's value is `pack:<id>`, with `follow-theme` kept apart from any pack name. The CLI accepts a bare id.
- Copying an import holds its own lock, and takes the change lock only to publish, so a long copy cannot make a theme change's hook give up. A failed or cancelled import changed nothing and is not in History.

To remove the integration before uninstalling, run `omarchy-control icons remove-integration`. It removes the unchanged hook and the saved choice, and leaves the icons as they are. A hook left behind does nothing once the app is gone.

## Requested behavior and delivery scope

The request has two parts: choose an installed icon pack in the same way as GNOME Tweaks, and make a downloaded custom pack usable without manually finding its destination folder.

| Delivery | Included | Completion requirement |
| --- | --- | --- |
| First release | Discover installed packs, previews, selection, Follow desktop theme, persistent fixed selection, history and undo | Verified application and recovery on the supported Omarchy baseline, including theme changes while Control is closed |
| Second release | Import an extracted theme folder with a chooser or drag and drop | Validated copy into the user's icon directory, collision handling, cancellation, and recovery without modifying the source |
| Later extension | Local archive import | Separate extraction review and tests before ZIP or tar formats are accepted |

An online catalog, remote URLs, package installation, pack editing, and removing existing packs are outside this feature. The reference [Omarchy Icons plugin](https://github.com/rosakodu/omarchy-icons) includes a catalog and package installation as well as switching; use its integration surfaces as research rather than adopting its full product scope or requiring the plugin at runtime.

## User experience

Keep the feature inside the existing Appearance page, near Theme and Text. Use the current typography, spacing, controls, and restrained selection treatment. The primary job is to recognize a pack and choose it.

The section contains:

- The current effective icon theme and an explanation if desktop consumers disagree.
- **Follow desktop theme**, showing the icon pack supplied by the active Omarchy theme.
- Installed pack choices with readable names and a few representative application, folder, and document icons. Previewing a pack changes only the preview.
- Search when the installed list is large; selection remains visible when filtering.
- In the second release, **Import folder…** and a drop target with the same behavior and validation.

Selecting an installed pack applies the reversible change immediately through the settings engine. Show the selected state as confirmed only after verification. Disable conflicting operations while a change is running, retain keyboard focus, and expose any applications that need reopening.

Importing and selecting are separate operations. After a successful copy, show **Imported** and offer **Use this pack**. A drop never changes the active theme by itself. Display ordinary consequences in plain language; put paths and backend details in the existing expandable details pattern.

| State | Expected behavior |
| --- | --- |
| Loading | Show progress without presenting an old selection as freshly verified |
| No additional packs | Keep the current theme visible and explain how to add a local pack |
| Unknown or externally configured theme | Show the effective name; opening Appearance does not adopt or replace it |
| Missing selected pack | Preserve the preference, explain the missing pack, and offer another selection or Follow desktop theme |
| Mixed GTK and Qt values | Show the disagreement and allow a deliberate selection to reconcile supported consumers |
| Backend unavailable | Keep discovery and previews available; explain which application targets cannot be updated |
| Changed elsewhere | Refresh the effective value and show the conflict; refreshing never forces the saved selection back |
| Import invalid or cancelled | Keep the active theme and original folder unchanged |

Support keyboard selection and import, visible focus, accessible names and checked states, translated strings, RTL, large text, and reduced motion. Check the smallest supported window width and long pack names. Drag and drop must have a keyboard equivalent.

## Discovery and preview

Add an injected icon discovery model using paths from `Env`, including explicit support for `XDG_DATA_HOME` and `XDG_DATA_DIRS`. The current `Env` does not expose these data roots. Validate absolute environment paths and keep all test roots isolated. The normal user data directory is `~/.local/share`; custom roots must remain supported under the [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/latest/).

Discover packs from the user data directory's `icons/`, legacy `~/.icons/`, and each system data directory's `icons/`. Inspect theme descriptors rather than recursively reading every image on page load. Preserve the distinction between the internal directory name and the translated display name. Respect hidden themes and exclude cursor-only packs from this picker. Themes can be layered across roots; resolve precedence and overlays against the supported toolkits rather than simply dropping every duplicate directory.

A theme's `index.theme` describes its metadata, icon directories, and inheritance. Implement bounded inheritance lookup and the required fallback behavior from the [Icon Theme Specification](https://specifications.freedesktop.org/icon-theme/latest/). Missing optional sample icons should use fallbacks; malformed metadata or missing parents should have useful diagnostics.

Preview lookup must be scoped to the candidate theme. Do not change the process-wide icon theme to render a card, reuse the bundled `image://icons/` namespace, or change the active desktop preference. Add a separate image provider with bounded decode sizes, asynchronous loading where appropriate, and cache keys that change when pack contents change. Label inherited preview samples where necessary so the preview does not misrepresent sparse packs.

Debounce directory watches, re-arm them after atomic replacement, and refresh when the app regains focus. Cache descriptors and resolve only visible preview samples. A scan must not run an installer, execute a pack's scripts, or reach the network.

## Application and persistence

### Establish the supported integration contract

The installed `/usr/share/omarchy/bin/omarchy-theme-set-gnome` reads `~/.local/state/omarchy/current/theme/icons.theme` and applies it through GSettings, falling back to `Yaru-blue` on the audited version. It also changes other appearance settings, so invoking the whole script is not a scoped icon setter. Recheck its source and fallback in the first milestone.

The plugin documents writes to several consumers. Treat the following as an audit list, with a read, write, notification, and verification contract for each before adding it to the supported set:

| Surface | Proposed treatment |
| --- | --- |
| `org.gnome.desktop.interface` / `icon-theme` | Read and change only this GSettings key through a session-aware adapter |
| `~/.config/gtk-3.0/settings.ini` and `gtk-4.0/settings.ini` | Patch only `gtk-icon-theme-name` in the appropriate group when required by the tested configuration |
| `~/.config/qt5ct/qt5ct.conf` and `qt6ct/qt6ct.conf` | Update the icon key only when that configuration tool is the active Qt platform theme |
| `~/.config/kdeglobals` | Support the Icons theme key when the installed KDE/Dolphin configuration uses it |
| Omarchy launcher, dock, panel, and file manager | Verify their actual icon source and refresh behavior separately; never equate a settings write with a visible refresh |
| Current Omarchy `icons.theme` | Read as the desktop theme's default; keep the user's fixed override in Control's own configuration |
| `~/.icons/default/index.theme` | Audit its purpose before touching it; do not change cursor configuration to apply application icons |

The paths above show defaults. Resolve each backend's real configuration location and supported ownership rules; retain Omarchy's documented fixed paths where its commands require them. Do not create configuration for an unused toolkit merely to make every file contain the same string. Environment overrides and sandboxed applications can affect coverage; record these as per-consumer results.

### Persist the user's choice

Proposed application preference: `$XDG_CONFIG_HOME/omarchy-control/icons.json`, with a versioned schema, a mode (`follow-theme` or `fixed`), and the exact internal theme ID for fixed mode. Use unambiguous catalog values so a pack name cannot collide with Follow desktop theme.

With no preference, read existing desktop state and leave it alone. An explicit fixed selection persists across app restarts, login, and Omarchy theme switches. Follow desktop theme removes the fixed override and immediately resolves and applies the active desktop theme's icon choice to the supported consumers. It must not restore a stale icon name captured before a different desktop theme was selected.

Persisting a name in `icons.json` alone cannot satisfy this contract while Control is closed. Use an owned additive hook at `~/.config/omarchy/hooks/theme-set.d/90-omarchy-control-icons`, calling a proposed narrow headless `omarchy-control icons reapply` command. Include hook creation in the first managed selection's change plan and journal. Preserve existing hooks and detect edits to the owned hook.

Keep this integration active in both managed modes: fixed mode reapplies the chosen pack, and Follow desktop theme resolves the current desktop theme and synchronizes consumers that stock Omarchy does not update. Returning to Follow clears the fixed pack ID rather than leaving stale GTK/Qt overrides behind. Undoing the first managed selection restores the prior unmanaged state and removes only the unchanged hook owned by that selection. Document equivalent integration cleanup for uninstalling Control.

The audited `omarchy-theme-set` runs theme hooks after its appearance batch, and `omarchy-hook` supports additive hook directories. Prove ordering under rapid consecutive theme switches, detached batches, and login before relying on it. The headless action must resolve the current theme at execution time, reject stale generations, serialize with relevant theme changes, and avoid recursively changing the desktop theme. It must work without starting a GUI or keeping a daemon alive.

Choose the supported login integration during the prototype if login resets the saved selection; use an existing additive lifecycle mechanism and include it in the same ownership contract. Fixed mode is not release-ready until both login and closed-app theme switching pass. Keep package-owned scripts and theme source directories untouched, and avoid rewriting the generated current theme to store the preference.

Treat another icon manager as a possible concurrent writer. Do not install the reference plugin automatically, run competing background correction loops, or silently take over its configuration. Surface external changes and require an explicit user selection to reconcile them; the documented theme and login integration is the only automatic reapplication path. If coexistence produces repeated conflicts, suspend that automatic path and explain how to resolve ownership.

## Transactions and recovery

Use `ChangeCoordinator` for planning, locking, history, and verification. A multi-consumer icon change needs a durable snapshot of each touched file key and session setting, including whether the previous value was unset. Capture the preference and owned hook as well. Backing up the entire dconf database is unnecessary and would include unrelated settings.

The existing coordinator tracks files, and `SettingsEngine::undo` restores a logical setting value. That does not yet restore a collection of distinct backend values or directory trees. Extend the transaction metadata narrowly to preserve versioned before and after state for the icon provider; implement its undo through the same GUI and CLI dispatch. Retain compatibility with existing journal records.

For each selection:

1. Validate that the exact pack remains available and the necessary adapters are usable. Build a plan listing all intended files, keys, hook effects, and reloads.
2. Acquire the application change lock, recheck preconditions, and persist the snapshot and intent before mutation.
3. Apply scoped changes asynchronously. Record each completed step and its resulting state so a later failure can distinguish the app's work from an external edit.
4. Re-read each supported target and perform only the proven refresh operations. Report success only when all required targets agree; describe optional or unsupported consumers separately.
5. On failure, restore the touched values that still match this transaction's output. Report partial recovery if a concurrent writer prevents restoration.

Preserve unrelated keys, comments, permissions, and supported symlink relationships in INI files. Add a focused INI editor if needed; avoid a whole-file rewrite through a generic settings serializer. Ambiguous or unsupported files remain readable with an explanation for blocked writes.

Undo restores the exact prior mixed state, including absent keys and the former follow/fixed policy. It must refuse to overwrite later changes to the relevant values. A process crash is reported with recovery information on the next run; it must not trigger blind restoration. Tests must also cover GSettings changes, which file backups cannot recover on their own.

## Local folder import

Add import after the picker and transaction model are established. Both the folder chooser and drag and drop accept a single local directory containing `index.theme`. Keep the internal directory identity and variant relationships intact. Explain that archives must be extracted first until archive import ships.

Import to `$XDG_DATA_HOME/icons/<theme-id>/`, normally `~/.local/share/icons/<theme-id>/`. Copy rather than move the user's source. Preserve icon data and license files, and never run install scripts supplied with the pack.

The import sequence is:

1. Inspect the descriptor and declared directories, validate a usable theme, and report missing dependencies without downloading them.
2. Check the destination and other roots for the same theme ID. A system or legacy pack with that ID would be shadowed, so show that consequence. The first importer refuses an occupied destination and preserves it; replacement is a later extension.
3. Copy into a staging directory on the destination filesystem. Bound total bytes, file count, nesting, and image decode work. Support relative symlinks that remain within the imported pack; reject escapes, loops, special files, and unsupported links with an actionable error. Never rename a theme silently because inheritance can depend on its ID.
4. Revalidate the staged copy and source stability, then publish it with a no-replace operation. Cancellation or failure cleans up only this operation's staging directory.
5. Rescan the pack and report import success. Offer a separate action to select it.

Directory imports need their own journal metadata and manifest; `Request.files` alone is insufficient. Record staging and destination ownership before publication. Interrupted work is reported for review, and cleanup only removes an unchanged staging tree owned by that operation. Initially record a successful import as an action without automatic undo; selection remains separately undoable. Do not expose pack deletion through the general Undo button until directory ownership and external modifications can be checked reliably.

Archive support is a later extension with a separate dependency decision. Require the same validation plus extraction bounds, path and link containment, handling of wrapper directories and multiple themes, and tests for malformed inputs before accepting any archive format.

## Code integration

Keep discovery, filesystem operations, and session settings in C++; QML owns presentation and interaction. Proposed integration points are:

| Location | Planned change |
| --- | --- |
| `src/iconthemeprovider.h/.cpp` | Shared discovery, current state, application, follow/fixed policy, scoped previews, and per-consumer verification |
| `src/iconthemeimporter.h/.cpp` | Staged local folder import and its manifest, added in the second release |
| `src/appcore.h` and `src/settingsengine.h/.cpp` | Own/inject the shared provider and route `appearance.icons` read, choices, plan, apply, reset, and undo through it |
| `src/changecoordinator.h/.cpp` | Durable provider metadata and recovery for non-file settings; preserve existing transactions |
| `src/env.h/.cpp` | Inject data roots and session-setting adapters into system and sandbox environments |
| `src/fileops.h/.cpp` | Focused INI updates, only if the supported backend contract needs direct file edits |
| `src/backend.h/.cpp` | Expose picker models and import operations; refresh on focus and external changes |
| `src/BlockIcons.qml` and a separate preview image provider | Native choices, scoped sample icons, import feedback, and accessible keyboard interaction |
| `src/GenericPage.qml`, `src/data/pages.json` | Place Icons with the Appearance content; adapt the current block-before-sections layout only as far as placement requires |
| `src/data/settings.json` and `workflows.json` | Stable searchable `appearance.icons` entry and picker/import destinations without duplicate results |
| `src/cli.cpp` | Share the setting operations and add the reviewed headless reapply route; add import CLI support with the importer |
| `src/gui.cpp`, `src/qml.qrc`, `omarchy-control-sources.pri` | Register resources and new sources without replacing the bundled icon provider |
| `src/compat.h/.cpp` and `src/data/compat.json` | Gate the reviewed theme/hook integration and changed backend behavior |
| Tests, sandbox, screenshots, translations, coverage docs | Add fixtures and evidence alongside the implementation |

These are planned names, not existing APIs. Confirm current source structure when implementation begins, particularly the in-progress Appearance UI. Avoid a broad UI redesign or a generic plugin execution layer.

The GUI and CLI must share the same discovery, validation, journal, and undo behavior. Keep stable commands such as `get appearance.icons`, `plan appearance.icons <choice>`, and `apply appearance.icons <choice>`. Specify structured output for mode, selected theme, effective values, coverage, conflicts, and restart requirements before wiring the UI.

## Milestones and acceptance gates

### I0 Verify backends and ownership

- Audit the installed Omarchy icon application, theme hooks, login behavior, toolkit selection, and existing plugin interaction.
- Prototype reads and scoped writes in disposable environments. Establish how to snapshot unset GSettings values and how each consumer receives updates.
- Prove discovery precedence, preview lookup, rapid theme-switch ordering, and persistent fixed selection while Control is closed.
- Decide whether any additional library is necessary and record its packaging cost. Pin the reviewed commands and contracts through compatibility checks.

**Exit gate:** a tested source-of-truth and recovery contract for every supported consumer. Unsupported consumers have an explicit status; core theme and login persistence have working prototypes.

### I1 Implement installed pack selection

- Build shared discovery and previews, the `appearance.icons` setting, follow/fixed persistence, and the additive hook.
- Extend snapshots and undo for exact multi-consumer state, then connect the Appearance block and CLI.
- Add translation strings, sandbox themes, and search coverage.

**Exit gate:** discovery and browsing make no changes; selection, Follow desktop theme, external changes, failure recovery, and undo pass in fixtures. Verified live consumers follow the selection, and fixed mode survives desktop theme changes and login without the app running.

### I2 Implement local folder import

- Build the staged importer, manifest, collision handling, progress, and cancellation.
- Add the chooser and drop target, followed by the separate Use this pack action.
- Cover internal symlinks and inheritance used by real local packs; explain unsupported structures without modifying them.

**Exit gate:** successful import creates exactly one validated user pack; failure, cancellation, or interruption preserves the source, existing packs, and active selection. Import performs no network or package-manager work.

### I3 Verify release behavior and document coverage

- Run the appropriate project tests and a disposable integration session with representative GTK and Qt apps, the file manager, and Omarchy's launcher/dock.
- Visually verify light/dark themes, sharp/rounded settings, narrow windows, large text, RTL, keyboard operation, long names, and missing preview samples.
- Record settings persistence separately from visible application refresh. Test relaunch/login, external icon manager changes, rapid desktop theme switches, app closure during a transaction, and changed integration scripts.
- Update the coverage ledger, live checks, and user documentation with the exact tested consumers and any reopening requirement.

**Exit gate:** release notes describe observed coverage without promising that every application or bundled icon follows the selected pack.

## Verification matrix

| Area | Required cases |
| --- | --- |
| Discovery | Default/custom XDG roots, legacy root, duplicate/layered IDs, localized names, hidden and cursor-only themes, malformed descriptors, missing roots and inherited packs |
| Preview | Candidate isolation, inherited and missing icons, bounded decoding, changed files, no global theme mutation, many installed packs |
| Selection | Unmanaged initial state, follow/fixed transitions, mixed backends, unset prior values, missing tools/session bus, read-only or externally edited configs, supported symlinks |
| Recovery | Failure after each mutation, stale plan, concurrent editor/plugin, exact undo after unrelated changes, interrupted process, old journal records |
| Persistence | Theme changes from Control and other entry points in both modes, closed app, rapid switches, changed or missing owned hook, return to unmanaged state, uninstall cleanup, logout/login |
| Import | Valid pack, nested metadata, internal/escaping links, duplicate IDs, shadowing, changed source, low disk space, cancellation, interruption, invalid or oversized input |
| Interface | Search navigation, current/effective disagreement, accessibility, keyboard import, RTL, large text, long labels, repeated drops and double activation |

Use `Env::sandbox`, temporary roots, and fake commands/session adapters for automated checks. New discovery and preview code must never fall back to the real user's icons, configuration, or session bus in tests. Use the existing screenshot harness for visual checks and a disposable real-session environment for integration evidence. Run `bin/test` for the relevant implementation changes; saving this plan alone does not require an application build or a live settings change.

## References

- [Main implementation plan](PLAN.md), especially configuration ownership, transactions, and shared GUI/CLI behavior.
- [P0 findings](P0-FINDINGS.md) and [live checks](LIVE-CHECKS.md) for established recovery rules and validation reporting.
- [Omarchy Icons](https://github.com/rosakodu/omarchy-icons) for the referenced feature and documented backend surfaces.
- [Icon Theme Specification](https://specifications.freedesktop.org/icon-theme/latest/) for descriptors, lookup, layering, and inheritance.
- [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/latest/) for data and configuration roots.
- Local integration sources to re-audit at I0: `omarchy-theme-set-gnome`, `omarchy-theme-set`, and `omarchy-hook` under `/usr/share/omarchy/bin/`.
