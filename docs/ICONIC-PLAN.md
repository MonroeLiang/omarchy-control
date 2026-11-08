# Bringing Iconic into Appearance › Icons

Status: L1, L2, and L3 implemented on November 8, 2026, with their automated, visual, and nested checks done; what is left of them needs a real session (see [LIVE-CHECKS.md](LIVE-CHECKS.md#icon-packs)). L4 is next. Decisions settled by Jason on November 8, 2026 (see [§10](#10-decisions)). Written November 8, 2026, against Omarchy 4.5 and [Iconic](https://github.com/UnderStackDev/omarchy_iconic) at `bce1cd2`. It continues the [icon themes plan](ICON-THEMES-PLAN.md), whose I0–I2 shipped.

Iconic is a separate Omarchy app (MIT, © 2026 UnderStackDev) that picks icon themes, installs theme archives, and edits app launchers. Its key idea is a small icon layer that makes Omarchy's launcher and dock show the chosen pack, which on Omarchy they otherwise do not. This plan takes what Iconic does that Control lacks, fixes the weak spots found in its code, and keeps what Control already does better.

**In one line:** Control already picks packs better than Iconic does, so the work is four additions, plus Iconic's wider set of config files and a Credits panel in About:

1. The launcher icon layer, which matters most. On by default.
2. Archive import.
3. Removing a pack.
4. Editing an app's launcher.

---

## 1. How Omarchy handles icons, and how that differs from GNOME

### What Iconic's README says

> Omarchy's launcher and dock don't look app icons up in the active icon theme. They index every app icon from every installed theme and keep the first file they find per name, SVGs before PNGs. So switching the icon theme usually changes folders but not apps, and PNG themes such as Yaru never show up in the launcher at all.

Reading Omarchy's code confirms this, with more detail.

### Choosing the pack

- **Where the pack is named:** an Omarchy theme names its pack in a one-line `icons.theme` file. On a theme switch, `omarchy-theme-set-gnome` writes that name to one GSettings key, `org.gnome.desktop.interface icon-theme`, or `Yaru-blue` when the theme has no file (`bin/omarchy-theme-set-gnome:28-34`).
- **Nothing else is written:** there is no GTK `settings.ini`, no `kdeglobals`, and no `qt*ct`.
- **Qt apps follow GTK:** they read the same key through `QT_QPA_PLATFORMTHEME=gtk3` (`default/hypr/envs.lua:14`).
- **Packs that don't exist:** two shipped themes name packs that are not installed. `vantablack` asks for `Yaru-gray` and `white` for `Yaru-grey`.
- **Hooks:** they run after the retint batch, with the theme slug as `$1`, in glob order from `~/.config/omarchy/hooks/theme-set.d/` (`bin/omarchy-hook:26-39`). The other hook names are `post-update` and `font-set`.

### Showing app icons: two different paths in one shell

| Surface | How it finds an app's icon | Follows the chosen pack? |
| --- | --- | --- |
| Launcher, dock, dock app grid | `shell/services/AppLibrary.qml:137-163`: runs `find` over `~/.icons`, `~/.local/share/icons`, every `$XDG_DATA_DIRS/icons`, and `/usr/share/pixmaps`. Keeps files under `*/apps/*` or `*/devices/*`, lists every SVG before any PNG, and keeps the first file per name. That index is tried before a themed lookup (`:57-69`) | **No.** Every installed pack competes, and which one wins depends on directory listing order |
| Tray | Quickshell's `image://icon/` provider through Qt | Yes |
| Notifications | `Quickshell.iconPath(name)` through Qt | Yes |

The index is rebuilt on start, 750 ms after the app list changes, and every time the launcher or app grid opens. It exists to work around Qt's icon lookup cache, which never re-scans (`AppLibrary.qml:19-22`).

**On this machine today:** breeze is selected, yet the launcher shows HighContrast SVGs for the terminal and file manager, Adwaita for the printer, and a 16×16 HighContrast PNG for Firefox. Breeze never gets a say. Yaru is all PNGs, so it loses to any SVG anywhere, which is why Omarchy's own default pack rarely shows in its own launcher.

### GNOME, for comparison (well-known behaviour, not inspected here)

GNOME Shell draws every app icon from the single active theme:

- **Source:** the `.desktop` file's `Icon=` becomes a themed name or a file.
- **Lookup:** the active theme, then its `Inherits` chain, then `hicolor`, then `pixmaps`.
- **Sizes:** it picks the closest size and scale from `index.theme`.
- **Freshness:** it uses `icon-theme.cache`, watches the folders, and reloads live when the setting changes.

### The differences that matter for this plan

- **One theme versus all of them.** GNOME honours one theme plus its inheritance. Omarchy's launcher and dock take the first matching file from every installed pack.
- **Sizes and formats.** GNOME chooses by size; Omarchy chooses by format (any SVG beats any PNG) and listing order.
- **Two behaviours.** Changing the pack changes the tray, notifications, GTK apps and Qt apps, but not the launcher or dock.
- **Owning the choice.** GNOME's setting belongs to the user. Omarchy overwrites it on every theme switch, so keeping a choice needs a theme-set hook. Control already installs one: `90-omarchy-control-icons`.
- **Freshness.** GNOME watches for changes. Omarchy re-runs `find` on open.

The real fix belongs upstream: the index should resolve through the active theme. Until then, a layer that wins the index is the only way to make the launcher follow a pack without editing Omarchy.

### Upstream status (checked November 8, 2026)

The fix already exists upstream, so we should not write our own. Repository: `omacom/omarchy`, branch `quattro`.

| Item | What it is | State |
| --- | --- | --- |
| Issue [#7552](https://github.com/omacom/omarchy/issues/7552) | "AppLibrary icon index ignores the active icon theme" | Open since Aug 19. The review bot confirmed it on Oct 8 |
| PR [#9408](https://github.com/omacom/omarchy/pull/9408) | **The fix.** The index walks the configured theme, then what it inherits, then hicolor and pixmaps. It ranks icons by size and falls back to Adwaita when the configured pack is missing (vantablack, white) | Open since Aug 31. The review bot verified it on an ISO-built machine on Oct 13–14 and fixed small issues: "Nothing is waiting on you. This is with the maintainer." No maintainer reply yet |
| PR [#15167](https://github.com/omacom/omarchy/pull/15167) | Scan icon folders once at shell startup | **Merged by dhh, Oct 12.** Its test asserts that every theme is indexed. That is the opposite of #9408's design, so #9408 waits on a design choice by the maintainer |
| PR [#16190](https://github.com/omacom/omarchy/pull/16190) | Try the active theme's lookup before the index | Open. The bot says it brings back the Zoom icon mix-up and fails a test |
| PR [#10480](https://github.com/omacom/omarchy/pull/10480) | Prefer the active theme in the index | Closed Sep 21 as a duplicate of #9408 |
| PRs [#16934](https://github.com/omacom/omarchy/pull/16934), [#16382](https://github.com/omacom/omarchy/pull/16382) | Use the largest PNG | Open. #9408 already covers this |
| PR [#18090](https://github.com/omacom/omarchy/pull/18090) | `omarchy-shell shell reloadIcons` over IPC, so the dock reloads icons without a shell restart | Open. **Useful to us**, see §3 |
| PR [#17804](https://github.com/omacom/omarchy/pull/17804) | Index bare icon files in the icon folders | Open |
| PR [#15717](https://github.com/omacom/omarchy/pull/15717) | Start the shell without the GTK3 platform theme | Open. If merged, the tray and notifications may stop following the icon setting. Watch it |
| PRs [#17363](https://github.com/omacom/omarchy/pull/17363), [#14879](https://github.com/omacom/omarchy/pull/14879), [#17976](https://github.com/omacom/omarchy/pull/17976), [#18033](https://github.com/omacom/omarchy/pull/18033) | The packs vantablack and white name that aren't installed | Open, several competing |

What this means for the plan:

- **No upstream patch from us.** At most, a short comment on #9408 confirming it fixes what we see. That is Jason's call, and nothing is posted without him.
- **The layer still ships.** #9408 is stalled on a design choice, the maintainer's own #15167 went the other way, and nothing ships a fix today.
- **If #9408 lands, the layer stops working on its own.** The index would then only walk the configured pack's chain, which never includes `~/.icons/omarchy-control`. The compatibility pin on the scan (§3) notices the change and retires the layer, which is then removed, not left behind. L1 needs a fixture of #9408's scan to prove that.
- **If #18090 lands,** a layer rebuild calls `omarchy-shell -q shell reloadIcons`, detected first, so the dock refreshes at once.

---

## 2. Iconic and Control, feature by feature

| Feature | Iconic (from its code) | Control today | Decision |
| --- | --- | --- | --- |
| Choose a pack, with previews | Gallery. Previews fall back to `hicolor`, so a tile can show icons the pack doesn't own | Tiles that only search the pack's own `Inherits` chain, plus History and undo | **Keep Control's** |
| Keep the pack across theme switches | Follow or Lock. The hook ignores `$1` and installs itself even in Follow mode | Follow or fixed. The hook takes `$1`, gives way to a newer switch, and leaves hand edits alone | **Keep Control's** |
| GTK `settings.ini`, `kdeglobals`, `qt*ct` | Writes the GTK files and `kdeglobals` every time; `qt*ct` only if present. No read-back | Patches a file only if it already names an icon theme, and verifies the change | **Follow Iconic's logic** (decision 4), keeping Control's undo, read-back and hand-edit protection (§3a) |
| **Launcher icon layer** | `~/.icons/iconic-apps`. See the weak spots in §3 | None. The row says the launcher may not change | **Port, redesigned** (§3) |
| Install from an archive | `bsdtar` with header-declared limits, unpacked inside `~/.local/share/icons` | Extracted folders only, with staging, an atomic publish, shadow warnings and cancel | **Port**, feeding the existing importer (§4) |
| Remove an installed pack | Deletes user packs that aren't in use; doesn't check other packs' `Inherits` | Not offered | **Port, safer** (§5) |
| Rename, re-icon or hide an app's launcher | Override copy with `X-Iconic-Override`, in-memory undo, and rename deletes every translation | Not offered | **Port, last, on the Apps page** (§6) |
| Command line | `iconic …` | `omarchy-control icons …` | **Extend** `icons` and add `apps launcher` (§8) |

---

## 3. The launcher icon layer

### What it must do

For every launcher on the machine, put one file named exactly as its `Icon=` value where the shell's index finds it first. That file is the chosen pack's icon, or the app's own icon when the pack has none. Then the launcher, dock and app grid show the pack, with no change to Omarchy.

### Weak spots in Iconic's version, and what we do instead

| Iconic | Problem | Control |
| --- | --- | --- |
| Staging folders `.iconic-apps-<pid>` and `.iconic-apps-old` inside `~/.icons` | `find` enters dot-folders, so half-built or abandoned copies get indexed and can win | Build in `~/.icons/.omarchy-control-next/building/`, which the shell's `*/apps/*` filter never matches, so staging is always on the same filesystem. Rename `building` to `apps` only once it is complete, then swap the folder in with one `renameat2(RENAME_EXCHANGE)`. A complete copy is identical to the live one, so the moment both exist does no harm |
| Nothing written when the pack lacks an icon | Some other pack's icon shows instead, at random | Fall back to `hicolor`, then `pixmaps`, so the result is always predictable. Write nothing only when no icon exists at all |
| Symlinks into the pack | They break when a package upgrade renames files | Copy the files (SVGs up to 1 MiB; PNGs wrapped). A few MB, and the layer stands on its own |
| Rebuilds on the GUI and on the hook at the same time | Both share `.iconic-apps-old` | One `flock` on a layer lock, used by both the CLI and the GUI |
| Not rebuilt for newly installed apps | New apps stay unthemed until the pack changes | Add a `post-update` hook, a watcher while Control runs (now often, since it stays in the tray), and a check on start (see Triggers) |
| No way to see what it did | — | A manifest, a coverage count in the Icons row, and a per-app list in details |

### Contents

- **Folder:** `~/.icons/omarchy-control/apps/<Icon=>.svg`. The shell's scan lists `~/.icons` first and only keeps paths under `*/apps/*`.
- **No `index.theme`:** GTK, Qt, and Control's own pack list then ignore the folder, and it can never be chosen as a pack.
- **Manifest:** `~/.icons/omarchy-control/manifest.json` records the version, the pack it was built for, a build stamp, and each name with its source file, source pack and fallback reason.
- **PNG icons:** wrapped in an SVG (`<image href="data:image/png;base64,…">`) so they beat SVGs elsewhere. This is how a PNG pack like Yaru reaches the launcher. Take the largest PNG not over 256 px.

### How each icon is chosen

1. **Collect the names.** Read every `Type=Application` `.desktop` file in the XDG `applications` folders, including NoDisplay ones, since the dock matches windows to them. Take each `Icon=`. An absolute path is the app's own file and is left alone.
2. **Look the name up in the chosen pack and its `Inherits` chain**, using the existing `IconThemes::findIcon`. It ranks sizes by the spec's distance, aiming near 128 px for HiDPI, and prefers scalable icons. It searches every context, because some launchers use device icons (Print Settings uses `printer`).
3. **Try simpler names.** If the exact name isn't found, try the last dot-separated part and then lowercase, as Iconic does (`org.mozilla.firefox` → `firefox`). The file still gets the original `Icon=` name.
4. **Fall back.** If the pack has nothing, use the name from `hicolor`, then `/usr/share/pixmaps`. Record the reason.
5. **Follow mode:** the pack is whatever Omarchy's theme currently names, so the launcher matches the desktop theme. A pack that isn't installed (vantablack's `Yaru-gray`) builds no layer and says why.

### Triggers

- Choosing a pack, undoing a choice, or switching Follow and fixed.
- The theme-set hook, which already runs `icons reapply "$1"`. In both modes it now also rebuilds the layer.
- A new `post-update` hook (`~/.config/omarchy/hooks/post-update.d/90-omarchy-control-icons`). It only runs `icons layer rebuild --if-stale`.
- While Control runs, a debounced watch on the `applications` and icon folders. This covers web apps and TUIs installed by Omarchy's scripts.
- On start, `--if-stale` compares the manifest with the pack and a quick stamp of the launcher files.
- `omarchy-control icons layer rebuild` by hand.

A rebuild is derived state, not a change, so it never appears in History. Turning the layer on or off is a change, with undo.

### When the shell sees it

The launcher and app grid rescan on every open, so they update on the next open. The dock only rescans when the app list changes or the grid opens. L1's nested checks measure this. If the dock stays stale, the row says so ("The dock updates the next time an app opens or closes"), rather than us poking the shell. Once upstream #18090 ships `omarchy-shell shell reloadIcons`, a rebuild calls it when the shell offers it.

### Owning it and removing it

- **Ownership:** Control owns only `~/.icons/omarchy-control/` and the two hook files. `icons remove-integration` removes the layer and both hooks. The PKGBUILD has no scriptlets, so `README` says to run that command before `pacman -R`.
- **Omarchy changes the scan:** pin `AppLibrary.qml`'s scan command and the `iconSource` order in the compatibility checks (`tools/gen-compat.py`). If Omarchy changes them, the layer pauses itself and the row explains why, the same way the app already pauses settings whose commands changed.
- **Another pack in `~/.icons`:** if a user pack lives in `~/.icons` itself, `find`'s listing order is not ours to control. Detect this, test it in L1, and if it can lose, show a warning that offers to move that pack to `~/.local/share/icons` through the existing importer.

### Living with Iconic

If `~/.icons/iconic-apps` or `~/.config/omarchy/hooks/theme-set.d/iconic` exists, both apps will fight over the same key and the same index. Iconic's hook sorts after ours, so it gets the last word. The Icons section then:

- Shows a notice ("Iconic is also managing icons").
- Explains what that means.
- Offers **Hand icons over to Omarchy Control**. After a confirmation, that moves Iconic's hook, layer and `~/.config/iconic/config.toml` into `~/.local/state/omarchy-control/retired/iconic/`, so it can be undone. Iconic's launcher overrides in `~/.local/share/applications` are listed and left alone.

---

## 3a. Config files: following Iconic's logic

Decision 4 changes I1's rule that a file is only patched if it already names an icon theme. Each choice of pack, and each automatic reapply, now writes the same name to:

| File | Key | When |
| --- | --- | --- |
| `~/.config/gtk-3.0/settings.ini` | `[Settings] gtk-icon-theme-name` | Always; created if missing |
| `~/.config/gtk-4.0/settings.ini` | `[Settings] gtk-icon-theme-name` | Always; created if missing |
| `~/.config/kdeglobals` | `[Icons] Theme` | Always; created if missing |
| `~/.config/qt5ct/qt5ct.conf`, `~/.config/qt6ct/qt6ct.conf` | `[Appearance] icon_theme` | Only if the file already exists |

This reaches apps that never read GSettings: XWayland GTK3 apps with no settings daemon, and KDE apps that use KIconThemes.

What stays from Control's model:

- **Edits keep everything else.** Only the one key changes, through the existing `patchConsumers`, line by line, so other keys and comments stay. A symlinked file is written through to its target.
- **Undo is exact.** The journal records whether each file existed and its exact contents. Undo restores them, and deletes a file this change created.
- **Read-back.** Each file is read back after writing, as GSettings already is.
- **Hand edits win on automatic runs.** The theme-set hook only rewrites a file that still holds what Control last wrote. A deliberate choice in the window or CLI always writes.
- **The `effects` list tells you.** The plan dialog for the setting names each file it will create or change.
- **Removing the integration leaves the files as they are,** like the icons themselves. They name a pack that stays installed.

L1's nested checks confirm which apps actually gain from each file, but the result doesn't gate the change.

---

## 4. Archive import

- **Formats:** `.tar` (gz, xz, bz2, zst, lz4, lzma), `.tgz`, `.txz`, `.tbz`/`.tbz2`, `.zip` and `.7z`, from the chooser or a drop. The same Import row takes folders and archives.
- **Read with the libarchive library, not the `bsdtar` command.** Every Arch system already has libarchive, since pacman depends on it; the package gains a `libarchive` dependency. Reading entry by entry lets us count real bytes and entries rather than trust the archive's headers.
  - Limits: 2 GiB and 300,000 entries, the same as Iconic.
  - Each path is checked before it is written: no absolute paths, no `..`, and no device files, FIFOs or hard links.
  - A symlink is kept only if its target stays inside the archive.
- **Unpack outside every icon folder,** in `$XDG_CACHE_HOME/omarchy-control/.omarchy-control-import-<id>/`. Iconic unpacked inside `~/.local/share/icons`, which the shell scans while the copy runs.
- **Find the packs:** every `index.theme` that has sized `Directories`, so one archive can hold several packs. When the `index.theme` sits at the archive's root, the pack is named after the archive file; Iconic rejected these. Cursor-only themes are reported and skipped.
- **Then hand each pack to the existing `IconThemeImporter`,** which already inspects, stages, publishes atomically, warns about shadowing, and cancels cleanly. One archive gives one result per pack. The cache folder is removed afterwards, and `recoverStaging` is extended to clean it after a crash.
- **Packs that build on each other:** packs another in the archive inherits go in first. A link out of one pack is allowed only into another pack from the same archive, which is installed beside it, as Papirus-Dark links into Papirus; that pack's own links are checked when it goes in. The folder importer still refuses any link out of a lone pack.
- **One question per archive:** if any pack in it would hide a copy installed elsewhere, that is asked once, before anything is installed. Each pack that goes in is its own entry in History.
- **Icon cache:** after publishing, if `gtk-update-icon-cache` exists, run `gtk-update-icon-cache -f -t -q <pack>`. A cache that came inside the archive may be stale, so it is regenerated, or deleted if the tool is missing.

---

## 5. Removing a pack

- **What can be removed:** only packs under the user's icon folders. The tile menu offers Remove; the CLI is `icons remove <id>`.
- **When it is refused:** when the pack is the one in use, the fixed choice, or what the current Omarchy theme names. Also when another installed pack inherits it, which Iconic doesn't check, or when another pack's symlink points into it.
- **Where it goes:** to the Trash, laid out as the freedesktop spec says under the user's data folder, and recorded in History. Undo brings it back while it is still in the Trash and nothing has taken its place. Qt's `moveToTrash` is not used: it reads the process environment, so a test would reach the real Trash.
- **In the window:** a trash button on tiles of packs the user installed, shown on hover or keyboard focus, and the Delete key. A confirmation names the folder. The CLI is `icons remove <id> --yes`.

---

## 6. Editing an app's launcher: last phase, on the Apps page

This is about launchers more than icons, so it lives on Apps. The Icons section links to it ("Change one app's icon").

- **Packaged launchers:** a full copy in `~/.local/share/applications/<id>.desktop`, marked with `X-OmarchyControl-Override=true`. It also records `X-OmarchyControl-Source=<path>` and that file's hash.
- **Your own launchers:** edited in place after a backup.
- **Package updates:** a copy hides them, so when the source hash no longer matches, the app shows "Out of date" with **Refresh**. Refresh copies the new file and applies the same changes again. Iconic's copies stay stale for good.
- **Rename:** set `Name` and the `Name[<current language>]` key, and keep every other translation. Iconic deletes all of them. See decision 3.
- **Icon:** a pack icon by name, or a file. Files go in `~/.local/share/omarchy-control/icons/` under a content hash. Icons no override uses any more are removed.
- **Hide:** `NoDisplay=true`. Omarchy's own `launcher.hides` list is respected and shown.
- **Undo:** through Control's journal, so it survives a restart; Iconic's undo lives in memory only. Reset removes the override or restores the backup.
- **`update-desktop-database`:** run only when the entry has a `MimeType`. The shell watches the folder anyway.

---

## 7. What changes in Appearance › Icons

| Row | Change |
| --- | --- |
| **Icon pack** (`appearance.icons`) | Same tiles. The description no longer says the launcher may not change. A tile menu offers Remove for user packs |
| **New: Launcher and dock** (`appearance.icons.launcher`, on/off) | "Use the icon pack for app icons in the launcher and dock." Status: "142 of 150 apps use Breeze, 8 use their own icons", with details listing those apps and why. Notices for: paused by compatibility, Iconic present, a pack in `~/.icons`, dock refresh timing |
| **Import an icon pack** (`appearance.icons.import`) | Takes folders and archives. The chooser gets a file mode with archive filters, and the drop target accepts both |
| **New link: Change one app's icon** | Goes to the Apps page's launcher editor (§6) |
| **Launcher and dock**, first run | The layer is on by default (decision 1): the first pack choice after updating builds it, and the confirmation for that choice says the launcher and dock will change, with the switch to turn it off |

### Credits in About

About gets a **Credits** section, built like its Project section from link rows in `workflows.json` (`app.credits.*`), so it is searchable, keyboard-reachable, and translated with no new component. Each row gives a name, what it contributed, its license, and a link that opens in the browser:

- **Iconic** by UnderStackDev, MIT. "The launcher icon layer builds on its design." Links to https://github.com/UnderStackDev/omarchy_iconic. The line grows as L2 and L4 borrow more.
- **Inter** by Rasmus Andersson, SIL Open Font License. The interface font, already bundled with its license.
- **Omarchy** by David Heinemeier Hansson and contributors, MIT. The desktop this manages.

A test checks the rows and their icons. It landed in L1, alongside the first borrowed design.

The existing rules still apply:

- Search lands on each row, with keywords such as "launcher icons", "dock icons", "app menu icons", "iconic" and "icon zip".
- Panels go through `pages.json`, and new strings are translated in Danish, German and Arabic.
- RTL, keyboard-only use and sandbox screenshots are covered.

---

## 8. Command line

```sh
omarchy-control icons layer status            # pack, built-for, coverage, paused reason
omarchy-control icons layer rebuild [--if-stale]
omarchy-control icons layer on|off            # the setting, through the normal change path
omarchy-control icons import <folder|archive>... [--replace|--keep-both] [--use]
omarchy-control icons remove <id>
omarchy-control apps launcher list [--modified|--hidden]
omarchy-control apps launcher set <id> [--name ..] [--icon <name|file>] [--hide|--show]
omarchy-control apps launcher reset <id>
```

Every command takes `--json`, like the rest of the CLI.

---

## 9. Phases and exit gates

| Phase | Work | Exit gate | Size |
| --- | --- | --- | --- |
| **L1 Layer** | `IconLayer` (build, publish, lock, manifest), the setting (on by default) with undo, the triggers and both hooks, `remove-integration`, compatibility pins, Iconic detection and hand-over, the row and its details, the CLI. The wider config files (§3a). The Credits panel | Unit tests on sandbox packs; nested checks proving the launcher and dock follow a pack choice, a theme switch with Control closed, and a new app after `post-update`, plus the checks listed below; screenshots | L |
| **L2 Archives** | The libarchive reader feeding `IconThemeImporter`, file chooser and drop, icon cache | Hostile-archive tests (zip-slip, absolute paths, symlink escapes, lying headers, bombs, device files), multi-pack archives, an `index.theme` at the root, cancel and crash recovery | M |
| **L3 Remove** | Remove a pack via the Trash, with its refusal rules and undo | Tests for each refusal rule and for undo after outside changes | S |
| **L4 Launcher editing** | Overrides with their source hash, rename/icon/hide, Refresh, the journal, the Apps page UI and CLI | Tests for packaged and user launchers, a package update, translations kept, undo across a restart; a nested check that the launcher shows the edit | M |
| **L5 Release** | Docs, coverage ledger, credits, README screenshots, version bump | `bin/test`, the nested check, `package-check` and `icons-check` all pass | S |

There is no separate verification phase: Iconic already proves that a layer in `~/.icons` wins Omarchy's launcher. L1's nested checks cover only where this design differs from Iconic's:

- An atomic swap while the launcher is open shows no broken icons.
- Copied icons and SVG-wrapped PNGs render at HiDPI.
- How long the dock takes to pick up a rebuild.
- What happens when another pack lives in `~/.icons`.
- Which apps gain from each config file in §3a.

No upstream patch: #9408 already is one (§1, Upstream status). Before each phase starts, re-check #9408, #18090 and #15717, since any of them landing changes this plan.

---

## 10. Decisions

Settled by Jason on November 8, 2026.

1. **The layer is on by default once a pack is chosen,** with a switch to turn it off (§3, §7).
2. **Launcher editing is part of this effort,** as the last phase, L4, so L1–L3 can ship on their own.
3. **Renaming sets `Name` and the current language's `Name[xx]`,** and keeps every other translation.
4. **The config files follow Iconic's logic:** the GTK `settings.ini` files and `kdeglobals` are always written, and `qt*ct` only when present. Control's undo, read-back and hand-edit protection stay (§3a).
5. **When Iconic is installed, Control offers the reversible hand-over** in §3, never automatically.
6. **Credit Iconic in a new Credits panel on the About page,** linking to its repository (§7). If any code is ported rather than reimplemented, its MIT notice also goes in `LICENSE`.
