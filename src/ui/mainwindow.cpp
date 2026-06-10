#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "core/template/template.h"
#include "folderscanner.h"
#include "models.h"
#include "previewworker.h"
#include "renameworker.h"
#include "traycontroller.h"
#include "wintaskbar.h"

namespace {

constexpr int kHistoryLimit = 10;

const QStringList& presetTemplates() {
    static const QStringList presets = {
        QStringLiteral("{folder}_{number = 0001}.{ext}"),
        QStringLiteral(
            "Photo_{photodate = YYYYMMDD OR filedate = YYYYMMDD}_{number = 001}.{ext}"),
        QStringLiteral("{filedate = YYYY-MM-DD}_{name}.{ext}"),
        QStringLiteral("{name AND lowercase AND nospaces}.{ext}"),
    };
    return presets;
}

QString formatEta(qint64 msRemaining) {
    const qint64 s = msRemaining / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10,
                                       QLatin1Char('0'));
}

QStringList pushHistory(QStringList list, const QString& value) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) return list;
    list.removeAll(trimmed);
    list.prepend(trimmed);
    while (list.size() > kHistoryLimit) list.removeLast();
    return list;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    qRegisterMetaType<QList<FileEntry>>();
    qRegisterMetaType<QList<PreviewItem>>();
    qRegisterMetaType<QList<RenameOp>>();

    setWindowTitle(tr("FileProcessor"));
    resize(900, 600);

    auto* editMenu = menuBar()->addMenu(tr("&Правка"));
    undoAction_ = editMenu->addAction(tr("Отменить последнее переименование"));
    undoAction_->setEnabled(false);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::undoTriggered);
    auto* helpMenu = menuBar()->addMenu(tr("&Справка"));
    QAction* tokensAction = helpMenu->addAction(tr("Токены шаблона…"));
    connect(tokensAction, &QAction::triggered, this, &MainWindow::showTokenHelp);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* folderRow = new QHBoxLayout();
    folderRow->addWidget(new QLabel(tr("Папка:")));
    folderCombo_ = new QComboBox();
    folderCombo_->setEditable(true);
    folderCombo_->setInsertPolicy(QComboBox::NoInsert);
    folderRow->addWidget(folderCombo_, 1);
    auto* browseButton = new QPushButton(tr("Обзор…"));
    folderRow->addWidget(browseButton);
    layout->addLayout(folderRow);

    auto* templateRow = new QHBoxLayout();
    templateRow->addWidget(new QLabel(tr("Шаблон:")));
    templateCombo_ = new QComboBox();
    templateCombo_->setEditable(true);
    templateCombo_->setInsertPolicy(QComboBox::NoInsert);
    templateRow->addWidget(templateCombo_, 1);
    auto* helpButton = new QPushButton(QStringLiteral("?"));
    helpButton->setFixedWidth(28);
    helpButton->setToolTip(tr("Справка по токенам шаблона"));
    templateRow->addWidget(helpButton);
    layout->addLayout(templateRow);

    errorLabel_ = new QLabel();
    errorLabel_->setStyleSheet(QStringLiteral("color: #c0392b;"));
    errorLabel_->setVisible(false);
    errorLabel_->setWordWrap(true);
    layout->addWidget(errorLabel_);

    auto* splitter = new QSplitter(Qt::Horizontal);
    auto makePane = [](const QString& title, QListView*& view) {
        auto* box = new QGroupBox(title);
        auto* boxLayout = new QVBoxLayout(box);
        view = new QListView();
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setUniformItemSizes(true);  // critical for 10k-row folders
        boxLayout->addWidget(view);
        return box;
    };
    splitter->addWidget(makePane(tr("Текущие имена"), currentView_));
    splitter->addWidget(makePane(tr("После переименования"), previewView_));
    currentModel_ = new FileListModel(this);
    previewModel_ = new PreviewListModel(this);
    currentView_->setModel(currentModel_);
    previewView_->setModel(previewModel_);
    layout->addWidget(splitter, 1);

    auto* actionRow = new QHBoxLayout();
    applyButton_ = new QPushButton(tr("Применить"));
    applyButton_->setEnabled(false);
    actionRow->addWidget(applyButton_);
    cancelButton_ = new QPushButton(tr("Отмена"));
    cancelButton_->setVisible(false);
    actionRow->addWidget(cancelButton_);
    progressBar_ = new QProgressBar();
    progressBar_->setVisible(false);
    actionRow->addWidget(progressBar_, 1);
    etaLabel_ = new QLabel();
    etaLabel_->setVisible(false);
    actionRow->addWidget(etaLabel_);
    actionRow->addStretch();
    layout->addLayout(actionRow);

    setCentralWidget(central);
    statusBar()->showMessage(tr("Выберите рабочую папку"));

    connect(currentView_->verticalScrollBar(), &QScrollBar::valueChanged,
            previewView_->verticalScrollBar(), &QScrollBar::setValue);
    connect(previewView_->verticalScrollBar(), &QScrollBar::valueChanged,
            currentView_->verticalScrollBar(), &QScrollBar::setValue);

    scanner_ = new FolderScanner(&scanGeneration_);
    scanner_->moveToThread(&scanThread_);
    connect(&scanThread_, &QThread::finished, scanner_, &QObject::deleteLater);
    connect(scanner_, &FolderScanner::batchReady, this, &MainWindow::onScanBatch);
    connect(scanner_, &FolderScanner::finished, this, &MainWindow::onScanFinished);
    scanThread_.start();

    previewWorker_ = new PreviewWorker(&previewGeneration_);
    previewWorker_->moveToThread(&previewThread_);
    connect(&previewThread_, &QThread::finished, previewWorker_,
            &QObject::deleteLater);
    connect(previewWorker_, &PreviewWorker::ready, this,
            &MainWindow::onPreviewReady);
    connect(previewWorker_, &PreviewWorker::parseFailed, this,
            &MainWindow::onParseFailed);
    previewThread_.start();

    renameWorker_ = new RenameWorker(&cancelRequested_);
    renameWorker_->moveToThread(&renameThread_);
    connect(&renameThread_, &QThread::finished, renameWorker_,
            &QObject::deleteLater);
    connect(renameWorker_, &RenameWorker::progress, this,
            &MainWindow::onRenameProgress);
    connect(renameWorker_, &RenameWorker::finished, this,
            &MainWindow::onRenameFinished);
    renameThread_.start();

    scanDebounce_ = new QTimer(this);
    scanDebounce_->setSingleShot(true);
    scanDebounce_->setInterval(300);
    connect(scanDebounce_, &QTimer::timeout, this, &MainWindow::startScan);
    connect(folderCombo_, &QComboBox::editTextChanged, scanDebounce_,
            qOverload<>(&QTimer::start));

    previewDebounce_ = new QTimer(this);
    previewDebounce_->setSingleShot(true);
    previewDebounce_->setInterval(300);
    connect(previewDebounce_, &QTimer::timeout, this, &MainWindow::startPreview);
    connect(templateCombo_, &QComboBox::editTextChanged, previewDebounce_,
            qOverload<>(&QTimer::start));

    connect(browseButton, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(helpButton, &QPushButton::clicked, this, &MainWindow::showTokenHelp);
    connect(applyButton_, &QPushButton::clicked, this, &MainWindow::applyClicked);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::cancelClicked);

    tray_ = new TrayController(this);
    connect(tray_, &TrayController::showRequested, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    connect(tray_, &TrayController::cancelRequested, this,
            &MainWindow::cancelClicked);
    connect(tray_, &TrayController::quitRequested, this, [this] {
        cancelRequested_.store(true);
        QApplication::quit();
    });
    taskbar_ = std::make_unique<WinTaskbarProgress>(this);

    loadSettings();
}

