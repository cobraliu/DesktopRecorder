#!/usr/bin/env bash
set -euo pipefail

# Usage: build-appimage.sh [path to static binary]
# Output: dist/RegionRecord-x86_64.AppImage (self-contained, zero external Qt/libav dependencies)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${1:-build-static/RegionRecord}"
OUT_DIR="dist"
APPDIR="${OUT_DIR}/RegionRecord.AppDir"
ICON_PNG="${SCRIPT_DIR}/../icon/RegionRecord.png"

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

# App icon: the 256px PNG generated from rr::renderAppIcon (packaging/icon/generate.sh).
if [ ! -f "${ICON_PNG}" ]; then
  echo "error: icon not found: ${ICON_PNG} (run packaging/icon/generate.sh)" >&2
  exit 1
fi
cp "${ICON_PNG}" "${APPDIR}/regionrecord.png"

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
