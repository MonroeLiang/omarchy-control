#!/bin/bash
# Runs install.sh against real release files in a local mirror, installing the real
# package into a disposable pacman root. This machine's packages are never touched.
#   tools/installer-check.sh <directory with the package and SHA256SUMS>
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
DIST="$(cd "${1:?Usage: tools/installer-check.sh <dist directory>}" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $*"; exit 1; }
pass() { echo "PASS: $*"; }

PACKAGE="$(basename "$(ls "$DIST"/omarchy-control-[0-9]*.pkg.tar.zst)")"
VERSION="$(sed -nE 's/^omarchy-control-([0-9.]+)-.*/\1/p' <<<"$PACKAGE")"

mirror() {
  rm -rf "$WORK/mirror"
  mkdir -p "$WORK/mirror/latest/download" "$WORK/mirror/download/v$VERSION"
  cp "$DIST/$PACKAGE" "$DIST/SHA256SUMS" "$WORK/mirror/latest/download/"
  cp "$DIST/$PACKAGE" "$DIST/SHA256SUMS" "$WORK/mirror/download/v$VERSION/"
}

# The sudo the installer finds: real pacman, under fakeroot, in a throwaway root.
fresh_root() {
  rm -rf "$WORK/root"
  mkdir -p "$WORK/root/var/lib/pacman" "$WORK/cache" "$WORK/bin"
  cat >"$WORK/pacman.conf" <<CONF
[options]
Architecture = auto
DisableSandbox
SigLevel = Never
LocalFileSigLevel = Never
CONF
  cat >"$WORK/bin/sudo" <<SUDO
#!/bin/bash
echo "sudo \$*" >>"$WORK/sudo.log"
[ "\$1" = pacman ] || exit 99
shift
exec fakeroot pacman --config "$WORK/pacman.conf" --root "$WORK/root" --dbpath "$WORK/root/var/lib/pacman" --cachedir "$WORK/cache" --noprogressbar -dd "\$@"
SUDO
  chmod +x "$WORK/bin/sudo"
  : >"$WORK/sudo.log"
}

run_installer() {
  # A new session has no terminal, the same as an agent or a script.
  PATH="$WORK/bin:$PATH" OMARCHY_CONTROL_RELEASES="file://$WORK/mirror" setsid bash -c 'cat "$0" | bash -s -- "$@"' "$ROOT/install.sh" "$@" </dev/null
}

mirror
fresh_root
run_installer --yes >"$WORK/log" 2>&1 || fail "install failed: $(tail -5 "$WORK/log")"
[ -x "$WORK/root/usr/bin/omarchy-control" ] || fail "the binary was not installed"
"$WORK/root/usr/bin/omarchy-control" --version | grep -qF "$VERSION" || fail "the installed binary is not $VERSION"
pass "installs the release package and verifies its checksum"
grep -q 'Checksum verified' "$WORK/log" || fail "no checksum step in: $(cat "$WORK/log")"
if command -v qmake6 >/dev/null && grep -Eiq 'qmake|make\b|g\+\+' "$WORK/sudo.log"; then fail "the installer tried to build"; fi
pass "builds nothing"

run_installer --yes >"$WORK/log" 2>&1 || fail "a second run failed: $(tail -5 "$WORK/log")"
pass "running it again is safe"

fresh_root
run_installer --yes --version "$VERSION" >"$WORK/log" 2>&1 || fail "pinned install failed: $(tail -5 "$WORK/log")"
[ -x "$WORK/root/usr/bin/omarchy-control" ] || fail "the pinned version was not installed"
pass "installs a pinned version"

fresh_root
printf 'not the package' >"$WORK/mirror/latest/download/$PACKAGE"
if run_installer --yes >"$WORK/log" 2>&1; then fail "accepted a tampered package"; fi
grep -q 'did not match' "$WORK/log" || fail "wrong message for a tampered package: $(cat "$WORK/log")"
[ ! -s "$WORK/sudo.log" ] || fail "pacman ran on a tampered package"
pass "refuses a tampered package, and pacman never runs"

mirror
fresh_root
rm "$WORK/mirror/latest/download/SHA256SUMS"
if run_installer --yes >"$WORK/log" 2>&1; then fail "installed without checksums"; fi
[ ! -s "$WORK/sudo.log" ] || fail "pacman ran without checksums"
pass "refuses a release without checksums"

mirror
fresh_root
if run_installer >"$WORK/log" 2>&1; then fail "went ahead with nobody to ask"; fi
grep -q 'no terminal' "$WORK/log" || fail "wrong message with no terminal: $(cat "$WORK/log")"
[ ! -s "$WORK/sudo.log" ] || fail "pacman ran with nobody to ask"
pass "without a terminal it stops and explains --yes"

# A download cut off midway must run nothing at all.
head -c "$(($(wc -c <"$ROOT/install.sh") - 15))" "$ROOT/install.sh" >"$WORK/cut.sh"
fresh_root
PATH="$WORK/bin:$PATH" OMARCHY_CONTROL_RELEASES="file://$WORK/mirror" bash "$WORK/cut.sh" --yes >"$WORK/log" 2>&1 || true
[ ! -s "$WORK/sudo.log" ] || fail "a truncated script ran pacman"
pass "a truncated download runs nothing"
