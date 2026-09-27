#!/bin/sh
set -eu

usage() {
    printf 'Usage: %s\n' "$0" >&2
    exit 2
}

[ "$#" -eq 0 ] || usage

REPOSITORY_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$REPOSITORY_ROOT"

for command_name in uscan dpkg-buildpackage dpkg-parsechangelog lintian debsign; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'Required command not found: %s\n' "$command_name" >&2
        exit 1
    }
done

SOURCE_PACKAGE=$(dpkg-parsechangelog --show-field Source)
PACKAGE_VERSION=$(dpkg-parsechangelog --show-field Version)
UPSTREAM_VERSION=${PACKAGE_VERSION#*:}
UPSTREAM_VERSION=${UPSTREAM_VERSION%-*}
PARENT_DIR=$(dirname -- "$REPOSITORY_ROOT")
ORIG_TARBALL=

find_orig_tarball() {
    for compression in xz gz bz2; do
        candidate="../${SOURCE_PACKAGE}_${UPSTREAM_VERSION}.orig.tar.${compression}"
        if [ -f "$candidate" ]; then
            ORIG_TARBALL=$candidate
            return 0
        fi
    done
    return 1
}

if ! find_orig_tarball; then
    uscan --download-current-version --force-download
fi

if ! find_orig_tarball; then
    printf 'Expected orig tarball not found: ../%s_%s.orig.tar.{xz,gz,bz2}\n' \
        "$SOURCE_PACKAGE" "$UPSTREAM_VERSION" >&2
    exit 1
fi

BUILD_DIR=$(mktemp -d "$PARENT_DIR/.${SOURCE_PACKAGE}-source.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
ORIG_TARBALL_ABS="$(CDPATH= cd -- "$(dirname -- "$ORIG_TARBALL")" && pwd)/$(basename -- "$ORIG_TARBALL")"
ln -s "$ORIG_TARBALL_ABS" "$BUILD_DIR/$(basename -- "$ORIG_TARBALL")"
tar -xf "$ORIG_TARBALL_ABS" -C "$BUILD_DIR"

SOURCE_DIR="$BUILD_DIR/${SOURCE_PACKAGE}-${UPSTREAM_VERSION}"
if [ ! -d "$SOURCE_DIR" ]; then
    printf 'Expected source directory not found in orig tarball: %s\n' "$SOURCE_DIR" >&2
    exit 1
fi

rm -rf "$SOURCE_DIR/debian"
cp -a "$REPOSITORY_ROOT/debian" "$SOURCE_DIR/debian"
rm -f "$SOURCE_DIR/debian/files"

printf 'Building source package %s from matching upstream source\n' "$PACKAGE_VERSION"
(cd "$SOURCE_DIR" && dpkg-buildpackage -S -d -us -uc)

CHANGES_FILE="$BUILD_DIR/${SOURCE_PACKAGE}_${PACKAGE_VERSION}_source.changes"
lintian --profile debian "$CHANGES_FILE"
debsign "$CHANGES_FILE"

for artifact in \
    "$BUILD_DIR/${SOURCE_PACKAGE}_${PACKAGE_VERSION}.dsc" \
    "$BUILD_DIR/${SOURCE_PACKAGE}_${PACKAGE_VERSION}.debian.tar."* \
    "$BUILD_DIR/${SOURCE_PACKAGE}_${PACKAGE_VERSION}_source."*; do
    [ -f "$artifact" ] || continue
    mv -f "$artifact" "$PARENT_DIR/"
done

CHANGES_FILE="../${SOURCE_PACKAGE}_${PACKAGE_VERSION}_source.changes"
printf '\nSource package is signed. Upload manually with:\n  dupload --to mentors %s\n' "$CHANGES_FILE"