#include "app/RecordingItem.h"

namespace rr {

QString stateLabel(RecordingState s) {
    switch (s) {
        case RecordingState::Recording:               return QStringLiteral("Recording");
        case RecordingState::Finalizing:              return QStringLiteral("Finalizing");
        case RecordingState::Completed:               return QStringLiteral("Completed");
        case RecordingState::FinalizationInterrupted: return QStringLiteral("Finalization interrupted");
        case RecordingState::Failed:                  return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QJsonObject toJson(const RecordingItem& item) {
    QJsonObject o;
    o["id"] = item.id;
    o["filePath"] = item.filePath;
    o["state"] = static_cast<int>(item.state);
    o["createdAtMs"] = item.createdAtMs;
    o["durationMs"] = item.durationMs;
    return o;
}

RecordingItem fromJson(const QJsonObject& o) {
    RecordingItem item;
    item.id = o.value("id").toString();
    item.filePath = o.value("filePath").toString();
    item.state = static_cast<RecordingState>(o.value("state").toInt());
    item.createdAtMs = o.value("createdAtMs").toVariant().toLongLong();
    item.durationMs = o.value("durationMs").toInt();
    return item;
}

}
