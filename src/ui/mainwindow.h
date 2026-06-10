#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListView;
class QStandardItemModel;
class QTimer;

// Milestone-1 skeleton: folder picker, template editor and a live two-pane
// preview computed synchronously. Async scanning/preview, the planner and
// the executor arrive in milestones 3-5 (see docs/plan.md).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void browseFolder();
    void schedulePreview();
    void rebuildPreview();

private:
    QLineEdit* folderEdit_ = nullptr;
    QLineEdit* templateEdit_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QListView* currentView_ = nullptr;
    QListView* previewView_ = nullptr;
    QStandardItemModel* currentModel_ = nullptr;
    QStandardItemModel* previewModel_ = nullptr;
    QTimer* debounce_ = nullptr;
};
