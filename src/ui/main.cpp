#include <QApplication>

#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("FileProcessor");
    QApplication::setOrganizationName("andreipanasyuk");
    // Closing the window during a run hides to tray; quitting is explicit.
    QApplication::setQuitOnLastWindowClosed(false);
    MainWindow w;
    w.show();
    return app.exec();
}
