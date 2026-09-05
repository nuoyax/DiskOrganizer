#include "CleanPage.h"
#include "Charts.h"
#include "util/SizeFormatter.h"
#include "SettingsDialog.h"
#include <QCheckBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace DiskOrganizer {

namespace {
// item data roles
constexpr int kRolePath     = Qt::UserRole;      // 完整路径
constexpr int kRoleSize     = Qt::UserRole + 1;  // 字节数
constexpr int kRoleCategory = Qt::UserRole + 2;  // 类别枚举值（存于类别节点）
constexpr int kRoleCautious = Qt::UserRole + 3;  // 是否"谨慎"项
}

CleanPage::CleanPage(QWidget* parent) : PageBase(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("扫描并清理系统垃圾文件，按类别勾选需要处理的项目"));
    title->setStyleSheet("color:#636E88; background:transparent;");
    root->addWidget(title);

    // 分类复选框
    auto* catRow = new QHBoxLayout;
    catRow->setSpacing(18);
    const char* names[12] = {
        "临时文件", "回收站", "浏览器缓存", "系统日志", "Windows 更新缓存",
        "缩略图缓存", "预读文件", "转储/错误报告", "安装程序缓存", "空文件夹",
        "零字节文件", "自定义规则"
    };
    for (int i = 0; i <= int(CleanCategory::CustomRules); ++i) {
        m_catChecks[i] = new QCheckBox(QString::fromUtf8(names[i]));
        m_catChecks[i]->setChecked(i < 7);           // 默认勾选常用类别
        catRow->addWidget(m_catChecks[i]);
    }
    catRow->addStretch();
    root->addLayout(catRow);

    // 中部：树 + 饼图
    auto* mid = new QHBoxLayout;
    mid->setSpacing(12);
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({tr("文件 / 类别"), tr("大小"), tr("说明")});
    m_tree->setColumnWidth(0, 420);
    m_tree->setColumnWidth(1, 110);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    connect(m_tree, &QTreeWidget::itemChanged, this, &CleanPage::onItemChanged);
    mid->addWidget(m_tree, 3);

    m_pie = new PieChart;
    m_pie->setMinimumWidth(300);
    mid->addWidget(m_pie, 1);
    root->addLayout(mid, 1);

    // 底部
    m_summary = new QLabel(tr("尚未扫描"));
    m_summary->setStyleSheet("color:#636E88; background:transparent;");
    m_progress = new QProgressBar;
    m_progress->setFixedHeight(10);
    m_progress->setTextVisible(false);

    auto* btnRow = new QHBoxLayout;
    m_scanBtn = new QPushButton(tr("开始扫描"));
    m_cleanBtn = new QPushButton(tr("清理选中项"));
    m_cleanBtn->setProperty("class", "secondary");
    m_cleanBtn->setEnabled(false);
    auto* recycle = new QCheckBox(tr("删除到回收站"));
    recycle->setChecked(true);
    connect(recycle, &QCheckBox::toggled, this, [this](bool on) { m_recycleBin = on; });
    auto* settingsBtn = new QPushButton(tr("排除设置"));
    settingsBtn->setProperty("class", "secondary");
    connect(settingsBtn, &QPushButton::clicked, this, &CleanPage::openSettings);

    btnRow->addWidget(m_scanBtn);
    btnRow->addWidget(m_cleanBtn);
    btnRow->addWidget(recycle);
    btnRow->addStretch();
    btnRow->addWidget(settingsBtn);
    root->addWidget(m_progress);
    root->addWidget(m_summary);
    root->addLayout(btnRow);

    connect(m_scanBtn, &QPushButton::clicked, this, &CleanPage::doScan);
    connect(m_cleanBtn, &QPushButton::clicked, this, &CleanPage::doClean);
}

void CleanPage::openSettings() {
    SettingsDialog dlg(this);
    dlg.exec();
}

QString CleanPage::categoryDisplayName(CleanCategory cat) {
    switch (cat) {
        case CleanCategory::TempFiles:       return tr("临时文件");
        case CleanCategory::RecycleBin:      return tr("回收站");
        case CleanCategory::BrowserCache:    return tr("浏览器缓存");
        case CleanCategory::SystemLogs:      return tr("系统日志");
        case CleanCategory::WindowsUpdate:   return tr("Windows 更新缓存");
        case CleanCategory::ThumbnailCache:  return tr("缩略图缓存");
        case CleanCategory::Prefetch:        return tr("预读文件");
        case CleanCategory::DumpFiles:       return tr("转储/错误报告");
        case CleanCategory::InstallerCache:  return tr("安装程序缓存");
        case CleanCategory::EmptyFolders:    return tr("空文件夹");
        case CleanCategory::ZeroByteFiles:   return tr("零字节文件");
        case CleanCategory::CustomRules:     return tr("自定义规则");
    }
    return {};
}

