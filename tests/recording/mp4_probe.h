#pragma once
#include <string>

namespace rr::test {
struct Mp4Info {
    bool ok = false;
    int width = 0;
    int height = 0;
    int videoPackets = 0;
    std::string codec;
    int audioStreams = 0;
    int audioPackets = 0;
    std::string audioCodec;
};
Mp4Info probeMp4(const std::string& path);
}
