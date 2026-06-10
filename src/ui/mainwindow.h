#pragma once

#include <QList>
#include <QMainWindow>
#include <QThread>

#include <atomic>

#include "fileentry.h"

class QLabel;
class QLineEdit;
class QListView;
class QTimer;

class FileListModel;
class FolderScanner;
class PreviewListModel;
class PreviewWorker;

// Folder picker, template editor and a live two-pane preview. Scanning and
// preview computation run in worker threads; results are matched against
// generation counters so stale work is dropped. The rename planner/executor
// arrive in milestones 4-5 (see docs/plan.md).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void browseFolder();
    void startScan();
    void startPreview();
    void onScanBatch(quint64 generation, const QList<FileEntry>& entries);
    void onScanFinished(quint64 generation, qsizetype totalCount);
    void onPreviewReady(quint64 generation, const QList<PreviewItem>& items);
    void onParseFailed(quint64 generation, qsizetype pos, const QString& message);

private:
    void showTemplateError(qsizetype pos, const QString& message);

    QLineEdit* folderEdit_ = nullptr;
    QLineEdit* templateEdit_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QListView* currentView_ = nullptr;
    QListView* previewView_ = nullptr;
    FileListModel* currentModel_ = nullptr;
    PreviewListModel* previewModel_ = nullptr;
    QTimer* scanDebounce_ = nullptr;
    QTimer* previewDebounce_ = nullptr;

    QThread scanThread_;
    QThread previewThread_;
    FolderScanner* scanner_ = nullptr;
    PreviewWorker* previewWorker_ = nullptr;
    std::atomic<quint64> scanGeneration_{0};
    std::atomic<quint64> previewGeneration_{0};
    QList<FileEntry> files_;
};
