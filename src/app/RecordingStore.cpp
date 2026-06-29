#include "app/RecordingStore.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>

namespace rr {

RecordingStore::RecordingStore(QString jsonPath, QObject* parent)
    : QObject(parent), path_(std::move(jsonPath)) {}

int RecordingStore::indexOf(const QString& id) const {
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].id == id) return i;
    return -1;
}

void RecordingStore::load() {
    items_.clear();
    QFile f(path_);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const auto& v : arr) {
            RecordingItem it = fromJson(v.toObject());
            // Entries not finalized properly last time (still recording/finalizing) are treated as "finalization interrupted"
            if (it.state == RecordingState::Recording ||
                it.state == RecordingState::Finalizing)
                it.state = RecordingState::FinalizationInterrupted;
            items_.push_back(it);
        }
    }
    emit changed();
}

void RecordingStore::save() const {
    QJsonArray arr;
    for (const auto& it : items_) arr.append(toJson(it));
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QFile f(path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

void RecordingStore::add(const RecordingItem& item) {
    items_.push_back(item);
    save();
    emit changed();
}

bool RecordingStore::removeAt(int index) {
    if (index < 0 || index >= items_.size()) return false;
    items_.removeAt(index);
    save();
    emit changed();
    return true;
}

void RecordingStore::setState(const QString& id, RecordingState state) {
    const int i = indexOf(id);
    if (i < 0) return;
    items_[i].state = state;
    save();
    emit changed();
}

void RecordingStore::setDuration(const QString& id, int durationMs) {
    const int i = indexOf(id);
    if (i < 0) return;
    items_[i].durationMs = durationMs;
    save();
    emit changed();
}

}
