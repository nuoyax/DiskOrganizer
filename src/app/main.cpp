#include <QApplication>
#include <QFont>
#include "ui/MainWindow.h"
#include "ui/FlatStyle.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DiskOrganizer");
    QApplication::setOrganizationName("DiskOrganizer");

    QFont f("Microsoft YaHei UI", 9);
    f.setStyleHint(QFont::SansSerif);
    QApplication::setFont(f);
    app.setStyleSheet(DiskOrganizer::flatStyleSheet());

    DiskOrganizer::MainWindow w;
    w.show();
    return app.exec();
}
