#include "mainwindow.h"

#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "core/template/template.h"
#include "folderscanner.h"
#include "models.h"
#include "previewworker.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    qRegisterMetaType<QList<FileEntry>>();
    qRegisterMetaType<QList<PreviewItem>>();

    setWindowTitle(tr("FileProcessor"));
    resize(900, 600);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* folderRow = new QHBoxLayout();
    folderRow->addWidget(new QLabel(tr("Папка:")));
    folderEdit_ = new QLineEdit();
    folderRow->addWidget(folderEdit_, 1);
    auto* browseButton = new QPushButton(tr("Обзор…"));
    folderRow->addWidget(browseButton);
    layout->addLayout(folderRow);

    auto* templateRow = new QHBoxLayout();
    templateRow->addWidget(new QLabel(tr("Шаблон:")));
    templateEdit_ = new QLineEdit(QStringLiteral("{folder}_{number = 0001}.{ext}"));
    templateRow->addWidget(templateEdit_, 1);
    layout->addLayout(templateRow);

    errorLabel_ = new QLabel();
    errorLabel_->setStyleSheet(QStringLiteral("color: #c0392b;"));
    errorLabel_->setVisible(false);
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

    scanDebounce_ = new QTimer(this);
    scanDebounce_->setSingleShot(true);
    scanDebounce_->setInterval(300);
    connect(scanDebounce_, &QTimer::timeout, this, &MainWindow::startScan);
    connect(folderEdit_, &QLineEdit::textChanged, scanDebounce_,
            qOverload<>(&QTimer::start));

    previewDebounce_ = new QTimer(this);
    previewDebounce_->setSingleShot(true);
    previewDebounce_->setInterval(300);
    connect(previewDebounce_, &QTimer::timeout, this, &MainWindow::startPreview);
    connect(templateEdit_, &QLineEdit::textChanged, previewDebounce_,
            qOverload<>(&QTimer::start));

    connect(browseButton, &QPushButton::clicked, this, &MainWindow::browseFolder);
}

MainWindow::~MainWindow() {
    // Invalidate the generations so in-flight work bails out quickly.
    scanGeneration_.fetch_add(1);
    previewGeneration_.fetch_add(1);
    scanThread_.quit();
    previewThread_.quit();
    scanThread_.wait();
    previewThread_.wait();
}

void MainWindow::browseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Выбор рабочей папки"), folderEdit_->text());
    if (!dir.isEmpty()) folderEdit_->setText(dir);
}

void MainWindow::startScan() {
    const quint64 generation = scanGeneration_.fetch_add(1) + 1;
    previewGeneration_.fetch_add(1);  // a new folder voids any running preview
    files_.clear();
    currentModel_->clear();
    previewModel_->clear();
    errorLabel_->setVisible(false);

    const QString dirPath = folderEdit_->text().trimmed();
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
    const quint64 generation = previewGeneration_.fetch_add(1) + 1;
    previewModel_->clear();
    errorLabel_->setVisible(false);

    // Parse on the UI thread first: instant error feedback while typing.
    try {
        fp::tmpl::Template::parse(templateEdit_->text().toStdString());
    } catch (const fp::tmpl::ParseError& e) {
        showTemplateError(static_cast<qsizetype>(e.pos),
                          QString::fromUtf8(e.what()));
        return;
    }
    if (files_.isEmpty()) return;

    const QString templateText = templateEdit_->text();
    const QString folderName = QDir(folderEdit_->text().trimmed()).dirName();
    const QList<FileEntry> snapshot = files_;  // implicit sharing, O(1)
    QMetaObject::invokeMethod(
        previewWorker_,
        [worker = previewWorker_, generation, templateText, folderName,
         snapshot] {
            worker->compute(generation, templateText, folderName, snapshot);
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
                                qsizetype renameCount, qsizetype conflictCount) {
    if (generation != previewGeneration_.load()) return;
    previewModel_->setItems(items);
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

void MainWindow::showTemplateError(qsizetype pos, const QString& message) {
    errorLabel_->setText(tr("Ошибка в шаблоне (символ %1): %2")
                             .arg(pos + 1)
                             .arg(message));
    errorLabel_->setVisible(true);
}
