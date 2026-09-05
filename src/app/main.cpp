#include <QApplication>
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DiskOrganizer");
    QApplication::setOrganizationName("DiskOrganizer");
    app.setWindowIconText(u8"磁盘整理助手");
    DiskOrganizer::MainWindow w;
    w.show();
    return app.exec();
}
