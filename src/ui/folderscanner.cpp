#include "folderscanner.h"

#include <QDir>
#include <QFileInfo>

namespace {
constexpr qsizetype kBatchSize = 500;
}

void FolderScanner::scan(quint64 generation, const QString& dirPath) {
    if (currentGeneration_->load() != generation) return;

    QDir dir(dirPath);
    const QFileInfoList infos =
        dir.entryInfoList(QDir::Files, QDir::Name | QDir::LocaleAware);

    QList<FileEntry> batch;
    batch.reserve(kBatchSize);
    for (const QFileInfo& fi : infos) {
        batch.append({fi.fileName(), fi.completeBaseName(), fi.suffix(),
                      fi.lastModified()});
        if (batch.size() >= kBatchSize) {
            if (currentGeneration_->load() != generation) return;
            emit batchReady(generation, batch);
            batch.clear();
        }
    }
    if (!batch.isEmpty()) {
        if (currentGeneration_->load() != generation) return;
        emit batchReady(generation, batch);
    }
    emit finished(generation, infos.size());
}
