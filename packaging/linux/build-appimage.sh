#!/usr/bin/env bash
set -euo pipefail

# Usage: build-appimage.sh [path to static binary]
# Output: dist/RegionRecord-x86_64.AppImage (self-contained, zero external Qt/libav dependencies)

BIN="${1:-build-static/RegionRecord}"
OUT_DIR="dist"
APPDIR="${OUT_DIR}/RegionRecord.AppDir"

if [ ! -x "${BIN}" ]; then
  echo "error: static binary not found: ${BIN}" >&2
  exit 1
fi

rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
cp "${BIN}" "${APPDIR}/usr/bin/RegionRecord"

cat > "${APPDIR}/RegionRecord.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=RegionRecord
Exec=RegionRecord
Icon=regionrecord
Categories=AudioVideo;
Terminal=false
EOF

# Placeholder icon: a valid 64x64 solid-color PNG (linuxdeploy validates that the icon
# is a real image; a 0-byte or corrupt file fails). The final icon is planned to replace this later.
base64 -d > "${APPDIR}/regionrecord.png" <<'EOF'
iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAZUlEQVR42u3QQREAAAQAMIVEVERZcjh7rMAiq+ezECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAu5byNsyHWWWRfkAAAAASUVORK5CYII=
EOF

# Download linuxdeploy (only needed once)
TOOL="${OUT_DIR}/linuxdeploy-x86_64.AppImage"
if [ ! -f "${TOOL}" ]; then
  curl -fsSL -o "${TOOL}" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  chmod +x "${TOOL}"
fi

# APPIMAGE_EXTRACT_AND_RUN=1 lets the tool run in environments without FUSE (CI/containers)
export APPIMAGE_EXTRACT_AND_RUN=1
"./${TOOL}" \
  --appdir "${APPDIR}" \
  --desktop-file "${APPDIR}/RegionRecord.desktop" \
  --icon-file "${APPDIR}/regionrecord.png" \
  --output appimage

# linuxdeploy outputs the artifact to the current directory; move it to dist/
mv RegionRecord*.AppImage "${OUT_DIR}/" 2>/dev/null || true
ls -1 "${OUT_DIR}"/RegionRecord*.AppImage
