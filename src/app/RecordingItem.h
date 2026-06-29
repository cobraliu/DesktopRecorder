#pragma once
#include <QString>
#include <QJsonObject>
#include <QtGlobal>

namespace rr {

enum class RecordingState {
    Recording, Finalizing, Completed, FinalizationInterrupted, Failed
};

struct RecordingItem {
    QString id;
    QString filePath;
    RecordingState state = RecordingState::Recording;
    qint64 createdAtMs = 0;
    int durationMs = 0;
};

QString stateLabel(RecordingState s);
QJsonObject toJson(const RecordingItem& item);
RecordingItem fromJson(const QJsonObject& o);

}