void CleanPage::doScan() {
    m_scanBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);
    m_tree->clear();
    m_items.clear();
    m_summary->setText(tr("正在扫描……"));
    m_progress->setRange(0, 0);   // 忙碌指示

    const int nCats = int(CleanCategory::CustomRules) + 1;
    QList<CleanCategory> cats;
    for (int i = 0; i < nCats; ++i)
        if (m_catChecks[i]->isChecked())
            cats.append(CleanCategory(i));

    auto* itemsPtr = new QList<CleanItem>;
    QtConcurrent::run([itemsPtr, cats]() {
        CleanerService svc;
        *itemsPtr = svc.findCleanableItems(cats);
    }).then(this, [this, itemsPtr]() {
        m_progress->setRange(0, 1);
        m_progress->setValue(1);
        m_items = *itemsPtr;
        delete itemsPtr;

        m_tree->blockSignals(true);
        QMap<int, QList<const CleanItem*>> byCat;
        for (const auto& it : m_items)
            byCat[int(it.category)].append(&it);

        for (auto cIt = byCat.keyBegin(); cIt != byCat.keyEnd(); ++cIt) {
            const int cat = *cIt;
            auto* node = new QTreeWidgetItem(m_tree,
                {categoryDisplayName(CleanCategory(cat)), QString(), QString()});
            node->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
            node->setCheckState(0, Qt::Checked);
            node->setData(0, kRoleCategory, cat);
            qint64 total = 0;
            for (const CleanItem* item : byCat[cat]) {
                auto* child = new QTreeWidgetItem(node,
                    {item->path, formatSize(item->size),
                     item->safeToDelete ? tr("[安全]") : tr("[谨慎] ") + item->description});
                child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                child->setCheckState(0, Qt::Checked);
                child->setData(0, kRolePath, item->path);
                child->setData(0, kRoleSize, item->size);
                child->setData(0, kRoleCautious, !item->safeToDelete);
                total += item->size;
            }
            node->setText(1, formatSize(total));
        }
        m_tree->blockSignals(false);

        updateSummary();
        m_scanBtn->setEnabled(true);
        m_cleanBtn->setEnabled(!m_items.isEmpty());
        if (m_items.isEmpty())
            m_summary->setText(tr("扫描完成：未发现可清理项目"));
    });
}

void CleanPage::onItemChanged(QTreeWidgetItem* item, int column) {
    if (m_updating || column != 0) return;
    m_updating = true;
    if (item->parent() == nullptr) {          // 类别节点 → 同步子项
        const Qt::CheckState st = item->checkState(0);
        for (int i = 0; i < item->childCount(); ++i)
            item->child(i)->setCheckState(0, st == Qt::PartiallyChecked ? Qt::Checked : st);
    }
    m_updating = false;
    updateSummary();
}

void CleanPage::updateSummary() {
    qint64 total = 0;
    int count = 0;
    QList<QPair<QString, double>> slices;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* cat = m_tree->topLevelItem(i);
        qint64 catTotal = 0;
        int catCount = 0;
        for (int j = 0; j < cat->childCount(); ++j) {
            auto* c = cat->child(j);
            if (c->checkState(0) != Qt::Checked) continue;
            const qint64 sz = c->data(0, kRoleSize).toLongLong();
            total += sz; ++count;
            catTotal += sz; ++catCount;
        }
        if (cat->childCount() > 0)
            cat->setText(1, catCount > 0
                ? QString("%1 · %2 项").arg(formatSize(catTotal)).arg(catCount)
                : tr("已全不选"));
        if (catTotal > 0)
            slices.append({cat->text(0), double(catTotal)});
    }
    m_summary->setText(tr("已选 %1 项，共 %2").arg(count).arg(formatSize(total)));
    m_pie->setData(slices);
}

void CleanPage::doClean() {
    QList<CleanItem> selected;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* cat = m_tree->topLevelItem(i);
        const CleanCategory cc = CleanCategory(cat->data(0, kRoleCategory).toInt());
        for (int j = 0; j < cat->childCount(); ++j) {
            auto* c = cat->child(j);
            if (c->checkState(0) != Qt::Checked) continue;
            CleanItem it;
            it.category = cc;
            it.path = c->data(0, kRolePath).toString();
            it.size = c->data(0, kRoleSize).toLongLong();
            it.safeToDelete = !c->data(0, kRoleCautious).toBool();
            it.description = c->text(2);
            selected.append(it);
        }
    }
    if (selected.isEmpty()) { m_summary->setText(tr("请先选择要清理的项目")); return; }

    m_scanBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);
    m_summary->setText(tr("正在清理 %1 项……").arg(selected.size()));
    m_progress->setRange(0, 0);

    QtConcurrent::run([selected, recycle = m_recycleBin]() {
        CleanerService svc;
        return svc.clean(selected, recycle);
    }).then(this, [this](int n) {
        m_progress->setRange(0, 1);
        m_progress->setValue(1);
        m_summary->setText(tr("已清理 %1 项，正在重新扫描……").arg(n));
        doScan();
    });
}

} // namespace DiskOrganizer
