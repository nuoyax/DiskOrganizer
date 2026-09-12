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
#include <QProgressBar>
#include <QStackedWidget>
#include <QStatusBar>
#include "CleanPage.h"
#include "DuplicatePage.h"
#include "DefragPage.h"
#include "SpaceAnalyzerPage.h"
#include "BigFilePage.h"
#include "Charts.h"
#include "Icons.h"
#include "FlatStyle.h"
#include "util/SizeFormatter.h"
#include "util/FileSystemUtil.h"
#include <QEvent>
#include <QTimer>
#include <cmath>
#include <QSizePolicy>

namespace DiskOrganizer {

namespace {
QPushButton* makeNavBtn(const QString& text, const char* iconPath, QWidget* parent) {
    auto* b = new QPushButton(Icons::tinted(iconPath, QColor(0xC5, 0xC2, 0xE8)), text, parent);
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
    resize(1180, 760);
    setMinimumSize(900, 600);
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* sidebar = new QWidget(this);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(208);
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
    const Nav navs[] = {
        {"磁盘概览", Icons::P::drive},
        {"垃圾清理", Icons::P::trash},
        {"重复文件", Icons::P::duplicate},
        {"大文件",   Icons::P::bigfile},
        {"空间分析", Icons::P::pie},
        {"碎片整理", Icons::P::disk},
    };

    auto* content = new QWidget(this);
    content->setObjectName("contentArea");
    auto* cv = new QVBoxLayout(content);
    cv->setContentsMargins(24, 20, 24, 20);

    m_cleanPage = new CleanPage(this);
    m_duplicatePage = new DuplicatePage(this);
    m_bigFilePage = new BigFilePage(this);
    m_analyzerPage = new SpaceAnalyzerPage(this);
    m_defragPage = new DefragPage(this);

    auto* overview = buildOverviewPage();
    m_stack->addWidget(overview);
    m_stack->addWidget(m_cleanPage);
    m_stack->addWidget(m_duplicatePage);
    m_stack->addWidget(m_bigFilePage);
    m_stack->addWidget(m_analyzerPage);
    m_stack->addWidget(m_defragPage);

    cv->addWidget(m_stack, 1);

    auto* navGroup = new QButtonGroup(this);
    connect(navGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_stack->setCurrentIndex(id);
    });
    for (int i = 0; i < 6; ++i) {
        auto* btn = makeNavBtn(QString::fromUtf8(navs[i].text), navs[i].icon, sidebar);
        navGroup->addButton(btn, i);
        sv->addWidget(btn);
        connect(btn, &QPushButton::toggled, this, [btn, icon = navs[i].icon]() {
            btn->setIcon(Icons::tinted(icon,
                btn->isChecked() ? QColor(0x5E, 0xE0, 0xD0) : QColor(0xC5, 0xC2, 0xE8)));
        });
    }
    navGroup->button(0)->setChecked(true);
    sv->addStretch();

    // 侧栏底部：存储池小卡
    auto* poolMini = new QFrame(sidebar);
    poolMini->setObjectName("sidebarPool");
    poolMini->setStyleSheet(
        "#sidebarPool{background:#2E2A63; border-radius:14px; margin:8px 12px;}");
    auto* pv = new QVBoxLayout(poolMini);
    pv->setContentsMargins(12, 10, 12, 10);
    pv->setSpacing(6);
    auto* poolCap = new QLabel(tr("存储池"), poolMini);
    poolCap->setStyleSheet("color:#A5A3C9; font-size:11px; background:transparent;");
    m_sidebarPoolLabel = new QLabel(tr("—"), poolMini);
    m_sidebarPoolLabel->setStyleSheet("color:#FFFFFF; font-size:12px; font-weight:600; background:transparent;");
    m_sidebarPoolBar = new QProgressBar(poolMini);
    m_sidebarPoolBar->setTextVisible(false);
    m_sidebarPoolBar->setFixedHeight(6);
    m_sidebarPoolBar->setRange(0, 100);
    m_sidebarPoolBar->setStyleSheet(
        "QProgressBar{background:#1E1B4B; border:none; border-radius:3px;}"
        "QProgressBar::chunk{background:#14B8A6; border-radius:3px;}");
    pv->addWidget(poolCap);
    pv->addWidget(m_sidebarPoolLabel);
    pv->addWidget(m_sidebarPoolBar);
    sv->addWidget(poolMini);

