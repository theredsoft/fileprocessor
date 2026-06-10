#include "mainwindow.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>
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
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "core/template/template.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
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
    auto makePane = [](const QString& title, QListView*& view,
                       QStandardItemModel*& model) {
        auto* box = new QGroupBox(title);
        auto* boxLayout = new QVBoxLayout(box);
        view = new QListView();
        model = new QStandardItemModel(view);
        view->setModel(model);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setUniformItemSizes(true);  // critical for 10k-row folders
        boxLayout->addWidget(view);
        return box;
    };
    splitter->addWidget(makePane(tr("Текущие имена"), currentView_, currentModel_));
    splitter->addWidget(
        makePane(tr("После переименования"), previewView_, previewModel_));
    layout->addWidget(splitter, 1);

    setCentralWidget(central);
    statusBar()->showMessage(tr("Выберите рабочую папку"));

    connect(currentView_->verticalScrollBar(), &QScrollBar::valueChanged,
            previewView_->verticalScrollBar(), &QScrollBar::setValue);
    connect(previewView_->verticalScrollBar(), &QScrollBar::valueChanged,
            currentView_->verticalScrollBar(), &QScrollBar::setValue);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(300);
    connect(debounce_, &QTimer::timeout, this, &MainWindow::rebuildPreview);
    connect(templateEdit_, &QLineEdit::textChanged, this, &MainWindow::schedulePreview);
    connect(folderEdit_, &QLineEdit::textChanged, this, &MainWindow::schedulePreview);
    connect(browseButton, &QPushButton::clicked, this, &MainWindow::browseFolder);
}

void MainWindow::browseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Выбор рабочей папки"), folderEdit_->text());
    if (!dir.isEmpty()) folderEdit_->setText(dir);
}

void MainWindow::schedulePreview() { debounce_->start(); }

void MainWindow::rebuildPreview() {
    errorLabel_->setVisible(false);
    currentModel_->clear();
    previewModel_->clear();

    const QString dirPath = folderEdit_->text().trimmed();
    QDir dir(dirPath);
    if (dirPath.isEmpty() || !dir.exists()) {
        statusBar()->showMessage(tr("Папка не выбрана"));
        return;
    }

    std::optional<fp::tmpl::Template> tmpl;
    try {
        tmpl = fp::tmpl::Template::parse(templateEdit_->text().toStdString());
    } catch (const fp::tmpl::ParseError& e) {
        errorLabel_->setText(tr("Ошибка в шаблоне (символ %1): %2")
                                 .arg(e.pos + 1)
                                 .arg(QString::fromUtf8(e.what())));
        errorLabel_->setVisible(true);
    }

    const QFileInfoList files =
        dir.entryInfoList(QDir::Files, QDir::Name | QDir::LocaleAware);
    long index = 0;
    for (const QFileInfo& fi : files) {
        currentModel_->appendRow(new QStandardItem(fi.fileName()));
        if (!tmpl) continue;

        fp::tmpl::FileContext ctx;
        ctx.folderName = dir.dirName().toStdString();
        ctx.stem = fi.completeBaseName().toStdString();
        ctx.ext = fi.suffix().toStdString();
        ctx.index = index++;
        if (const QDateTime mod = fi.lastModified(); mod.isValid()) {
            const QDate d = mod.date();
            const QTime t = mod.time();
            ctx.fileDate = fp::tmpl::DateTime{d.year(),  d.month(),  d.day(),
                                              t.hour(),  t.minute(), t.second()};
        }
        // ctx.photoDate: EXIF reader arrives in milestone 7

        QStandardItem* item = nullptr;
        try {
            item = new QStandardItem(QString::fromStdString(tmpl->apply(ctx)));
        } catch (const fp::tmpl::EvalError& e) {
            item = new QStandardItem(
                tr("⚠ %1").arg(QString::fromUtf8(e.what())));
            item->setForeground(QBrush(QColor(0xc0, 0x39, 0x2b)));
        }
        previewModel_->appendRow(item);
    }
    statusBar()->showMessage(tr("Файлов: %1").arg(files.size()));
}
