#include "wintaskbar.h"

#include <QWidget>

#ifdef _WIN32

#define NOMINMAX  // windows.h min/max macros break std::min/std::max

#include <shobjidl.h>
#include <windows.h>

namespace {
ITaskbarList3* taskbarOf(void* p) { return static_cast<ITaskbarList3*>(p); }
HWND hwndOf(QWidget* w) { return reinterpret_cast<HWND>(w->winId()); }
}  // namespace

WinTaskbarProgress::WinTaskbarProgress(QWidget* window) : window_(window) {
    ITaskbarList3* taskbar = nullptr;
    // Qt's GUI thread already initialized COM.
    const HRESULT hr =
        CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&taskbar));
    if (SUCCEEDED(hr) && taskbar != nullptr) {
        if (SUCCEEDED(taskbar->HrInit()))
            taskbar_ = taskbar;
        else
            taskbar->Release();
    }
}

WinTaskbarProgress::~WinTaskbarProgress() {
    if (taskbar_ != nullptr) taskbarOf(taskbar_)->Release();
}

void WinTaskbarProgress::setProgress(quint64 done, quint64 total) {
    if (taskbar_ == nullptr || total == 0) return;
    taskbarOf(taskbar_)->SetProgressState(hwndOf(window_), TBPF_NORMAL);
    taskbarOf(taskbar_)->SetProgressValue(hwndOf(window_), done, total);
}

void WinTaskbarProgress::setError() {
    if (taskbar_ == nullptr) return;
    taskbarOf(taskbar_)->SetProgressState(hwndOf(window_), TBPF_ERROR);
}

void WinTaskbarProgress::clear() {
    if (taskbar_ == nullptr) return;
    taskbarOf(taskbar_)->SetProgressState(hwndOf(window_), TBPF_NOPROGRESS);
}

#else

WinTaskbarProgress::WinTaskbarProgress(QWidget* window) : window_(window) {}
WinTaskbarProgress::~WinTaskbarProgress() = default;
void WinTaskbarProgress::setProgress(quint64, quint64) {}
void WinTaskbarProgress::setError() {}
void WinTaskbarProgress::clear() {}

#endif