    auto* settingsBtn = new QPushButton(
        Icons::tinted(QString::fromUtf8(Icons::P::settings), QColor(0xC5, 0xC2, 0xE8)),
        tr("设置"), sidebar);
    settingsBtn->setObjectName("navBtn");
    settingsBtn->setProperty("class", "navBtn");
    settingsBtn->setCursor(Qt::PointingHandCursor);
    connect(settingsBtn, &QPushButton::clicked, m_cleanPage, &CleanPage::openSettings);
    sv->addWidget(settingsBtn);

    root->addWidget(sidebar);
    root->addWidget(content, 1);
    setCentralWidget(central);
    DiskOrganizer::applyCardShadows(this);
}

QWidget* MainWindow::buildOverviewPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);

    auto* head = new QHBoxLayout;
    auto* headCol = new QVBoxLayout;
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto* header = new QLabel(tr("磁盘概览"), page);
    header->setStyleSheet("font-size:24px; font-weight:800; color:#181445; background:transparent;");
    m_overviewPill = new QLabel(tr("0 卷已装载"), page);
    m_overviewPill->setStyleSheet(
        "padding:3px 10px; border-radius:12px; background-color:#E8E6FB; color:#4B41E1;"
        "font-size:12px; font-weight:600;");
    titleRow->addWidget(header);
    titleRow->addWidget(m_overviewPill);
    titleRow->addStretch();
    auto* hint = new QLabel(tr("实时检测本地驱动器与卷状态 · 双击磁盘行进入空间分析"), page);
    hint->setProperty("class", "hint");
    headCol->addLayout(titleRow);
    headCol->addWidget(hint);
    head->addLayout(headCol, 1);

    auto* settingsBtn = new QPushButton(tr("自动清理设置"), page);
    settingsBtn->setProperty("class", "secondary");
    connect(settingsBtn, &QPushButton::clicked, m_cleanPage, &CleanPage::openSettings);
    auto* scanAllBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")),
                                       tr("立即扫描所有盘"), page);
    connect(scanAllBtn, &QPushButton::clicked, this, [this] {
        // 切到空间分析并对第一个本地盘启动扫描
        const DiskItemList disks = enumerateDisks();
        QString first;
        for (const auto& d : disks) {
            if (d.totalBytes > 0) { first = d.driveLetter; break; }
        }
        m_stack->setCurrentIndex(4);
        if (!first.isEmpty())
            m_analyzerPage->scanPath(first.endsWith(':') ? first + "/" : first);
    });
    head->addWidget(settingsBtn);
    head->addWidget(scanAllBtn);
    v->addLayout(head);

    auto* chartsRow = new QHBoxLayout;
    chartsRow->setSpacing(14);

    auto* poolCard = new QFrame(page);
    poolCard->setProperty("class", "card");
    auto* pv = new QVBoxLayout(poolCard);
    pv->setContentsMargins(18, 14, 18, 14);
    pv->setSpacing(8);
    auto* poolHead = new QHBoxLayout;
    auto* poolTitleCol = new QVBoxLayout;
    auto* poolTitle = new QLabel(tr("存储池总使用率"), page);
    poolTitle->setStyleSheet("color:#6C7A77; font-size:12px; font-weight:600; background:transparent;");
    m_poolTotal = new QLabel(page);
    m_poolTotal->setStyleSheet("font-size:18px; font-weight:800; color:#181445; background:transparent;");
    poolTitleCol->addWidget(poolTitle);
    poolTitleCol->addWidget(m_poolTotal);
    poolHead->addLayout(poolTitleCol, 1);
    m_poolPill = new QLabel(page);
    m_poolPill->setStyleSheet(
        "padding:3px 10px; border-radius:10px; background-color:rgba(245,158,11,0.12);"
        "color:#92400E; font-size:11px; font-weight:700;");
    poolHead->addWidget(m_poolPill);
    pv->addLayout(poolHead);

    auto* poolBody = new QHBoxLayout;
    poolBody->setSpacing(16);
    poolBody->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_diskPie = new PieChart;
    m_diskPie->setShowLegend(false);
    m_diskPie->setFixedSize(200, 200);
    m_diskPie->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    poolBody->addWidget(m_diskPie, 0, Qt::AlignVCenter);

    m_pieLegendHost = new QWidget(page);
    m_pieLegendHost->setMinimumWidth(180);
    m_pieLegendLay = new QVBoxLayout(m_pieLegendHost);
    m_pieLegendLay->setContentsMargins(0, 8, 0, 8);
    m_pieLegendLay->setSpacing(8);
    m_pieLegendLay->addStretch();
    poolBody->addWidget(m_pieLegendHost, 1);
    pv->addLayout(poolBody, 1);
    chartsRow->addWidget(poolCard, 5);

    auto* cmpCard = new QFrame(page);
    cmpCard->setProperty("class", "card");
    auto* cvv = new QVBoxLayout(cmpCard);
    cvv->setContentsMargins(18, 14, 18, 14);
    cvv->setSpacing(8);
    auto* cmpHead = new QHBoxLayout;
    auto* cmpTitleCol = new QVBoxLayout;
    auto* cmpTitle = new QLabel(tr("驱动器空间对比"), page);
    cmpTitle->setStyleSheet("color:#6C7A77; font-size:12px; font-weight:600; background:transparent;");
    m_cmpSubtitle = new QLabel(page);
    m_cmpSubtitle->setStyleSheet("font-size:18px; font-weight:800; color:#181445; background:transparent;");
    cmpTitleCol->addWidget(cmpTitle);
    cmpTitleCol->addWidget(m_cmpSubtitle);
    cmpHead->addLayout(cmpTitleCol, 1);
    auto* unitPill = new QLabel(tr("单位: GB / TB"), page);
    unitPill->setStyleSheet(
        "padding:3px 10px; border-radius:10px; background-color:#F4F4F0;"
        "color:#181445; font-size:11px; font-weight:600;");
    cmpHead->addWidget(unitPill);
    cvv->addLayout(cmpHead);

    m_barList = new QVBoxLayout;
    m_barList->setSpacing(10);
    cvv->addLayout(m_barList, 1);
    auto* legendRow = new QHBoxLayout;
    m_barLegend = new QLabel(page);
    m_barLegend->setTextFormat(Qt::RichText);
    m_barLegend->setStyleSheet("color:#6C7A77; font-size:11px; background:transparent;");
    legendRow->addWidget(m_barLegend, 1);
    auto* cycle = new QLabel(tr("自检周期: 实时"), page);
    cycle->setStyleSheet("color:#6C7A77; font-size:11px; background:transparent;");
    legendRow->addWidget(cycle);
    cvv->addLayout(legendRow);
    chartsRow->addWidget(cmpCard, 7);
    v->addLayout(chartsRow);

    auto* tableCard = new QFrame(page);
    tableCard->setProperty("class", "card");
    auto* tv = new QVBoxLayout(tableCard);
    tv->setContentsMargins(12, 12, 12, 12);
    m_diskTable = new QTableWidget(0, 7, this);
    m_diskTable->setHorizontalHeaderLabels({tr("盘符"), tr("卷标"), tr("文件系统"),
                                            tr("总容量"), tr("可用空间"), tr("使用率"),
                                            tr("快捷维护")});
    auto* diskHdr = m_diskTable->horizontalHeader();
    diskHdr->setStretchLastSection(false);
    diskHdr->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    diskHdr->setSectionResizeMode(1, QHeaderView::Stretch);
    diskHdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    diskHdr->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    diskHdr->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    diskHdr->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    diskHdr->setSectionResizeMode(6, QHeaderView::Fixed);
    m_diskTable->setColumnWidth(6, 148);
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
               "本程序基于 Qt 6（LGPLv3）。"));
    });
}

