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

// One rename operation, as planned by the preview pass.
struct RenameOp {
    QString from;
    QString to;
};
Q_DECLARE_METATYPE(RenameOp)

// One row of the right-hand (preview) pane.
struct PreviewItem {
    enum class Kind { Rename, Unchanged, Conflict };
    QString text;
    Kind kind = Kind::Rename;
};
Q_DECLARE_METATYPE(PreviewItem)
