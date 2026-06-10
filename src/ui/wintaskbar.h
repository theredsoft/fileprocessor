#pragma once

#include <QtGlobal>

class QWidget;

// Progress on the Windows taskbar button via ITaskbarList3.
// A no-op everywhere else, so callers need no #ifdef.
class WinTaskbarProgress {
public:
    explicit WinTaskbarProgress(QWidget* window);
    ~WinTaskbarProgress();
    WinTaskbarProgress(const WinTaskbarProgress&) = delete;
    WinTaskbarProgress& operator=(const WinTaskbarProgress&) = delete;

    void setProgress(quint64 done, quint64 total);
    void setError();  // paints the remaining bar red
    void clear();

private:
    QWidget* window_;
    void* taskbar_ = nullptr;  // ITaskbarList3*, opaque to non-Windows code
};
