#pragma once

#include <QList>
#include <QObject>

#include <atomic>

#include "fileentry.h"

// Lives in a worker thread. Drives fp::exec — both the rename run and the
// journal-based undo. Progress comes from the executor's pool threads,
// throttled to ~10 emits/sec so a 10k-file run does not flood the UI queue.
class RenameWorker : public QObject {
    Q_OBJECT
public:
    explicit RenameWorker(const std::atomic<bool>* cancelFlag,
                          QObject* parent = nullptr)
        : QObject(parent), cancelFlag_(cancelFlag) {}

    void run(quint64 runId, const QString& folder, const QList<RenameOp>& ops,
             const QString& journalPath);
    void undo(quint64 runId, const QString& folder, const QString& journalPath);

signals:
    void progress(quint64 runId, qsizetype processed, qsizetype total);
    void finished(quint64 runId, qsizetype done, qsizetype failed,
                  qsizetype skipped, bool cancelled, const QStringList& errors);

private:
    const std::atomic<bool>* cancelFlag_;
};