void MainWindow::refreshDisks() {
    const DiskItemList disks = enumerateDisks();
    m_diskTable->setRowCount(disks.size());
    m_overviewPill->setText(tr("%1 卷已装载").arg(disks.size()));
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
        const double ratio = d.usedRatio();
        const QColor dotColor = ratio > 0.9 ? QColor("#D93026")
                              : ratio > 0.7 ? QColor("#E8A13A")
                                            : QColor("#2E9E5B");
        auto* usageItem = new QTableWidgetItem(
            QString::fromUtf8("\xE2\x97\x8F ") + QString::number(ratio * 100, 'f', 1) + '%');
        usageItem->setForeground(dotColor);
        m_diskTable->setItem(i, 5, usageItem);

        // 快捷维护：清理 / 分析（紧凑样式，避免全局 pill padding 裁切文案）
        auto* actions = new QWidget;
        auto* ah = new QHBoxLayout(actions);
        ah->setContentsMargins(4, 2, 4, 2);
        ah->setSpacing(8);
        const QString compactBtn =
            QStringLiteral("QPushButton{padding:4px 12px; min-width:56px; border-radius:10px;}");
        auto* cleanBtn = new QPushButton(tr("清理"), actions);
        cleanBtn->setProperty("class", "secondary");
        cleanBtn->setFixedHeight(28);
        cleanBtn->setStyleSheet(compactBtn);
        auto* analyzeBtn = new QPushButton(tr("分析"), actions);
        analyzeBtn->setProperty("class", "secondary");
        analyzeBtn->setFixedHeight(28);
        analyzeBtn->setStyleSheet(compactBtn);
        const QString drive = d.driveLetter;
        connect(cleanBtn, &QPushButton::clicked, this, [this] {
            m_stack->setCurrentIndex(1);
        });
        connect(analyzeBtn, &QPushButton::clicked, this, [this, drive] {
            m_stack->setCurrentIndex(4);
            m_analyzerPage->scanPath(drive.endsWith(':') ? drive + "/" : drive);
        });
        ah->addWidget(cleanBtn);
        ah->addWidget(analyzeBtn);
        ah->addStretch();
        m_diskTable->setCellWidget(i, 6, actions);
        m_diskTable->setRowHeight(i, 40);
    }

    QList<QPair<QString, double>> pie;
    qint64 poolTotal = 0, poolUsed = 0, poolFree = 0;
    for (const auto& d : disks) {
        if (d.totalBytes <= 0) continue;
        const QString label = d.volumeLabel.isEmpty() ? d.driveLetter : d.driveLetter + " " + d.volumeLabel;
        pie.append({label, double(d.totalBytes - d.freeBytes)});
        poolTotal += d.totalBytes;
        poolUsed += d.totalBytes - d.freeBytes;
        poolFree += d.freeBytes;
    }
    m_diskPie->setData(pie);
    if (poolTotal > 0) {
        const double usedRatio = double(poolUsed) / poolTotal;
        m_poolTotal->setText(tr("%1 / %2").arg(formatSize(poolUsed)).arg(formatSize(poolTotal)));
        m_poolPill->setText(tr("已用 %1%").arg(usedRatio * 100, 0, 'f', 1));
        m_diskPie->setCenterLabel(tr("剩余可用"), formatSize(poolFree),
                                  tr("%1% FREE").arg((1.0 - usedRatio) * 100, 0, 'f', 1));
        // 外置图例：每行固定不换行（避免 HTML QLabel 被挤成竖排）
        while (QLayoutItem* it = m_pieLegendLay->takeAt(0)) {
            if (auto* w = it->widget()) w->deleteLater();
            delete it;
        }
        for (int i = 0; i < disks.size(); ++i) {
            const DiskItem& d = disks[i];
            if (d.totalBytes <= 0) continue;
            const QString label = d.volumeLabel.isEmpty()
                ? d.driveLetter
                : d.driveLetter + QLatin1Char(' ') + d.volumeLabel;
            const QColor c = DiskOrganizer::diskSegmentColor(i);
            const qint64 used = d.totalBytes - d.freeBytes;
            const double pct = poolUsed > 0 ? 100.0 * used / poolUsed : 0;

            auto* row = new QWidget(m_pieLegendHost);
            auto* rh = new QHBoxLayout(row);
            rh->setContentsMargins(0, 0, 0, 0);
            rh->setSpacing(8);
            auto* swatch = new QLabel(row);
            swatch->setFixedSize(12, 12);
            swatch->setStyleSheet(
                QString("background:%1; border-radius:3px;").arg(c.name()));
            auto* text = new QLabel(
                tr("%1 · %2（%3%）").arg(label, formatSize(used)).arg(pct, 0, 'f', 1), row);
            text->setStyleSheet(
                "font-size:12px; color:#181445; background:transparent;");
            text->setWordWrap(false);
            text->setMinimumWidth(160);
            rh->addWidget(swatch, 0, Qt::AlignVCenter);
            rh->addWidget(text, 1, Qt::AlignVCenter);
            m_pieLegendLay->addWidget(row);
        }
        m_pieLegendLay->addStretch();
        if (m_sidebarPoolLabel) {
            m_sidebarPoolLabel->setText(tr("%1 / %2 · %3%")
                .arg(formatSize(poolUsed), formatSize(poolTotal))
                .arg(usedRatio * 100, 0, 'f', 0));
            m_sidebarPoolBar->setValue(int(usedRatio * 100));
        }
    } else {
        m_poolTotal->setText("—");
        m_poolPill->setText(tr("无数据"));
        while (QLayoutItem* it = m_pieLegendLay->takeAt(0)) {
            if (auto* w = it->widget()) w->deleteLater();
            delete it;
        }
        m_pieLegendLay->addStretch();
    }
    m_cmpSubtitle->setText(tr("%1 卷在线分配情况").arg(disks.size()));

    // 清空旧进度条 fill，避免泄漏
    m_barFills.clear();
    m_barTimers.clear();
    while (m_barList->count() > 0) {
        QLayoutItem* it = m_barList->takeAt(0);
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
    for (int i = 0; i < disks.size(); ++i) {
        const DiskItem& d = disks[i];
        if (d.totalBytes <= 0) continue;
        const double ratio = d.usedRatio();
        const QColor c = ratio > 0.9 ? QColor("#EF4444")
                       : ratio > 0.7 ? QColor("#F59E0B")
                                     : QColor("#10B981");
        auto* rowWidget = new QWidget(this);
        auto* row = new QVBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto* top = new QHBoxLayout;
        top->setSpacing(8);
        auto* drivePill = new QLabel(d.driveLetter, rowWidget);
        drivePill->setStyleSheet(
            "padding:2px 8px; border-radius:6px; background-color:#F4F4F0;"
            "color:#181445; font-weight:700; font-size:12px;");
        auto* nameLabel = new QLabel(
            d.volumeLabel.isEmpty() ? tr("本地磁盘") : d.volumeLabel, rowWidget);
        nameLabel->setStyleSheet("font-weight:600; color:#181445; background:transparent;");
        top->addWidget(drivePill);
        top->addWidget(nameLabel);
        top->addStretch();
        auto* valLabel = new QLabel(rowWidget);
        valLabel->setTextFormat(Qt::RichText);
        valLabel->setText(tr("可用 <span style='color:%1;font-weight:700;'>%2</span> / %3"
                             "&nbsp;&nbsp;<span style='background-color:rgba(0,0,0,0.06);"
                             "padding:1px 6px;border-radius:6px;font-weight:600;'>已用 %4%</span>")
                              .arg(c.name(), formatSize(d.freeBytes),
                                   formatSize(d.totalBytes))
                              .arg(ratio * 100, 0, 'f', 1));
        valLabel->setStyleSheet("font-size:12px; color:#6C7A77; background:transparent;");
        top->addWidget(valLabel);
        row->addLayout(top);

        auto* barBg = new QFrame(rowWidget);
        barBg->setFixedHeight(10);
        barBg->setStyleSheet(
            "QFrame{background:#F4F4F0; border-radius:5px; border:1px solid #EFECF7;}");
        auto* fill = new QFrame(barBg);
        fill->setStyleSheet(QString("QFrame{background:%1; border-radius:4px; border:none;}").arg(c.name()));
        fill->setGeometry(1, 1, 2, 8);
        barBg->installEventFilter(this);
        m_barFills.append({fill, ratio});
        QElapsedTimer t;
        t.start();
        m_barTimers.append(t);
        row->addWidget(barBg);
        m_barList->addWidget(rowWidget);
    }
    m_barAnimTimer.restart();
    animateBars();
    m_barLegend->setText(tr(
        "<span style='color:#EF4444'>●</span> 空间紧缺 (>90%) &nbsp; "
        "<span style='color:#F59E0B'>●</span> 预警 (70-90%) &nbsp; "
        "<span style='color:#10B981'>●</span> 充裕 (<70%)"));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::Resize) {
        for (const auto& bf : m_barFills) {
            if (bf.fill->parentWidget() == obj) {
                const int w = qMax(2, int((bf.fill->parentWidget()->width() - 2) * bf.ratio));
                bf.fill->resize(w, bf.fill->height());
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::animateBars() {
    const qint64 elapsed = m_barAnimTimer.elapsed();
    const double t = qMin(1.0, elapsed / 500.0);
    const double ease = 1.0 - std::pow(1.0 - t, 3.0);
    bool done = true;
    for (int i = 0; i < m_barFills.size(); ++i) {
        auto& bf = m_barFills[i];
        QWidget* bg = bf.fill->parentWidget();
        const int fullW = qMax(2, int((bg->width() - 2) * bf.ratio * ease));
        if (bf.fill->width() != fullW) bf.fill->resize(fullW, bf.fill->height());
        if (t < 1.0) done = false;
    }
    if (!done) QTimer::singleShot(16, this, &MainWindow::animateBars);
}

void MainWindow::openDiskInAnalyzer(int row, int column) {
    Q_UNUSED(column)
    const QString drive = m_diskTable->item(row, 0)->text();
    m_stack->setCurrentIndex(4);
    m_analyzerPage->scanPath(drive.endsWith(':') ? drive + "/" : drive);
}

} // namespace DiskOrganizer
