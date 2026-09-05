#include "MainWindow.h"
#include <QApplication>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include "CleanPage.h"
#include "DuplicatePage.h"
#include "DefragPage.h"
#include "SpaceAnalyzerPage.h"
#include "BigFilePage.h"
#include "Charts.h"
#include "Icons.h"
#include "util/SizeFormatter.h"
#include "util/FileSystemUtil.h"

namespace DiskOrganizer {

namespace {
// 侧边导航按钮：可勾选、左对齐、带图标
QPushButton* makeNavBtn(const QString& text, const char* iconPath, QWidget* parent) {
    auto* b = new QPushButton(Icons::tinted(iconPath, QColor(0x5A, 0x64, 0x78)), text, parent);
    b->setObjectName("navBtn");
    b->setProperty("class", "navBtn");
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setIconSize(QSize(19, 19));
    return b;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    buildMenus();
    refreshDisks();
    statusBar()->showMessage(tr("就绪"));
    setWindowTitle(tr("DiskOrganizer 磁盘整理助手"));
    resize(1080, 720);
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ===== 左侧导航栏 =====
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(200);
    auto* sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(0, 0, 0, 12);
    sv->setSpacing(2);

    auto* title = new QLabel(tr("DiskOrganizer"), sidebar);
    title->setObjectName("appTitle");
    auto* subtitle = new QLabel(tr("磁盘整理助手 v1.0"), sidebar);
    subtitle->setObjectName("appSubtitle");
    sv->addWidget(title);
    sv->addWidget(subtitle);

    m_stack = new QStackedWidget(this);

    struct Nav { const char* text; const char* icon; };
    // 顺序与下方 addPage 一致
    const Nav navs[] = {
        {"磁盘概览", Icons::P::drive},
        {"垃圾清理", Icons::P::trash},
        {"重复文件", Icons::P::duplicate},
        {"大文件",   Icons::P::bigfile},
        {"空间分析", Icons::P::pie},
        {"碎片整理", Icons::P::disk},
    };

    // ===== 内容区 =====
    auto* content = new QWidget(this);
    content->setObjectName("contentArea");
    auto* cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);

    // 概览页（自建）
    auto* overview = buildOverviewPage();
    m_stack->addWidget(overview);

    m_cleanPage = new CleanPage(this);
    m_duplicatePage = new DuplicatePage(this);
    m_bigFilePage = new BigFilePage(this);
    m_analyzerPage = new SpaceAnalyzerPage(this);
    m_defragPage = new DefragPage(this);
    m_stack->addWidget(m_cleanPage);
    m_stack->addWidget(m_duplicatePage);
    m_stack->addWidget(m_bigFilePage);
    m_stack->addWidget(m_analyzerPage);
    m_stack->addWidget(m_defragPage);

    cv->addWidget(m_stack, 1);

    // 导航按钮 → 页面切换
    auto* navGroup = new QButtonGroup(this);
    connect(navGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_stack->setCurrentIndex(id);
    });
    for (int i = 0; i < 6; ++i) {
        auto* btn = makeNavBtn(QString::fromUtf8(navs[i].text), navs[i].icon, sidebar);
        navGroup->addButton(btn, i);
        sv->addWidget(btn);
        // 图标选中态着色
        connect(btn, &QPushButton::toggled, this, [btn, icon = navs[i].icon]() {
            btn->setIcon(Icons::tinted(icon,
                btn->isChecked() ? QColor(0x2F, 0x6F, 0xED) : QColor(0x5A, 0x64, 0x78)));
        });
    }
    navGroup->button(0)->setChecked(true);
    sv->addStretch();

    root->addWidget(sidebar);
    root->addWidget(content, 1);
    setCentralWidget(central);
}

QWidget* MainWindow::buildOverviewPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(24, 20, 24, 20);
    v->setSpacing(14);

    auto* header = new QLabel(tr("磁盘概览"), page);
    header->setProperty("class", "cardTitle");
    header->setStyleSheet("font-size:20px; font-weight:800; color:#23262F; background:transparent;");
    auto* hint = new QLabel(tr("双击磁盘行进入空间分析"), page);
    hint->setProperty("class", "hint");
    v->addWidget(header);
    v->addWidget(hint);

    // 图表卡片
    auto* chartCard = new QFrame(page);
    chartCard->setProperty("class", "card");
    auto* cv = new QHBoxLayout(chartCard);
    cv->setContentsMargins(16, 12, 16, 12);
    cv->addWidget(m_diskPie = new PieChart, 1);
    cv->addWidget(m_diskBar = new BarChart, 1);
    v->addWidget(chartCard);

    // 磁盘表卡片
    auto* tableCard = new QFrame(page);
    tableCard->setProperty("class", "card");
    auto* tv = new QVBoxLayout(tableCard);
    tv->setContentsMargins(12, 12, 12, 12);
    m_diskTable = new QTableWidget(0, 6, this);
    m_diskTable->setHorizontalHeaderLabels({tr("盘符"), tr("卷标"), tr("文件系统"),
                                            tr("总容量"), tr("可用空间"), tr("使用率")});
    m_diskTable->horizontalHeader()->setStretchLastSection(true);
    m_diskTable->verticalHeader()->setVisible(false);
    m_diskTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_diskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_diskTable->setShowGrid(false);
    connect(m_diskTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::openDiskInAnalyzer);
    tv->addWidget(m_diskTable);
    v->addWidget(tableCard, 1);
    return page;
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
        // 使用率：<70% 绿、70-90% 黄、>90% 红（含圆点色块）
        const double ratio = d.usedRatio();
        const QColor dotColor = ratio > 0.9 ? QColor("#D93026")
                              : ratio > 0.7 ? QColor("#E8A13A")
                                            : QColor("#2E9E5B");
        auto* usageItem = new QTableWidgetItem(
            QString::fromUtf8("\xE2\x97\x8F ") + QString::number(ratio * 100, 'f', 1) + '%');
        usageItem->setForeground(dotColor);
        m_diskTable->setItem(5, usageItem);
    }

    // 图表：各磁盘 已用/可用
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
    // 切到空间分析页（导航按钮索引 4）
    m_stack->setCurrentIndex(4);
    m_analyzerPage->scanPath(drive);
}

} // namespace DiskOrganizer
