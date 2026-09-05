#include "MainWindow.h"
#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include "CleanPage.h"
#include "DuplicatePage.h"
#include "DefragPage.h"
#include "SpaceAnalyzerPage.h"
#include "BigFilePage.h"
#include "Charts.h"
#include "util/SizeFormatter.h"
#include "util/FileSystemUtil.h"

namespace DiskOrganizer {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    buildMenus();
    refreshDisks();
    statusBar()->showMessage(tr("就绪"));
    setWindowTitle(tr("DiskOrganizer 磁盘整理助手"));
    resize(1000, 680);
}

void MainWindow::buildUi() {
    m_tabs = new QTabWidget(this);

    // 概览页：磁盘表格
    m_diskTable = new QTableWidget(0, 6, this);
    m_diskTable->setHorizontalHeaderLabels({tr("盘符"), tr("卷标"), tr("文件系统"),
                                            tr("总容量"), tr("可用空间"), tr("使用率")});
    m_diskTable->horizontalHeader()->setStretchLastSection(true);
    m_diskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_diskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(m_diskTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::openDiskInAnalyzer);

    auto* overview = new QWidget(this);
    auto* v = new QVBoxLayout(overview);
    auto* hint = new QLabel(tr("双击磁盘行进入空间分析"), overview);
    hint->setStyleSheet("color:#636E88; background:transparent;");
    v->addWidget(hint);

    auto* charts = new QHBoxLayout;
    charts->addWidget(m_diskPie = new PieChart, 1);
    charts->addWidget(m_diskBar = new BarChart, 1);
    v->addLayout(charts);
    v->addWidget(m_diskTable, 1);
    m_tabs->addTab(overview, tr("磁盘概览"));

    m_cleanPage = new CleanPage(this);
    m_duplicatePage = new DuplicatePage(this);
    m_analyzerPage = new SpaceAnalyzerPage(this);
    m_defragPage = new DefragPage(this);
    m_bigFilePage = new BigFilePage(this);
    m_tabs->addTab(m_cleanPage, tr("垃圾清理"));
    m_tabs->addTab(m_duplicatePage, tr("重复文件"));
    m_tabs->addTab(m_bigFilePage, tr("大文件"));
    m_tabs->addTab(m_analyzerPage, tr("空间分析"));
    m_tabs->addTab(m_defragPage, tr("碎片整理"));

    setCentralWidget(m_tabs);
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("刷新磁盘(&R)"), this, &MainWindow::refreshDisks, QKeySequence::Refresh);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&X)"), qApp, &QApplication::quit, QKeySequence::Quit);

    QMenu* toolMenu = menuBar()->addMenu(tr("工具(&T)"));
    toolMenu->addAction(tr("设置(&S)"), m_cleanPage, &CleanPage::openSettings);

    QMenu* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于(&A)"), this, [this] {
        QMessageBox::about(this, tr("关于"),
            tr("<b>DiskOrganizer v1.0.0</b><br>Qt6 + C++ 静态编译<br><br>"
               "本程序基于 Qt 6（LGPLv3）。<br>源码获取：https://example.com/diskorganizer"));
    });
}

void MainWindow::refreshDisks() {
    const DiskItemList disks = enumerateDisks();
    m_diskTable->setRowCount(disks.size());
    for (int i = 0; i < disks.size(); ++i) {
        const DiskItem& d = disks[i];
        auto setItem = [this, i](int col, const QString& text) {
            m_diskTable->setItem(i, col, new QTableWidgetItem(text));
        };
        setItem(0, d.driveLetter);
        setItem(1, d.volumeLabel);
        setItem(2, d.fileSystem);
        setItem(3, formatSize(d.totalBytes));
        setItem(4, formatSize(d.freeBytes));
        setItem(5, QString::number(d.usedRatio() * 100, 'f', 1) + '%');
    }

    // 图表：各磁盘 已用/可用 空间
    QList<QPair<QString, double>> pie, bar;
    for (const auto& d : disks) {
        if (d.totalBytes <= 0) continue;
        const QString label = d.volumeLabel.isEmpty() ? d.driveLetter : d.driveLetter + " " + d.volumeLabel;
        pie.append({label + tr(" 已用"), double(d.totalBytes - d.freeBytes)});
        pie.append({label + tr(" 可用"), double(d.freeBytes)});
        bar.append({label, double(d.totalBytes - d.freeBytes)});
    }
    m_diskPie->setData(pie);
    m_diskBar->setData(bar);
}

void MainWindow::openDiskInAnalyzer(int row, int column) {
    Q_UNUSED(column)
    const QString drive = m_diskTable->item(row, 0)->text();
    m_tabs->setCurrentWidget(m_analyzerPage);
    m_analyzerPage->scanPath(drive);
}

} // namespace DiskOrganizer
