#!/bin/bash
# Installs, upgrades, and removes the real package in a disposable pacman root, so the
# package's own behavior is checked without touching this machine's package database.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
WORK="$(mktemp -d)"
UPGRADE="$ROOT/.package-check"
trap 'rm -rf "$WORK" "$UPGRADE"' EXIT

mkdir -p "$WORK/root/var/lib/pacman" "$WORK/cache" "$WORK/gnupg" "$WORK/none" "$UPGRADE"
cat >"$WORK/pacman.conf" <<CONF
[options]
Architecture = auto
DisableSandbox
SigLevel = Never
LocalFileSigLevel = Never
CONF
pac() { fakeroot pacman --config "$WORK/pacman.conf" --root "$WORK/root" --dbpath "$WORK/root/var/lib/pacman" --cachedir "$WORK/cache" --gpgdir "$WORK/gnupg" --logfile "$WORK/pacman.log" --hookdir "$WORK/none" --noconfirm --nodeps --nodeps --noscriptlet --arch "$(uname -m)" "$@"; }

fail() { echo "FAIL: $*"; exit 1; }
pass() { echo "PASS: $*"; }

"$ROOT/bin/build" >/dev/null
(cd "$ROOT/pkgbuild" && PKGDEST="$WORK/v1" BUILDDIR="$WORK/b1" makepkg -f --nodeps --noconfirm >/dev/null 2>&1) || true
mkdir -p "$WORK/v1"
(cd "$ROOT/pkgbuild" && PKGDEST="$WORK/v1" makepkg -f --nodeps --noconfirm >"$WORK/make1.log" 2>&1) || fail "package did not build"
cp "$ROOT/pkgbuild/omarchy-control.desktop" "$ROOT/pkgbuild/omarchy-control.svg" "$UPGRADE/"
sed 's/^pkgrel=.*/pkgrel=2/' "$ROOT/pkgbuild/PKGBUILD" >"$UPGRADE/PKGBUILD"
mkdir -p "$WORK/v2"
(cd "$UPGRADE" && PKGDEST="$WORK/v2" makepkg -f --nodeps --noconfirm >"$WORK/make2.log" 2>&1) || fail "upgrade package did not build"
V1="$(ls "$WORK"/v1/omarchy-control-0*.pkg.tar.*)"
V2="$(ls "$WORK"/v2/omarchy-control-0*.pkg.tar.*)"

desktop-file-validate "$ROOT/pkgbuild/omarchy-control.desktop" && pass "desktop entry is valid"
[ -z "$(bsdtar -tf "$V1" | grep -E '^\.INSTALL$' || true)" ] && pass "no install scriptlet, so installing changes no user config"
bsdtar -tf "$V1" | grep -vE '^\.|/$' | grep -qvE '^usr/' && fail "package ships files outside /usr" || pass "ships only under /usr"

# A user's own state, which removal must leave alone.
HOME_DIR="$WORK/root/home/someone/.config/omarchy-control"
mkdir -p "$HOME_DIR"
echo '{"pins":["appearance.theme"]}' >"$HOME_DIR/preferences.json"

pac -U "$V1" >"$WORK/install.log" 2>&1 || fail "install failed: $(tail -4 "$WORK/install.log")"
for f in usr/bin/omarchy-control usr/share/applications/omarchy-control.desktop usr/share/icons/hicolor/scalable/apps/omarchy-control.svg usr/share/licenses/omarchy-control/LICENSE; do
  [ -e "$WORK/root/$f" ] || fail "missing after install: $f"
done
pass "install places the binary, launcher entry, icon, and license"
"$WORK/root/usr/bin/omarchy-control" --version | grep -q '0.1.0' && pass "installed binary runs"
grep -q 'Exec=omarchy-control open' "$WORK/root/usr/share/applications/omarchy-control.desktop" && grep -q 'Keywords=Settings;Control Panel;Preferences' "$WORK/root/usr/share/applications/omarchy-control.desktop" && pass "launcher opens the window and finds Settings and Control Panel"
need="$(readelf -d "$WORK/root/usr/bin/omarchy-control" | sed -n 's/.*NEEDED.*\[\(libQt6[A-Za-z]*\).*/\1/p' | sort -u | tr '\n' ' ')"
echo "      links: $need"

pac -U "$V2" >/dev/null 2>&1 || fail "upgrade failed"
fakeroot pacman --config "$WORK/pacman.conf" --root "$WORK/root" --dbpath "$WORK/root/var/lib/pacman" -Q omarchy-control | grep -q '0.1.0-2' || fail "upgrade did not take"
[ -x "$WORK/root/usr/bin/omarchy-control" ] && pass "upgrade replaces 0.1.0-1 with 0.1.0-2"
[ -f "$HOME_DIR/preferences.json" ] && pass "upgrade keeps user preferences"

pac -R omarchy-control >/dev/null 2>&1 || fail "removal failed"
[ ! -e "$WORK/root/usr/bin/omarchy-control" ] && [ ! -e "$WORK/root/usr/share/applications/omarchy-control.desktop" ] && pass "removal takes every packaged file"
[ -f "$HOME_DIR/preferences.json" ] && pass "removal keeps user preferences"
rm -rf "$ROOT"/pkgbuild/pkg "$ROOT"/pkgbuild/src "$ROOT"/pkgbuild/*.pkg.tar.*
