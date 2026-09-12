#include "Icons.h"
#include "SpaceAnalyzerPage.h"
#include "Charts.h"
#include "FlatStyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include "services/CleanerService.h"
#include "services/ReportService.h"
#include "services/ScannerService.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

namespace {
QColor folderColor(int i) {
    static const QColor c[] = {
        QColor(0x8B, 0x5C, 0xF6), QColor(0xF4, 0x3F, 0x5E), QColor(0x10, 0xB9, 0x81),
        QColor(0x0E, 0xA5, 0xE9), QColor(0x14, 0xB8, 0xA6), QColor(0xF5, 0x9E, 0x0B),
    };
    return c[i % 6];
}
} // namespace

SpaceAnalyzerPage::SpaceAnalyzerPage(QWidget* parent) : PageBase(parent) {
    buildUi();
}

void SpaceAnalyzerPage::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // 页头
    auto* head = new QHBoxLayout;
    auto* headCol = new QVBoxLayout;
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto* title = new QLabel(tr("空间分析"), this);
    title->setStyleSheet("font-size:24px; font-weight:800; color:#181445; background:transparent;");
    m_titlePill = new QLabel(tr("尚未扫描"), this);
    m_titlePill->setStyleSheet(
        "padding:3px 10px; border-radius:12px; background-color:#E8E6FB; color:#4B41E1;"
        "font-size:12px; font-weight:600;");
    titleRow->addWidget(title);
    titleRow->addWidget(m_titlePill);
    titleRow->addStretch();
    auto* subtitle = new QLabel(tr("深入解析硬盘空间消耗结构与大容量层级"), this);
    subtitle->setStyleSheet("color:#6C7A77; background:transparent;");
    headCol->addLayout(titleRow);
    headCol->addWidget(subtitle);
    head->addLayout(headCol, 1);

    m_exportBtn = new QPushButton(tr("导出报告"), this);
    m_exportBtn->setProperty("class", "secondary");
    m_exportBtn->setEnabled(false);
    m_cleanBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::trash), QColor("white")),
                                 tr("清理选中"), this);
    m_cleanBtn->setEnabled(false);
    head->addWidget(m_exportBtn);
    head->addWidget(m_cleanBtn);
    root->addLayout(head);

    // 面包屑 + 路径 + 扫描
    auto* pathRow = new QHBoxLayout;
    m_breadcrumb = new QLabel(tr("此电脑"), this);
    m_breadcrumb->setStyleSheet("color:#6C7A77; font-size:12px; background:transparent;");
    pathRow->addWidget(m_breadcrumb, 1);
    m_pathEdit = new QLineEdit(QDir::rootPath(), this);
    m_pathEdit->setFixedWidth(280);
    auto* browse = new QPushButton(tr("浏览…"), this);
    browse->setProperty("class", "secondary");
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择目录"), m_pathEdit->text());
        if (!dir.isEmpty()) { m_pathEdit->setText(dir); scanPath(dir); }
    });
    m_scanBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")),
                                tr("重新扫描"), this);
    pathRow->addWidget(m_pathEdit);
    pathRow->addWidget(browse);
    pathRow->addWidget(m_scanBtn);
    root->addLayout(pathRow);

    // 视图切换
    auto* viewRow = new QHBoxLayout;
    auto* treemapBtn = new QPushButton(tr("树状图"), this);
    auto* listBtn = new QPushButton(tr("清单"), this);
    auto* pieBtn = new QPushButton(tr("饼状图"), this);
    for (auto* b : {treemapBtn, listBtn, pieBtn}) {
        b->setCheckable(true);
        b->setProperty("class", "secondary");
    }
    treemapBtn->setChecked(true);
    m_depthCombo = new QComboBox(this);
    for (int d = 2; d <= 5; ++d)
        m_depthCombo->addItem(tr("层级深度: %1 层").arg(d), d);
    m_depthCombo->setCurrentIndex(1); // 3
    viewRow->addWidget(treemapBtn);
    viewRow->addWidget(listBtn);
    viewRow->addWidget(pieBtn);
    viewRow->addStretch();
    viewRow->addWidget(m_depthCombo);
    root->addLayout(viewRow);

    m_progress = new QProgressBar(this);
    m_progress->hide();
    root->addWidget(m_progress);

    // 主区：左可视化 + 右 Top Folders
    auto* body = new QHBoxLayout;
    body->setSpacing(14);

    auto* leftCard = new QFrame(this);
    leftCard->setProperty("class", "card");
    auto* lv = new QVBoxLayout(leftCard);
    lv->setContentsMargins(14, 12, 14, 12);
    m_viewStack = new QStackedWidget(leftCard);
    m_treemap = new TreemapWidget(m_viewStack);
    m_listTree = new QTreeWidget(m_viewStack);
    m_listTree->setHeaderLabels({tr("目录"), tr("大小")});
    m_listTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_listTree->setSortingEnabled(true);
    m_listTree->sortByColumn(1, Qt::DescendingOrder);
    m_pie = new PieChart(m_viewStack);
    m_viewStack->addWidget(m_treemap);
    m_viewStack->addWidget(m_listTree);
    m_viewStack->addWidget(m_pie);
    lv->addWidget(m_viewStack, 1);
    m_focusLabel = new QLabel(tr("当前聚焦：—"), leftCard);
    m_focusLabel->setStyleSheet("color:#6C7A77; font-size:12px; background:transparent;");
    lv->addWidget(m_focusLabel);
    m_typeBar = new SegmentedBar(leftCard);
    lv->addWidget(m_typeBar);
    m_typeLegend = new QLabel(leftCard);
    m_typeLegend->setWordWrap(true);
    m_typeLegend->setStyleSheet("color:#6C7A77; font-size:11px; background:transparent;");
    lv->addWidget(m_typeLegend);
    body->addWidget(leftCard, 6);

    auto* rightCard = new QFrame(this);
    rightCard->setProperty("class", "card");
    auto* rv = new QVBoxLayout(rightCard);
    rv->setContentsMargins(14, 12, 14, 12);
    auto* rightTitle = new QLabel(tr("目录占用排行 Top Folders"), rightCard);
    rightTitle->setStyleSheet("font-weight:700; color:#181445; background:transparent;");
    rv->addWidget(rightTitle);
    m_folderScroll = new QScrollArea(rightCard);
    m_folderScroll->setWidgetResizable(true);
    m_folderScroll->setFrameShape(QFrame::NoFrame);
    m_folderList = new QWidget;
    m_folderList->setLayout(new QVBoxLayout);
    m_folderList->layout()->setContentsMargins(0, 0, 0, 0);
    m_folderList->layout()->setSpacing(8);
    static_cast<QVBoxLayout*>(m_folderList->layout())->addStretch();
    m_folderScroll->setWidget(m_folderList);
    rv->addWidget(m_folderScroll, 1);
    m_folderSummary = new QLabel(tr("已选中 0 项"), rightCard);
    m_folderSummary->setStyleSheet("color:#6C7A77; font-size:12px; background:transparent;");
    rv->addWidget(m_folderSummary);
    body->addWidget(rightCard, 4);
    root->addLayout(body, 1);

    connect(m_scanBtn, &QPushButton::clicked, this, &SpaceAnalyzerPage::cancelOrRescan);
    connect(m_cleanBtn, &QPushButton::clicked, this, &SpaceAnalyzerPage::doCleanSelected);
    connect(m_exportBtn, &QPushButton::clicked, this, &SpaceAnalyzerPage::doExport);
    connect(treemapBtn, &QPushButton::clicked, this, [this, treemapBtn, listBtn, pieBtn] {
        treemapBtn->setChecked(true); listBtn->setChecked(false); pieBtn->setChecked(false);
        m_viewStack->setCurrentIndex(0);
    });
    connect(listBtn, &QPushButton::clicked, this, [this, treemapBtn, listBtn, pieBtn] {
        listBtn->setChecked(true); treemapBtn->setChecked(false); pieBtn->setChecked(false);
        m_viewStack->setCurrentIndex(1);
    });
    connect(pieBtn, &QPushButton::clicked, this, [this, treemapBtn, listBtn, pieBtn] {
        pieBtn->setChecked(true); treemapBtn->setChecked(false); listBtn->setChecked(false);
        m_viewStack->setCurrentIndex(2);
    });
    connect(m_depthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        m_treemap->setDepth(m_depthCombo->currentData().toInt());
    });
    connect(m_treemap, &TreemapWidget::focusChanged, this, [this](const QString& path, qint64 size) {
        m_focusLabel->setText(tr("当前聚焦：%1  %2").arg(path, formatSize(size)));
        updateBreadcrumb(path);
    });
    connect(m_treemap, &TreemapWidget::pathActivated, this, [this](const QString& path) {
        m_checked.insert(path);
        rebuildFolderList();
        updateCleanButton();
    });

    DiskOrganizer::applyCardShadows(this);
}

