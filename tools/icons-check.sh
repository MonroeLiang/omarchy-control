#!/bin/bash
# Runs the icon pack choice through Omarchy's real scripts, with no session involved.
#
# The real gsettings runs on GLib's keyfile backend, which never touches dconf or a bus,
# and HOME points at a throwaway folder, so the desktop this runs on is not involved.
# dbus-run-session is NOT isolation for this: the bus starts dconf-service with the outer
# environment, and it writes to the real database. The real value is checked before and
# after, and this fails loudly if it moved.
#
# Usage: tools/icons-check.sh [path to omarchy-control]
set -u
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BIN="${1:-$HERE/../build/omarchy-control}"
OMARCHY_BIN="${OMARCHY_BIN:-/usr/bin}"
for tool in gsettings "$OMARCHY_BIN/omarchy-hook" "$OMARCHY_BIN/omarchy-theme-set-gnome"; do
  command -v "$tool" >/dev/null || { echo "needs $tool"; exit 2; }
done
[ -x "$BIN" ] || { echo "build the app first: $BIN"; exit 2; }
BIN="$(realpath "$BIN")"

REAL_BEFORE="$(gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null)"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
FAILED=0
pass() { echo "ok   - $1"; }
fail() { echo "FAIL - $1 (wanted $2, got $3)"; FAILED=1; }
same() { if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "$2" "$3"; fi; }

export HOME="$ROOT/home" XDG_CONFIG_HOME="$ROOT/home/.config" XDG_STATE_HOME="$ROOT/home/.local/state"
export XDG_DATA_HOME="$ROOT/home/.local/share" XDG_DATA_DIRS="$ROOT/share" XDG_CACHE_HOME="$ROOT/home/.cache"
export XDG_RUNTIME_DIR="$ROOT/run" GSETTINGS_BACKEND=keyfile OMARCHY_PATH=/usr/share/omarchy
export GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas
# Omarchy's script only needs this to be set. Nothing here can reach a bus through it.
export DBUS_SESSION_BUS_ADDRESS="unix:path=$ROOT/no-bus" QT_QPA_PLATFORM=offscreen
SHIM="$ROOT/bin"
mkdir -p "$SHIM" "$XDG_RUNTIME_DIR" "$HOME/.config/omarchy/hooks/theme-set.d" "$HOME/.local/state/omarchy/current/theme"
ln -s "$BIN" "$SHIM/omarchy-control"
export PATH="$SHIM:$OMARCHY_BIN:/usr/bin:/bin"
THEME="$HOME/.local/state/omarchy/current/theme"
printf 'mode = "dark"\nbackground = "#101010"\n' >"$THEME/colors.toml"
printf 'first\n' >"$HOME/.local/state/omarchy/current/theme.name"

pack() {
  mkdir -p "$ROOT/share/icons/$1/scalable/apps"
  printf '[Icon Theme]\nName=%s\nDirectories=scalable/apps\n[scalable/apps]\nSize=48\nMinSize=8\nMaxSize=512\nType=Scalable\n' "$1" >"$ROOT/share/icons/$1/index.theme"
  printf '<svg xmlns="http://www.w3.org/2000/svg"/>' >"$ROOT/share/icons/$1/scalable/apps/folder.svg"
}
pack Yaru-blue; pack ThemeOne; pack ThemeTwo; pack Chosen
now() { gsettings get org.gnome.desktop.interface icon-theme | tr -d "'"; }
# What a theme change does, in order, using Omarchy's own two scripts.
#
# Like omarchy-theme-set: the swap is serialized by one lock, then each batch takes another
# lock for its retints and skips them if a newer switch has replaced its theme, and the
# hook runs with that lock released. That ordering is what the hook relies on.
GENERATION="$HOME/.local/state/omarchy/current/theme.generation"
NAME="$HOME/.local/state/omarchy/current/theme.name"
switch_theme() {
  local mine="$BASHPID-$RANDOM"
  (
    flock 9
    printf '%s\n' "$1" >"$THEME/icons.theme"
    printf '%s\n' "$mine" >"$GENERATION"
    printf '%s\n' "$1" >"$NAME"
  ) 9>"$XDG_RUNTIME_DIR/omarchy-theme-set.lock"
  (
    flock 8
    if [ "$(cat "$GENERATION")" = "$mine" ]; then
      "$OMARCHY_BIN/omarchy-theme-set-gnome" 2>/dev/null
    fi
  ) 8>"$XDG_RUNTIME_DIR/omarchy-theme-set-retint.lock"
  "$OMARCHY_BIN/omarchy-hook" theme-set "$1" >/dev/null
}

switch_theme ThemeOne
same "with no choice made, Omarchy's value stands" ThemeOne "$(now)"
"$BIN" get appearance.icons >/dev/null
same "reading changes nothing" ThemeOne "$(now)"

TX="$("$BIN" apply appearance.icons Chosen --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')"
same "choosing a pack applies it" Chosen "$(now)"
HOOK="$HOME/.config/omarchy/hooks/theme-set.d/90-omarchy-control-icons"
[ -f "$HOOK" ] && pass "the hook is in place" || fail "the hook is in place" present missing

switch_theme ThemeTwo
same "a fixed choice survives a theme change with no window open" Chosen "$(now)"

for i in 1 2 3 4 5 6; do switch_theme "ThemeOne" & switch_theme "ThemeTwo" & done
wait
same "and survives rapid overlapping switches" Chosen "$(now)"

"$BIN" apply appearance.icons follow-theme >/dev/null
switch_theme ThemeOne
same "following keeps up with the theme" ThemeOne "$(now)"

"$BIN" apply appearance.icons Chosen >/dev/null
"$BIN" reset appearance.icons >/dev/null
same "returning to following uses the current theme's pack" ThemeOne "$(now)"

rm -f "$THEME/icons.theme"
"$OMARCHY_BIN/omarchy-theme-set-gnome" 2>/dev/null
same "a theme with no pack gets Omarchy's fallback" Yaru-blue "$(now)"
same "and the app agrees on what that fallback is" "follow-theme" "$("$BIN" get appearance.icons --json | python3 -c 'import json,sys; print(json.load(sys.stdin)["value"])')"

"$BIN" icons remove-integration >/dev/null
printf 'ThemeTwo\n' >"$THEME/icons.theme"
"$BIN" apply appearance.icons Chosen >/dev/null
TX2="$("$BIN" history --json | python3 -c 'import json,sys; rows=json.load(sys.stdin); rows=rows if isinstance(rows,list) else rows.get("history",rows); print(rows[0]["id"])')"
"$BIN" undo "$TX2" >/dev/null
same "undo goes back to the value before" Yaru-blue "$(now)"
[ ! -f "$HOOK" ] && pass "and removes the hook it added" || fail "and removes the hook it added" absent present

if [ "$(env -u GSETTINGS_BACKEND HOME="${REAL_HOME:-$(getent passwd "$(id -u)" | cut -d: -f6)}" XDG_CONFIG_HOME= XDG_DATA_DIRS=/usr/share DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$(id -u)/bus" /usr/bin/gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null)" = "$REAL_BEFORE" ]; then
  pass "the real session's icon theme did not move ($REAL_BEFORE)"
else
  fail "the real session's icon theme did not move" "$REAL_BEFORE" "changed"
fi
[ "$FAILED" = 0 ] && echo "All good." || echo "Something failed."
exit "$FAILED"