MainWindow::~MainWindow() {
    saveSettings();
    // Invalidate generations and raise the cancel flag so in-flight work
    // bails out quickly, then drain the threads.
    scanGeneration_.fetch_add(1);
    previewGeneration_.fetch_add(1);
    cancelRequested_.store(true);
    scanThread_.quit();
    previewThread_.quit();
    renameThread_.quit();
    scanThread_.wait();
    previewThread_.wait();
    renameThread_.wait();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (running_ && tray_->available()) {
        // The run keeps going in the background; the tray shows its progress.
        hide();
        tray_->notify(tr("FileProcessor"),
                      tr("Переименование продолжается в фоне"), true);
        event->ignore();
        return;
    }
    event->accept();
    QApplication::quit();
}

void MainWindow::browseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Выбор рабочей папки"), folderCombo_->currentText());
    if (!dir.isEmpty()) folderCombo_->setCurrentText(dir);
}

void MainWindow::startScan() {
    if (running_) return;
    const quint64 generation = scanGeneration_.fetch_add(1) + 1;
    previewGeneration_.fetch_add(1);  // a new folder voids any running preview
    files_.clear();
    pendingOps_.clear();
    applyButton_->setEnabled(false);
    currentModel_->clear();
    previewModel_->clear();
    errorLabel_->setVisible(false);

    const QString dirPath = folderCombo_->currentText().trimmed();
    if (dirPath.isEmpty() || !QDir(dirPath).exists()) {
        statusBar()->showMessage(tr("Папка не выбрана"));
        return;
    }
    statusBar()->showMessage(tr("Сканирование…"));
    QMetaObject::invokeMethod(
        scanner_,
        [scanner = scanner_, generation, dirPath] {
            scanner->scan(generation, dirPath);
        },
        Qt::QueuedConnection);
}

