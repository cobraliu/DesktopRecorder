#include "app/RecordingController.h"
#include "app/RecordingStore.h"
#include <QDateTime>
#include <QUuid>

namespace rr {

RecordingController::RecordingController(RecordingStore* store, QObject* parent)
    : QObject(parent), store_(store) {}

RecordingController::~RecordingController() = default;

QString RecordingController::startRecording(const CaptureRegion& region,
                                            const OutputOptions& options) {
    if (recorder_) return QString();

    currentId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    startedAtMs_ = QDateTime::currentMSecsSinceEpoch();

    RecordingItem item;
    item.id = currentId_;
    item.filePath = QString::fromStdString(options.path);
    item.state = RecordingState::Recording;
    item.createdAtMs = startedAtMs_;
    store_->add(item);

    recorder_ = std::make_unique<Recorder>();
    const QString id = currentId_;

    connect(recorder_.get(), &Recorder::finished, this,
            [this, id](const QString& path) {
        const int dur = int(QDateTime::currentMSecsSinceEpoch() - startedAtMs_);
        store_->setDuration(id, dur);
        store_->setState(id, RecordingState::Completed);
        recorder_.reset();
        emit recordingCompleted(id, path);
    });
    connect(recorder_.get(), &Recorder::error, this,
            [this, id](const QString& msg) {
        store_->setState(id, RecordingState::Failed);
        recorder_.reset();
        emit recordingFailed(id, msg);
    });

    recorder_->start(region, options);
    return currentId_;
}

void RecordingController::stopRecording() {
    if (!recorder_) return;
    store_->setState(currentId_, RecordingState::Finalizing);
    recorder_->stop();
}

}