void SpaceAnalyzerPage::updateBreadcrumb(const QString& path) {
    QStringList parts = QDir::fromNativeSeparators(path).split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        m_breadcrumb->setText(tr("此电脑"));
        return;
    }
    QString crumb = tr("此电脑");
    for (const QString& p : parts) crumb += QStringLiteral(" / ") + p;
    m_breadcrumb->setText(crumb);
}

void SpaceAnalyzerPage::cancelOrRescan() {
    if (m_scanning) {
        m_cancelled.store(true);
        m_titlePill->setText(tr("正在取消……"));
        return;
    }
    scanPath(m_pathEdit->text());
}

void SpaceAnalyzerPage::scanPath(const QString& path) {
    if (!QFileInfo::exists(path)) {
        m_titlePill->setText(tr("路径不存在"));
        return;
    }
    if (m_scanning) {
        m_cancelled.store(true);
        return;
    }
    m_scanning = true;
    m_cancelled.store(false);
    m_rootPath = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    m_pathEdit->setText(m_rootPath);
    m_checked.clear();
    m_progress->setRange(0, 0);
    m_progress->show();
    m_scanBtn->setText(tr("取消"));
    m_titlePill->setText(tr("扫描中……"));
    m_exportBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);
    updateBreadcrumb(m_rootPath);

    (void)QtConcurrent::run([this]() {
        ScannerService scanner;
        auto cancelled = [this]() { return m_cancelled.load(); };
        std::function<bool(qint64, const QString&)> onProgress =
            [this, cancelled](qint64, const QString& cur) -> bool {
            if (cancelled()) return false;
            QMetaObject::invokeMethod(this, [this, cur]() {
                m_titlePill->setText(tr("扫描中：%1").arg(
                    fontMetrics().elidedText(cur, Qt::ElideMiddle, 220)));
            }, Qt::QueuedConnection);
            return true;
        };
        QList<FileInfo> files = scanner.scanBlocking({m_rootPath}, onProgress, 0, 0, cancelled);
        if (cancelled()) {
            QMetaObject::invokeMethod(this, [this]() { resetScanUi(); }, Qt::QueuedConnection);
            return;
        }
        SpaceAnalyzer analyzer;
        const auto dirs = analyzer.directorySizes(files);
        const auto types = analyzer.typeDistribution(files);
        const qint64 n = files.size();
        QMetaObject::invokeMethod(this, [this, dirs, types, n]() {
            applyResults(dirs, types, n, 0);
        }, Qt::QueuedConnection);
    });
}