void MainWindow::startPreview() {
    if (running_) return;
    const quint64 generation = previewGeneration_.fetch_add(1) + 1;
    previewModel_->clear();
    pendingOps_.clear();
    applyButton_->setEnabled(false);
    errorLabel_->setVisible(false);

    // Parse on the UI thread first: instant error feedback while typing.
    try {
        fp::tmpl::Template::parse(templateCombo_->currentText().toStdString());
    } catch (const fp::tmpl::ParseError& e) {
        showTemplateError(static_cast<qsizetype>(e.pos),
                          QString::fromUtf8(e.what()));
        return;
    }
    if (files_.isEmpty()) return;

    statusBar()->showMessage(tr("Вычисление имён…"));
    const QString templateText = templateCombo_->currentText();
    const QString folderPath = folderCombo_->currentText().trimmed();
    const QList<FileEntry> snapshot = files_;  // implicit sharing, O(1)
    QMetaObject::invokeMethod(
        previewWorker_,
        [worker = previewWorker_, generation, templateText, folderPath,
         snapshot] {
            worker->compute(generation, templateText, folderPath, snapshot);
        },
        Qt::QueuedConnection);
}

void MainWindow::onScanBatch(quint64 generation, const QList<FileEntry>& entries) {
    if (generation != scanGeneration_.load()) return;
    files_ += entries;
    QStringList names;
    names.reserve(entries.size());
    for (const FileEntry& e : entries) names.append(e.fileName);
    currentModel_->appendNames(names);
    statusBar()->showMessage(
        tr("Сканирование… найдено файлов: %1").arg(files_.size()));
}

void MainWindow::onScanFinished(quint64 generation, qsizetype totalCount) {
    if (generation != scanGeneration_.load()) return;
    statusBar()->showMessage(tr("Файлов: %1").arg(totalCount));
    startPreview();
}

void MainWindow::onPreviewReady(quint64 generation,
                                const QList<PreviewItem>& items,
                                qsizetype renameCount, qsizetype conflictCount,
                                const QList<RenameOp>& ops) {
    if (generation != previewGeneration_.load()) return;
    previewModel_->setItems(items);
    pendingOps_ = ops;
    applyButton_->setEnabled(!running_ && conflictCount == 0 && renameCount > 0);
    statusBar()->showMessage(tr("Файлов: %1 · переименований: %2 · конфликтов: %3")
                                 .arg(items.size())
                                 .arg(renameCount)
                                 .arg(conflictCount));
}

void MainWindow::onParseFailed(quint64 generation, qsizetype pos,
                               const QString& message) {
    if (generation != previewGeneration_.load()) return;
    showTemplateError(pos, message);
}

void MainWindow::applyClicked() {
    if (running_ || pendingOps_.isEmpty()) return;
    undoRun_ = false;
    setRunning(true);
    cancelRequested_.store(false);
    const quint64 runId = ++runId_;
    lastJournalPath_ = newJournalPath();
    lastRunFolder_ = folderCombo_->currentText().trimmed();
    progressBar_->setRange(0, static_cast<int>(pendingOps_.size()));
    progressBar_->setValue(0);
    etaLabel_->setText(tr("оценка…"));
    runTimer_.start();
    statusBar()->showMessage(tr("Переименование…"));

    const QString folder = lastRunFolder_;
    const QList<RenameOp> ops = pendingOps_;
    const QString journal = lastJournalPath_;
    QMetaObject::invokeMethod(
        renameWorker_,
        [worker = renameWorker_, runId, folder, ops, journal] {
            worker->run(runId, folder, ops, journal);
        },
        Qt::QueuedConnection);
}

