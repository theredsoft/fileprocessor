#include "traycontroller.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QWidget>

namespace {

QColor stateColor(TrayController::State state) {
    switch (state) {
    case TrayController::State::Working: return {0x2e, 0x86, 0xc1};  // blue
    case TrayController::State::Done: return {0x27, 0xae, 0x60};     // green
    case TrayController::State::Error: return {0xc0, 0x39, 0x2b};    // red
    case TrayController::State::Idle: break;
    }
    return {0x7f, 0x8c, 0x8d};  // gray
}

QIcon drawIcon(TrayController::State state, int percent) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(stateColor(state));
    p.drawEllipse(1, 1, 30, 30);
    if (state == TrayController::State::Working && percent >= 0) {
        // progress pie on top of the disc
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawPie(5, 5, 22, 22, 90 * 16,
                  -static_cast<int>(percent * 3.6) * 16);
    } else {
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(Qt::NoBrush);
        p.drawText(QRect(0, 0, 32, 32), Qt::AlignCenter, QStringLiteral("FP"));
    }
    return QIcon(pm);
}

}  // namespace

TrayController::TrayController(QWidget* window) : QObject(window) {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_ = new QSystemTrayIcon(this);

    auto* menu = new QMenu(window);
    QAction* showAction = menu->addAction(tr("Показать окно"));
    cancelAction_ = menu->addAction(tr("Отменить переименование"));
    cancelAction_->setEnabled(false);
    menu->addSeparator();
    QAction* quitAction = menu->addAction(tr("Выход"));
    tray_->setContextMenu(menu);

    connect(showAction, &QAction::triggered, this, &TrayController::showRequested);
    connect(cancelAction_, &QAction::triggered, this,
            &TrayController::cancelRequested);
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick)
                    emit showRequested();
            });
    connect(tray_, &QSystemTrayIcon::messageClicked, this,
            &TrayController::showRequested);

    refreshIcon();
    tray_->show();
}

bool TrayController::available() const { return tray_ != nullptr; }

void TrayController::setState(State state) {
    if (tray_ == nullptr) return;
    state_ = state;
    if (state != State::Working) percent_ = -1;
    cancelAction_->setEnabled(state == State::Working);
    refreshIcon();
}

void TrayController::setProgress(int percent, const QString& etaText) {
    if (tray_ == nullptr) return;
    state_ = State::Working;
    percent_ = percent;
    QString tip = tr("FileProcessor — переименование %1%").arg(percent);
    if (!etaText.isEmpty()) tip += QStringLiteral(" · ") + etaText;
    tray_->setToolTip(tip);
    refreshIcon();
}

void TrayController::notify(const QString& title, const QString& message,
                            bool success) {
    if (tray_ == nullptr) return;
    tray_->showMessage(title, message,
                       success ? QSystemTrayIcon::Information
                               : QSystemTrayIcon::Warning,
                       5000);
}

void TrayController::refreshIcon() {
    tray_->setIcon(drawIcon(state_, percent_));
    if (state_ != State::Working)
        tray_->setToolTip(QStringLiteral("FileProcessor"));
}