void SpaceAnalyzerPage::resetScanUi() {
    m_scanning = false;
    m_progress->hide();
    m_scanBtn->setText(tr("重新扫描"));
    m_titlePill->setText(tr("扫描已取消"));
}

void SpaceAnalyzerPage::applyResults(const QList<QPair<QString, qint64>>& dirs,
                                     const QList<TypeStat>& types, qint64 fileCount, qint64) {
    m_scanning = false;
    m_progress->hide();
    m_scanBtn->setText(tr("重新扫描"));
    m_dirs = dirs;
    m_types = types;
    m_exportBtn->setEnabled(true);

    qint64 total = 0;
    for (const auto& d : dirs) {
        if (QDir::fromNativeSeparators(d.first)
                .compare(m_rootPath, Qt::CaseInsensitive) == 0) {
            total = d.second;
            break;
        }
    }
    if (total <= 0 && !dirs.isEmpty()) total = dirs.first().second;

    m_titlePill->setText(tr("已完成扫描 · %1 (%2 个对象)")
                             .arg(formatSize(total)).arg(fileCount));

    TreemapNode tree = buildTree(dirs, m_rootPath);
    m_treemap->setDepth(m_depthCombo->currentData().toInt());
    m_treemap->setRoot(tree);

    m_listTree->clear();
    int shown = 0;
    for (const auto& [dir, size] : dirs) {
        if (++shown > 200) break;
        auto* it = new QTreeWidgetItem(m_listTree, {dir, formatSize(size)});
        it->setData(0, Qt::UserRole, dir);
        it->setData(1, Qt::UserRole, size);
    }

    QList<QPair<QString, double>> pie;
    QList<QPair<QColor, double>> segs;
    QString legend;
    int ti = 0;
    qint64 typeTotal = 0;
    for (const auto& t : types) typeTotal += t.totalBytes;
    for (const auto& t : types) {
        if (ti >= 8) break;
        if (t.extension.isEmpty() || t.totalBytes <= 0) continue;
        const QString name = t.extension;
        pie.append({name, double(t.totalBytes)});
        const QColor c = folderColor(ti);
        segs.append({c, double(t.totalBytes)});
        const double pct = typeTotal > 0 ? 100.0 * t.totalBytes / typeTotal : 0;
        legend += QString("<span style='color:%1'>●</span> %2 %3% &nbsp; ")
                      .arg(c.name(), name.toHtmlEscaped()).arg(pct, 0, 'f', 0);
        ++ti;
    }
    m_pie->setData(pie);
    m_typeBar->setSegments(segs);
    m_typeLegend->setText(legend);

    rebuildFolderList();
    updateCleanButton();
}

