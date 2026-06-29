#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include "app/RecordingItem.h"

namespace rr {

class RecordingStore : public QObject {
    Q_OBJECT
public:
    explicit RecordingStore(QString jsonPath, QObject* parent = nullptr);

    void load();
    void save() const;
    const QVector<RecordingItem>& items() const { return items_; }

    void add(const RecordingItem& item);
    bool removeAt(int index);
    void setState(const QString& id, RecordingState state);
    void setDuration(const QString& id, int durationMs);

signals:
    void changed();

private:
    int indexOf(const QString& id) const;
    QString path_;
    QVector<RecordingItem> items_;
};

}
