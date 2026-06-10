#pragma once

#include <QObject>

class QAction;
class QSystemTrayIcon;
class QWidget;

// Tray icon with a state-colored glyph: while a run is active the icon shows
// a progress pie and the tooltip carries percent + ETA, so the app stays
// informative when the window is minimized to tray.
class TrayController : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Working, Done, Error };

    explicit TrayController(QWidget* window);

    bool available() const;
    void setState(State state);
    void setProgress(int percent, const QString& etaText);
    void notify(const QString& title, const QString& message, bool success);

signals:
    void showRequested();
    void cancelRequested();
    void quitRequested();

private:
    void refreshIcon();

    QSystemTrayIcon* tray_ = nullptr;
    QAction* cancelAction_ = nullptr;
    State state_ = State::Idle;
    int percent_ = -1;
};
