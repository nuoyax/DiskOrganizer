#include "ui/MainWindow.h"
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>

namespace DiskOrganizer {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    buildMenus();
    loadDisks();
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::buildUi() {
    auto* tabs = new QTabWidget(this);
    // TODO: 添加 CleanPage / DuplicatePage / DefragPage / SettingsDialog 入口
    tabs->addTab(new QLabel(tr("磁盘概览（待实现）")), tr("磁盘概览"));
    setCentralWidget(tabs);
    resize(960, 640);
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("退出(&X)"), this, &QWidget::close);
    QMenu* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于(&A)"), this, &MainWindow::close);
}

void MainWindow::loadDisks() {
    // TODO: QStorageInfo 枚举磁盘填充概览页
}

} // namespace DiskOrganizer
