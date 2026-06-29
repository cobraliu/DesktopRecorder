#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include "recording/types.h"

class QThread;

namespace rr {

class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    void start(const CaptureRegion& region, const OutputOptions& options);
    void stop();

signals:
    void finished(const QString& path);
    void error(const QString& msg);

private:
    void runLoop(CaptureRegion region, OutputOptions options);

    QThread* thread_ = nullptr;
    std::atomic<bool> stopFlag_{false};
};

}
