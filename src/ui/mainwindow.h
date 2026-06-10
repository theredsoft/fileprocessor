#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QMainWindow>
#include <QThread>

#include <atomic>

#include "fileentry.h"

class QAction;
class QLabel;
class QLineEdit;
class QListView;
class QProgressBar;
class QPushButton;
class QTimer;

class FileListModel;
class FolderScanner;
class PreviewListModel;
class PreviewWorker;
class RenameWorker;

// Folder picker, template editor, live two-pane preview and the rename run
// itself. All heavy work lives in worker threads; results are matched
// against generation counters so stale work is dropped.
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
    void onPreviewReady(quint64 generation, const QList<PreviewItem>& items,
                        qsizetype renameCount, qsizetype conflictCount,
                        const QList<RenameOp>& ops);
    void onParseFailed(quint64 generation, qsizetype pos, const QString& message);
    void applyClicked();
    void cancelClicked();
    void undoTriggered();
    void onRenameProgress(quint64 runId, qsizetype processed, qsizetype total);
    void onRenameFinished(quint64 runId, qsizetype done, qsizetype failed,
                          qsizetype skipped, bool cancelled,
                          const QStringList& errors);

private:
    void showTemplateError(qsizetype pos, const QString& message);
    void setRunning(bool running);
    QString newJournalPath() const;

    QLineEdit* folderEdit_ = nullptr;
    QLineEdit* templateEdit_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QListView* currentView_ = nullptr;
    QListView* previewView_ = nullptr;
    FileListModel* currentModel_ = nullptr;
    PreviewListModel* previewModel_ = nullptr;
    QTimer* scanDebounce_ = nullptr;
    QTimer* previewDebounce_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* etaLabel_ = nullptr;
    QAction* undoAction_ = nullptr;

    QThread scanThread_;
    QThread previewThread_;
    QThread renameThread_;
    FolderScanner* scanner_ = nullptr;
    PreviewWorker* previewWorker_ = nullptr;
    RenameWorker* renameWorker_ = nullptr;
    std::atomic<quint64> scanGeneration_{0};
    std::atomic<quint64> previewGeneration_{0};
    std::atomic<bool> cancelRequested_{false};
    quint64 runId_ = 0;
    bool running_ = false;
    QList<FileEntry> files_;
    QList<RenameOp> pendingOps_;
    QString lastJournalPath_;
    QElapsedTimer runTimer_;
};
