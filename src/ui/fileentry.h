#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

// Snapshot of one file taken by the scanner thread; everything the preview
// worker needs without touching the filesystem again.
struct FileEntry {
    QString fileName;
    QString stem;    // name without extension
    QString suffix;  // extension without the dot
    QDateTime lastModified;
};
Q_DECLARE_METATYPE(FileEntry)

// One row of the right-hand (preview) pane.
struct PreviewItem {
    QString text;
    bool isError = false;
};
Q_DECLARE_METATYPE(PreviewItem)