TreemapNode SpaceAnalyzerPage::buildTree(const QList<QPair<QString, qint64>>& dirs,
                                         const QString& root) const {
    const QString rootNorm = QDir::fromNativeSeparators(root);
    TreemapNode rootNode;
    rootNode.path = rootNorm;
    rootNode.label = QFileInfo(rootNorm).fileName().isEmpty()
        ? rootNorm : QFileInfo(rootNorm).fileName();

    // 取紧挨 root 的一级子目录
    QHash<QString, qint64> direct;
    for (const auto& [path, size] : dirs) {
        const QString p = QDir::fromNativeSeparators(path);
        if (p.compare(rootNorm, Qt::CaseInsensitive) == 0) {
            rootNode.size = size;
            continue;
        }
        if (!p.startsWith(rootNorm, Qt::CaseInsensitive)) continue;
        QString rest = p.mid(rootNorm.size());
        if (rest.startsWith('/')) rest = rest.mid(1);
        if (rest.isEmpty()) continue;
        const QString first = rest.section('/', 0, 0);
        if (first.isEmpty()) continue;
        const QString childPath = rootNorm.endsWith('/')
            ? rootNorm + first : rootNorm + '/' + first;
        // 仅当 path 恰好是一级子目录时记 size（避免孙目录覆盖）
        if (rest == first)
            direct[childPath] = size;
    }
    if (rootNode.size <= 0) {
        for (auto it = direct.constBegin(); it != direct.constEnd(); ++it)
            rootNode.size += it.value();
    }

    int idx = 0;
    for (auto it = direct.constBegin(); it != direct.constEnd(); ++it) {
        TreemapNode child;
        child.path = it.key();
        child.label = QFileInfo(it.key()).fileName();
        child.size = it.value();
        child.color = folderColor(idx++);
        // 二级子目录
        QHash<QString, qint64> grand;
        for (const auto& [path, size] : dirs) {
            const QString p = QDir::fromNativeSeparators(path);
            if (!p.startsWith(child.path, Qt::CaseInsensitive)) continue;
            QString rest = p.mid(child.path.size());
            if (rest.startsWith('/')) rest = rest.mid(1);
            if (rest.isEmpty() || rest.contains('/')) {
                if (!rest.contains('/') && !rest.isEmpty())
                    grand[child.path + '/' + rest] = size;
                continue;
            }
            grand[child.path + '/' + rest] = size;
        }
        int g = 0;
        for (auto git = grand.constBegin(); git != grand.constEnd(); ++git) {
            TreemapNode gc;
            gc.path = git.key();
            gc.label = QFileInfo(git.key()).fileName();
            gc.size = git.value();
            gc.color = child.color.lighter(100 + (g++ % 4) * 12);
            child.children.append(gc);
        }
        std::sort(child.children.begin(), child.children.end(),
                  [](const TreemapNode& a, const TreemapNode& b) { return a.size > b.size; });
        if (child.children.size() > 12)
            child.children = child.children.mid(0, 12);
        rootNode.children.append(child);
    }
    std::sort(rootNode.children.begin(), rootNode.children.end(),
              [](const TreemapNode& a, const TreemapNode& b) { return a.size > b.size; });
    if (rootNode.children.size() > 16)
        rootNode.children = rootNode.children.mid(0, 16);
    return rootNode;
}