void MainWindow::cancelClicked() {
    cancelRequested_.store(true);
    cancelButton_->setEnabled(false);
    cancelButton_->setText(tr("Отмена…"));
}

void MainWindow::undoTriggered() {
    if (running_ || lastJournalPath_.isEmpty() || lastRunFolder_.isEmpty())
        return;
    undoRun_ = true;
    setRunning(true);
    cancelRequested_.store(false);
    const quint64 runId = ++runId_;
    progressBar_->setRange(0, 0);  // size becomes known with first progress
    etaLabel_->setText(QString());
    runTimer_.start();
    statusBar()->showMessage(tr("Откат переименования…"));

    const QString folder = lastRunFolder_;
    const QString journal = lastJournalPath_;
    QMetaObject::invokeMethod(
        renameWorker_,
        [worker = renameWorker_, runId, folder, journal] {
            worker->undo(runId, folder, journal);
        },
        Qt::QueuedConnection);
}

void MainWindow::onRenameProgress(quint64 runId, qsizetype processed,
                                  qsizetype total) {
    if (runId != runId_ || !running_) return;
    progressBar_->setRange(0, static_cast<int>(total));
    progressBar_->setValue(static_cast<int>(processed));
    const qint64 elapsed = runTimer_.elapsed();
    if (processed > 0 && elapsed > 500 && processed < total) {
        const double rate = static_cast<double>(processed) / elapsed;  // per ms
        const qint64 remaining =
            static_cast<qint64>(static_cast<double>(total - processed) / rate);
        etaLabel_->setText(tr("осталось ~%1").arg(formatEta(remaining)));
    }
    const int percent =
        total > 0 ? static_cast<int>(processed * 100 / total) : 0;
    tray_->setProgress(percent, etaLabel_->text());
    taskbar_->setProgress(static_cast<quint64>(processed),
                          static_cast<quint64>(total));
}

void MainWindow::onRenameFinished(quint64 runId, qsizetype done,
                                  qsizetype failed, qsizetype skipped,
                                  bool cancelled, const QStringList& errors) {
    if (runId != runId_) return;
    setRunning(false);

    QSettings settings;
    if (undoRun_) {
        // The journal has been replayed; it must not be replayed twice.
        lastJournalPath_.clear();
        settings.remove(QStringLiteral("undo/journal"));
        settings.remove(QStringLiteral("undo/folder"));
        undoAction_->setEnabled(false);
    } else if (done > 0) {
        settings.setValue(QStringLiteral("undo/journal"), lastJournalPath_);
        settings.setValue(QStringLiteral("undo/folder"), lastRunFolder_);
        undoAction_->setEnabled(true);
        rememberSuccessfulRun();
    }

    QString summary = cancelled
                          ? tr("Прервано: выполнено %1, пропущено %2").arg(done).arg(skipped)
                          : tr("Готово: переименовано %1").arg(done);
    if (failed > 0) summary += tr(", ошибок: %1").arg(failed);
    statusBar()->showMessage(summary);
    if (!errors.isEmpty()) {
        errorLabel_->setText(errors.join(QStringLiteral("\n")));
        errorLabel_->setVisible(true);
    }

    const bool success = failed == 0 && !cancelled;
    tray_->setState(failed > 0 ? TrayController::State::Error
                               : TrayController::State::Done);
    tray_->notify(tr("FileProcessor"), summary, success);
    if (failed > 0)
        taskbar_->setError();
    else
        taskbar_->clear();

    startScan();  // show the folder's new state
}

void MainWindow::showTokenHelp() {
    QMessageBox box(this);
    box.setWindowTitle(tr("Токены шаблона"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>Источники</h3>"
        "<pre>"
        "{folder}     имя текущей папки\n"
        "{name}       имя файла без расширения\n"
        "{ext}        расширение файла\n"
        "{number}     порядковый номер\n"
        "{filedate}   дата изменения файла\n"
        "{photodate}  дата съёмки (EXIF)\n"
        "\"текст\"      строковый литерал"
        "</pre>"
        "<h3>Связки</h3>"
        "<pre>"
        "=    формат:   {number = 0001}, {filedate = YYYY-MM-DD}\n"
        "AND  параметр: {number = 0010 AND step = 5},\n"
        "               {name AND uppercase AND first 8}\n"
        "OR   запасной: {photodate OR filedate OR \"nodate\"}"
        "</pre>"
        "<h3>Примеры</h3>"
        "<pre>"
        "{folder}_{number = 0001}.{ext}\n"
        "Photo_{photodate = YYYYMMDD OR filedate = YYYYMMDD}_{number = 001}.{ext}\n"
        "{name AND lowercase AND nospaces}.{ext}"
        "</pre>"
        "<p>Литеральные скобки: <code>{{</code> и <code>}}</code>. "
        "Модификаторы: uppercase, lowercase, nospaces, first N, step = N.</p>"));
    box.exec();
}

