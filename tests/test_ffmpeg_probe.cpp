#include <QtTest>
#include <string>
#include "ffmpeg_probe.h"

class TestFfmpegProbe : public QObject {
    Q_OBJECT
private slots:
    void versionMentionsLibavformat();
};

void TestFfmpegProbe::versionMentionsLibavformat() {
    const std::string v = rr::ffmpegVersion();
    QVERIFY(!v.empty());
    QVERIFY2(v.find("libavformat") != std::string::npos,
             v.c_str());
}

QTEST_APPLESS_MAIN(TestFfmpegProbe)
#include "test_ffmpeg_probe.moc"
