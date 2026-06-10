#include <QApplication>

#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("FileProcessor");
    QApplication::setOrganizationName("andreipanasyuk");
    MainWindow w;
    w.show();
    return app.exec();
}
