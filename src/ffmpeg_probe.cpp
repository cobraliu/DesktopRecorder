#include "ffmpeg_probe.h"
#include <cstdio>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/version.h>
}

namespace rr {
std::string ffmpegVersion() {
    const unsigned v = avformat_version();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "libavformat %u.%u.%u",
                  AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v), AV_VERSION_MICRO(v));
    return std::string(buf);
}
}
