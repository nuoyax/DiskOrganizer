#include <QApplication>
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DiskOrganizer");
    QApplication::setOrganizationName("DiskOrganizer");
    DiskOrganizer::MainWindow w;
    w.show();
    return app.exec();
}
