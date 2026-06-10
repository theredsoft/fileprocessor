#pragma once

#include <QList>
#include <QObject>

#include <atomic>

#include "fileentry.h"

// Lives in a worker thread. Applies the template to a snapshot of the file
// list and returns the whole preview column. Cancellation is cooperative:
// the worker rechecks the shared generation counter every few hundred files.
class PreviewWorker : public QObject {
    Q_OBJECT
public:
    explicit PreviewWorker(const std::atomic<quint64>* currentGeneration,
                           QObject* parent = nullptr)
        : QObject(parent), currentGeneration_(currentGeneration) {}

    void compute(quint64 generation, const QString& templateText,
                 const QString& folderName, const QList<FileEntry>& files);

signals:
    void ready(quint64 generation, const QList<PreviewItem>& items,
               qsizetype renameCount, qsizetype conflictCount);
    void parseFailed(quint64 generation, qsizetype pos, const QString& message);

private:
    const std::atomic<quint64>* currentGeneration_;
};
