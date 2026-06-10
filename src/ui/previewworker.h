#pragma once

#include <QHash>
#include <QList>
#include <QObject>

#include <atomic>
#include <optional>

#include "core/template/template.h"
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
                 const QString& folderPath, const QList<FileEntry>& files);

signals:
    void ready(quint64 generation, const QList<PreviewItem>& items,
               qsizetype renameCount, qsizetype conflictCount,
               const QList<RenameOp>& ops);
    void parseFailed(quint64 generation, qsizetype pos, const QString& message);

private:
    std::optional<fp::tmpl::DateTime> photoDateFor(const QString& folderPath,
                                                   const FileEntry& f);

    const std::atomic<quint64>* currentGeneration_;
    // EXIF reads are the expensive part of a preview pass; cache them per
    // file+mtime so retyping the template does not re-read 10k JPEGs.
    QHash<QString, std::optional<fp::tmpl::DateTime>> exifCache_;
    QString cachedFolder_;
};
