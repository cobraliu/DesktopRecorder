#pragma once
#include <QObject>
#include <QString>
#include <memory>
#include "recording/types.h"
#include "recording/Recorder.h"

namespace rr {

class RecordingStore;

class RecordingController : public QObject {
    Q_OBJECT
public:
    explicit RecordingController(RecordingStore* store, QObject* parent = nullptr);
    ~RecordingController() override;

    QString startRecording(const CaptureRegion& region, const OutputOptions& options);
    void stopRecording();
    bool isRecording() const { return recorder_ != nullptr; }

signals:
    void recordingCompleted(const QString& id, const QString& path);
    void recordingFailed(const QString& id, const QString& msg);

private:
    RecordingStore* store_;
    std::unique_ptr<Recorder> recorder_;
    QString currentId_;
    qint64 startedAtMs_ = 0;
};

}
