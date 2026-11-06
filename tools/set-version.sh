#!/bin/sh
# Sets the version everywhere it is written down. The release workflow refuses a tag
# that disagrees with these.
#   tools/set-version.sh 0.2.0
set -eu

VERSION="${1:-}"
case "$VERSION" in
  [0-9]*.[0-9]*.[0-9]*) ;;
  *) echo "Usage: tools/set-version.sh 0.2.0" >&2; exit 1 ;;
esac
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
sed -i "s/^VERSION = .*/VERSION = $VERSION/" "$ROOT/omarchy-control.pro"
sed -i "s/APP_VERSION=\\\\\\\\\\\\\"[^\\\\]*\\\\\\\\\\\\\"/APP_VERSION=\\\\\\\\\\\\\"$VERSION\\\\\\\\\\\\\"/" "$ROOT/tests/omarchy-control_tests.pro" "$ROOT/tools/screens/screens.pro"
sed -i "s/^pkgver=.*/pkgver=$VERSION/; s/^pkgrel=.*/pkgrel=1/" "$ROOT/pkgbuild/PKGBUILD"
grep -H -e 'VERSION = ' -e 'APP_VERSION' -e '^pkgver=' "$ROOT/omarchy-control.pro" "$ROOT/tests/omarchy-control_tests.pro" "$ROOT/tools/screens/screens.pro" "$ROOT/pkgbuild/PKGBUILD"
