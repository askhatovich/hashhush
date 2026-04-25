#!/usr/bin/env bash
# Build a .deb for the host architecture. Frontend is built and embedded into
# the binary first, so the resulting package is a single self-contained
# executable plus a config + systemd unit.
set -euo pipefail

PKG_NAME="hashhush"
PKG_VERSION="${1:-$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo '0.0.0')}"
PKG_ARCH="$(dpkg --print-architecture)"
PKG_MAINTAINER="Roman Lyubimov"
PKG_DESCRIPTION="Ephemeral end-to-end encrypted group chat server"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
STAGE_DIR="$(mktemp -d)"

trap 'rm -rf "$STAGE_DIR"' EXIT

# --- Configure backend (generates version.{js,h} for the frontend) ---------
#
# CMake's configure step materialises web/src/version.js via configure_file
# from version.js.in. The frontend bundler imports it, so the configure must
# come BEFORE `npm run build`.

echo "==> Configuring $PKG_NAME $PKG_VERSION ($PKG_ARCH) ..."

# APP_VERSION is read by src/CMakeLists.txt as the canonical version source.
APP_VERSION="$PKG_VERSION" cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    "$PROJECT_DIR"

# --- Build frontend ---------------------------------------------------------

if [ -d "$PROJECT_DIR/web" ] && [ -f "$PROJECT_DIR/web/package.json" ]; then
    echo "==> Building frontend ..."
    cd "$PROJECT_DIR/web"
    npm ci --ignore-scripts
    npm run build
    bash embed.sh
    cd "$PROJECT_DIR"
fi

# --- Build backend (with the embedded frontend) -----------------------------

echo "==> Building binary ..."
APP_VERSION="$PKG_VERSION" cmake --build "$BUILD_DIR" -j"$(nproc)"

# --- Stage ------------------------------------------------------------------

echo "==> Staging package tree ..."

install -Dm755 "$BUILD_DIR/src/$PKG_NAME"        "$STAGE_DIR/usr/bin/$PKG_NAME"
install -Dm644 "$SCRIPT_DIR/config.ini.example"  "$STAGE_DIR/etc/$PKG_NAME/config.ini"
install -Dm644 "$SCRIPT_DIR/$PKG_NAME.service"   "$STAGE_DIR/usr/lib/systemd/system/$PKG_NAME.service"

mkdir -p "$STAGE_DIR/var/lib/$PKG_NAME"

# --- DEBIAN control --------------------------------------------------------

mkdir -p "$STAGE_DIR/DEBIAN"

cat > "$STAGE_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $PKG_VERSION
Architecture: $PKG_ARCH
Maintainer: $PKG_MAINTAINER
Description: $PKG_DESCRIPTION
 A self-contained C++ binary with an embedded Svelte single-page frontend.
 Rooms are link-shared and end-to-end encrypted; the server cannot read
 message contents, never receives the encryption key, and keeps no
 persistent transcript.
Priority: optional
Section: net
Depends: libc6, libsqlite3-0, libstdc++6
EOF

# conffiles — dpkg won't overwrite a user-edited config on upgrade.
cat > "$STAGE_DIR/DEBIAN/conffiles" <<EOF
/etc/$PKG_NAME/config.ini
EOF

# postinst — system user + state directory + daemon-reload.
cat > "$STAGE_DIR/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e

if ! getent passwd hashhush >/dev/null 2>&1; then
    adduser --system --group --no-create-home --home /nonexistent hashhush
fi

mkdir -p /var/lib/hashhush
chown hashhush:hashhush /var/lib/hashhush
chmod 750 /var/lib/hashhush

if [ -d /run/systemd/system ]; then
    systemctl daemon-reload
fi
POSTINST
chmod 755 "$STAGE_DIR/DEBIAN/postinst"

# postrm — clean up on purge.
cat > "$STAGE_DIR/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e

if [ "$1" = "purge" ]; then
    if getent passwd hashhush >/dev/null 2>&1; then
        deluser --system hashhush || true
    fi
    rm -rf /etc/hashhush /var/lib/hashhush
fi

if [ -d /run/systemd/system ]; then
    systemctl daemon-reload
fi
POSTRM
chmod 755 "$STAGE_DIR/DEBIAN/postrm"

# --- Build .deb -------------------------------------------------------------

DEB_FILE="$PROJECT_DIR/${PKG_NAME}_${PKG_VERSION}_${PKG_ARCH}.deb"

echo "==> Packaging $DEB_FILE ..."
dpkg-deb --root-owner-group --build "$STAGE_DIR" "$DEB_FILE"

echo "==> Done: $DEB_FILE"
echo "   Install:  sudo dpkg -i $DEB_FILE"
echo "   Enable:   sudo systemctl enable --now $PKG_NAME"
