#!/bin/bash
# Builds what a GitHub release publishes: the Arch package and SHA256SUMS, in dist/.
# The release workflow runs this, and it runs the same way in a local Arch container.
#   tools/release-build.sh [v0.2.0]     With a tag, also checks it matches the version.
# Must not run as root, because makepkg refuses to.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TAG="${1:-}"
cd "$ROOT"

VERSION="$(sed -n 's/^pkgver=//p' pkgbuild/PKGBUILD)"
grep -qx "VERSION = $VERSION" omarchy-control.pro || { echo "omarchy-control.pro does not say $VERSION. Run tools/set-version.sh." >&2; exit 1; }
if [ -n "$TAG" ] && [ "$TAG" != "v$VERSION" ]; then
  echo "Tag $TAG does not match version $VERSION. Run tools/set-version.sh first." >&2
  exit 1
fi

bin/test
tools/package-check.sh

rm -rf dist
mkdir -p dist
(cd pkgbuild && PKGDEST="$ROOT/dist" makepkg -f --nodeps --noconfirm)
rm -rf pkgbuild/pkg pkgbuild/src
(cd dist && sha256sum -- *.pkg.tar.zst >SHA256SUMS)
tools/installer-check.sh "$ROOT/dist"

echo "Ready to publish:"
ls -l dist