void SpaceAnalyzerPage::rebuildFolderList() {
    QLayout* lay = m_folderList->layout();
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
    int shown = 0;
    qint64 checkedBytes = 0;
    int checkedCount = 0;
    for (const auto& [dir, size] : m_dirs) {
        if (++shown > 40) break;
        auto* row = new QFrame(m_folderList);
        row->setStyleSheet("QFrame{background:#FAFAF8; border-radius:10px;}");
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(10, 8, 10, 8);
        auto* cb = new QCheckBox(row);
        cb->setChecked(m_checked.contains(dir));
        connect(cb, &QCheckBox::toggled, this, [this, dir, size](bool on) {
            if (on) m_checked.insert(dir); else m_checked.remove(dir);
            updateCleanButton();
            // 刷新底部汇总
            qint64 bytes = 0;
            for (const auto& [d, s] : m_dirs)
                if (m_checked.contains(d)) bytes += s;
            m_folderSummary->setText(tr("已选中 %1 项（共计 %2）")
                                         .arg(m_checked.size()).arg(formatSize(bytes)));
        });
        auto* name = new QLabel(QFileInfo(dir).fileName().isEmpty() ? dir : QFileInfo(dir).fileName(), row);
        name->setStyleSheet("font-weight:600; background:transparent;");
        name->setToolTip(dir);
        auto* sizeLab = new QLabel(formatSize(size), row);
        sizeLab->setStyleSheet("font-weight:700; color:#4B41E1; background:transparent;");
        h->addWidget(cb);
        h->addWidget(name, 1);
        h->addWidget(sizeLab);
        auto* openBtn = new QPushButton(tr("打开"), row);
        openBtn->setProperty("class", "ghost");
        connect(openBtn, &QPushButton::clicked, this, [dir] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        h->addWidget(openBtn);
        lay->addWidget(row);
        if (m_checked.contains(dir)) {
            ++checkedCount;
            checkedBytes += size;
        }
    }
    static_cast<QVBoxLayout*>(lay)->addStretch();
    m_folderSummary->setText(tr("已选中 %1 项（共计 %2）")
                                 .arg(checkedCount).arg(formatSize(checkedBytes)));
}

void SpaceAnalyzerPage::updateCleanButton() {
    qint64 bytes = 0;
    for (const auto& [d, s] : m_dirs)
        if (m_checked.contains(d)) bytes += s;
    m_cleanBtn->setEnabled(!m_checked.isEmpty() && !m_scanning);
    m_cleanBtn->setText(m_checked.isEmpty()
        ? tr("清理选中")
        : tr("清理选中 (%1)").arg(formatSize(bytes)));
}

void SpaceAnalyzerPage::doCleanSelected() {
    if (m_checked.isEmpty()) return;
    if (QMessageBox::question(this, tr("确认清理"),
            tr("将把选中的 %1 个目录/文件移入回收站？").arg(m_checked.size()))
        != QMessageBox::Yes)
        return;
    CleanerService cleaner;
    QList<CleanItem> items;
    for (const QString& path : m_checked) {
        CleanItem ci;
        ci.path = path;
        ci.size = QFileInfo(path).isDir() ? 0 : QFileInfo(path).size();
        for (const auto& [d, s] : m_dirs)
            if (d.compare(path, Qt::CaseInsensitive) == 0) { ci.size = s; break; }
        ci.safeToDelete = true;
        items.append(ci);
    }
    const qint64 freed = cleaner.clean(items, true);
    m_titlePill->setText(tr("已清理，释放约 %1").arg(formatSize(freed)));
    m_checked.clear();
    scanPath(m_rootPath);
}

void SpaceAnalyzerPage::doExport() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出分析报告"), QStringLiteral("space-report.csv"),
        tr("CSV (*.csv);;所有文件 (*.*)"));
    if (path.isEmpty()) return;
    QList<FileInfo> files;
    for (const auto& [dir, size] : m_dirs) {
        FileInfo fi;
        fi.absolutePath = dir;
        fi.size = size;
        fi.isDir = true;
        files.append(fi);
    }
    if (ReportService::exportScanReport(path, tr("空间分析报告"), files))
        m_titlePill->setText(tr("报告已导出"));
    else
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件"));
}

} // namespace DiskOrganizer
