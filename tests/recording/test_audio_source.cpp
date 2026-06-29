#include <QtTest>
#include <cstdlib>
#include <vector>
#include "recording/AudioSource.h"

class TestAudioSource : public QObject {
    Q_OBJECT
private slots:
    void capturesSomeSamples();
};

void TestAudioSource::capturesSomeSamples() {
    if (!std::getenv("PULSE_SERVER") && !std::getenv("XDG_RUNTIME_DIR"))
        QSKIP("no pulse runtime; skipping audio capture test");
    rr::AudioSource src;
    if (!src.open())
        QSKIP("cannot open pulse default source in this env");
    QVERIFY(src.sampleRate() > 0);
    QVERIFY(src.channels() > 0);

    std::vector<int16_t> buf;
    int n = 0;
    QVERIFY(src.readSamples(buf, n));
    QVERIFY(n > 0);
    QVERIFY(static_cast<int>(buf.size()) >= n * src.channels());
    src.close();
}

QTEST_MAIN(TestAudioSource)
#include "test_audio_source.moc"
