#!/usr/bin/env bash
#
# build-deb.sh — compila el plugin y arma un paquete .deb
#
# Uso:  bash .github/scripts/build-deb.sh <sufijo-de-distro>   (ej.: deb12, deb13, ubuntu24.04)
# Deja el paquete en ./dist/. Lo usa el workflow de GitHub, pero también
# sirve a mano en cualquier Debian/Ubuntu con las dependencias instaladas.

set -euo pipefail

SUFFIX=${1:?falta el sufijo de distro (ej.: deb12)}
PKG=xfce-desktop-button-plugin
MAINTAINER=${MAINTAINER:-"Tantin1 <tantin1@users.noreply.github.com>"}
HOMEPAGE=${HOMEPAGE:-"https://github.com/Tantin1/xfce-desktop-button-plugin"}

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
BUILD="$WORK/build"
STAGE="$WORK/debian/$PKG"     # la ruta que espera dpkg-shlibdeps
DIST="$ROOT/dist"

# versión: la de project(... VERSION x.y.z) en CMakeLists.txt
UPSTREAM=$(sed -n 's/^project([^)]*VERSION \([0-9.]*\).*/\1/p' "$ROOT/src/CMakeLists.txt")
[ -n "$UPSTREAM" ] || { echo "No se encontró la versión en src/CMakeLists.txt" >&2; exit 1; }
VERSION="$UPSTREAM-1~$SUFFIX"
ARCH=$(dpkg --print-architecture)

mkdir -p "$DIST"

# --- compilar e instalar en una carpeta de preparación ---
cmake -S "$ROOT/src" -B "$BUILD" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel
DESTDIR="$STAGE" cmake --install "$BUILD"

# sin símbolos de depuración: paquete más chico
find "$STAGE" -name '*.so' -exec strip --strip-unneeded {} +

# --- dependencias: dpkg-shlibdeps las calcula desde la biblioteca ---
SO=$(find "$STAGE" -name "lib$PKG.so" | head -1)
mkdir -p "$STAGE/DEBIAN"      # así dpkg-shlibdeps reconoce la carpeta del paquete
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$PKG" "$PKG" > "$WORK/debian/control"
SHLIBS=$(cd "$WORK" && dpkg-shlibdeps -O "${SO#"$WORK"/}" | sed -n 's/^shlibs:Depends=//p')

# --- metadatos del paquete ---
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/share/doc/$PKG"
SIZE=$(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)

cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $SIZE
Depends: xfce4-panel, $SHLIBS
Section: xfce
Priority: optional
Homepage: $HOMEPAGE
Description: Slim, customizable Show Desktop button for the Xfce panel
 Replacement for the Xfce "Show Desktop" plugin that can be made as thin
 as one pixel. Draws a configurable line or icon, includes built-in
 profiles inspired by Windows 7/10/11, smooth transitions, peek on hover,
 light/dark panel detection and X11/Wayland support.
EOF

cat > "$STAGE/usr/share/doc/$PKG/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: $PKG
Source: $HOMEPAGE

Files: *
License: GPL-2.0+
 On Debian systems, the full text of the GNU General Public License
 version 2 can be found in /usr/share/common-licenses/GPL-2.
EOF

# permisos estándar (dpkg-deb se queja de carpetas con permisos raros)
find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -type f -exec chmod 0644 {} +
find "$STAGE" -name '*.so' -exec chmod 0644 {} +

OUT="$DIST/${PKG}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$STAGE" "$OUT"

echo
echo "Paquete: $OUT"
dpkg-deb --info "$OUT" | sed -n '/Package:/,/Description:/p'