void MainWindow::showTemplateError(qsizetype pos, const QString& message) {
    errorLabel_->setText(tr("Ошибка в шаблоне (символ %1): %2")
                             .arg(pos + 1)
                             .arg(message));
    errorLabel_->setVisible(true);
}

void MainWindow::setRunning(bool running) {
    running_ = running;
    if (running) {
        tray_->setState(TrayController::State::Working);
        taskbar_->clear();
    }
    folderCombo_->setEnabled(!running);
    templateCombo_->setEnabled(!running);
    applyButton_->setEnabled(!running && !pendingOps_.isEmpty());
    undoAction_->setEnabled(false);
    cancelButton_->setVisible(running);
    cancelButton_->setEnabled(running);
    cancelButton_->setText(tr("Отмена"));
    progressBar_->setVisible(running);
    etaLabel_->setVisible(running);
}

QString MainWindow::newJournalPath() const {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/journal-%1.log")
                     .arg(QDateTime::currentDateTime().toString(
                         QStringLiteral("yyyyMMdd-hhmmss-zzz")));
}

void MainWindow::loadSettings() {
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
    folderHistory_ =
        settings.value(QStringLiteral("history/folders")).toStringList();
    templateHistory_ =
        settings.value(QStringLiteral("history/templates")).toStringList();

    QString lastTemplate =
        settings.value(QStringLiteral("ui/lastTemplate")).toString();
    if (lastTemplate.isEmpty()) lastTemplate = presetTemplates().first();
    populateTemplateCombo(lastTemplate);
    populateFolderCombo(
        settings.value(QStringLiteral("ui/lastFolder")).toString());

    // Undo survives a restart while its journal file is still around.
    const QString journal =
        settings.value(QStringLiteral("undo/journal")).toString();
    const QString folder =
        settings.value(QStringLiteral("undo/folder")).toString();
    if (!journal.isEmpty() && !folder.isEmpty() && QFile::exists(journal)) {
        lastJournalPath_ = journal;
        lastRunFolder_ = folder;
        undoAction_->setEnabled(true);
    }
}

void MainWindow::saveSettings() const {
    QSettings settings;
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/lastFolder"),
                      folderCombo_->currentText().trimmed());
    settings.setValue(QStringLiteral("ui/lastTemplate"),
                      templateCombo_->currentText());
    settings.setValue(QStringLiteral("history/folders"), folderHistory_);
    settings.setValue(QStringLiteral("history/templates"), templateHistory_);
}

void MainWindow::rememberSuccessfulRun() {
    folderHistory_ = pushHistory(folderHistory_, lastRunFolder_);
    templateHistory_ =
        pushHistory(templateHistory_, templateCombo_->currentText());
    populateTemplateCombo(templateCombo_->currentText());
    populateFolderCombo(folderCombo_->currentText());
    saveSettings();
}

void MainWindow::populateTemplateCombo(const QString& keepText) {
    const QSignalBlocker blocker(templateCombo_);
    templateCombo_->clear();
    templateCombo_->addItems(templateHistory_);
    if (!templateHistory_.isEmpty())
        templateCombo_->insertSeparator(templateCombo_->count());
    for (const QString& preset : presetTemplates())
        if (!templateHistory_.contains(preset)) templateCombo_->addItem(preset);
    templateCombo_->setCurrentText(keepText);
}

void MainWindow::populateFolderCombo(const QString& keepText) {
    const QSignalBlocker blocker(folderCombo_);
    folderCombo_->clear();
    folderCombo_->addItems(folderHistory_);
    folderCombo_->setCurrentText(keepText);
    // The blocker suppressed the change signal; kick the scan explicitly.
    if (!keepText.trimmed().isEmpty()) scanDebounce_->start();
}
