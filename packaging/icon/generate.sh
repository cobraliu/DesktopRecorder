#!/usr/bin/env bash
# Regenerate the platform icon files from the QPainter source (rr::renderAppIcon).
# Pipeline: build the render_icon tool -> emit PNG masters -> pack into the
# Windows .ico, the macOS .icns, and a Linux .png. Run after changing the icon
# drawing in src/ui/icons.cpp. Requires: a configured CMake build dir, ImageMagick
# (`convert`), and python3.
#
# Usage: packaging/icon/generate.sh [build-dir]   (default build-dir: build-dev)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="${1:-$root/build-dev}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cmake --build "$build" --target render_icon >/dev/null
"$build/render_icon" "$tmp" >/dev/null

# Windows: multi-resolution .ico (16..256; 256 is stored PNG-compressed).
convert "$tmp/icon_16.png" "$tmp/icon_24.png" "$tmp/icon_32.png" "$tmp/icon_48.png" \
        "$tmp/icon_64.png" "$tmp/icon_128.png" "$tmp/icon_256.png" "$here/RegionRecord.ico"

# Linux: a single 256px PNG for the .desktop entry / AppImage .DirIcon.
cp "$tmp/icon_256.png" "$here/RegionRecord.png"

# macOS: a proper multi-resolution .icns (ImageMagick's icns writer is lossy, so
# pack the container by hand: 'icns' header + typed PNG chunks incl. @2x variants).
python3 - "$tmp" "$here/RegionRecord.icns" <<'PY'
import struct, sys
src, out = sys.argv[1], sys.argv[2]
png = lambda n: open(f"{src}/icon_{n}.png", "rb").read()
entries = [
    ("icp4", 16), ("icp5", 32), ("icp6", 64),
    ("ic07", 128), ("ic08", 256), ("ic09", 512), ("ic10", 1024),
    ("ic11", 32),   # 16@2x
    ("ic12", 64),   # 32@2x
    ("ic13", 256),  # 128@2x
    ("ic14", 512),  # 256@2x
]
chunks = b"".join(t.encode("ascii") + struct.pack(">I", len(d := png(sz)) + 8) + d
                   for t, sz in entries)
open(out, "wb").write(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)
PY

echo "Generated: RegionRecord.ico, RegionRecord.icns, RegionRecord.png in $here"
