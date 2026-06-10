#pragma once

#include <QList>
#include <QObject>

#include <atomic>

#include "fileentry.h"

// Lives in a worker thread. Lists the folder and streams entries to the UI
// in batches so a 10k-file folder fills the view progressively. A scan is
// abandoned as soon as the owner bumps the shared generation counter.
class FolderScanner : public QObject {
    Q_OBJECT
public:
    explicit FolderScanner(const std::atomic<quint64>* currentGeneration,
                           QObject* parent = nullptr)
        : QObject(parent), currentGeneration_(currentGeneration) {}

    void scan(quint64 generation, const QString& dirPath);

signals:
    void batchReady(quint64 generation, const QList<FileEntry>& entries);
    void finished(quint64 generation, qsizetype totalCount);

private:
    const std::atomic<quint64>* currentGeneration_;
};
