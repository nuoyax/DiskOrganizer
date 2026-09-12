#include "CleanPage.h"
#include "Charts.h"
#include "Icons.h"
#include "FlatStyle.h"
#include "util/SizeFormatter.h"
#include "SettingsDialog.h"
#include "services/Logger.h"
#include <QCheckBox>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
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

// 分类卡配色（参照 clean-light：teal/indigo/amber/rose/slate/purple 循环）
struct CardSkin { const char* icon; int bgR, bgG, bgB; int fgR, fgG, fgB; };
CardSkin cardSkin(int i) {
    static const CardSkin skins[] = {
        {Icons::P::trash,    0xF0, 0xFD, 0xFA, 0x0D, 0x94, 0x88}, // teal
        {Icons::P::broom,    0xEE, 0xF2, 0xFF, 0x4B, 0x41, 0xE1}, // indigo
        {Icons::P::drive,    0xFF, 0xFB, 0xEB, 0xB4, 0x53, 0x09}, // amber
        {Icons::P::trash,    0xFF, 0xF1, 0xF2, 0xE1, 0x1D, 0x48}, // rose
        {Icons::P::folder,   0xF8, 0xFA, 0xFC, 0x47, 0x55, 0x69}, // slate
        {Icons::P::disk,     0xFA, 0xF5, 0xFF, 0x93, 0x33, 0xEA}, // purple
    };
    return skins[i % 6];
}

// 小圆角彩色图标块
QLabel* iconChip(const char* iconPath, const CardSkin& skin, int size = 40) {
    auto* l = new QLabel;
    l->setPixmap(Icons::tinted(QString::fromUtf8(iconPath),
                               QColor(skin.fgR, skin.fgG, skin.fgB), size * 5 / 10).pixmap(size * 5 / 10, size * 5 / 10));
    l->setFixedSize(size, size);
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(QString("background:#%1%2%3;border-radius:%4px;")
                         .arg(skin.bgR, 2, 16, QChar('0'))
                         .arg(skin.bgG, 2, 16, QChar('0'))
                         .arg(skin.bgB, 2, 16, QChar('0'))
                         .arg(size * 3 / 10));
    return l;
}
} // namespace

CleanPage::CleanPage(QWidget* parent) : PageBase(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // ===== 页头：大标题 + 状态 pill + 副标题 =====
    auto* headRow = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto* title = new QLabel(tr("垃圾清理"));
    title->setStyleSheet("font-size:24px; font-weight:800; color:#181445; background:transparent;");
    m_headStatus = new QLabel(tr("尚未扫描"));
    m_headStatus->setStyleSheet(
        "QLabel{padding:3px 10px; border-radius:12px; background:#E6F2EF; color:#006B5F;"
        "font-size:12px; font-weight:600; background:transparent;}");
    // 上面的 background:transparent 会覆盖，改用双属性写法
    m_headStatus->setStyleSheet(
        "padding:3px 10px; border-radius:12px; background-color:#E6F2EF; color:#006B5F;"
        "font-size:12px; font-weight:600;");
    titleRow->addWidget(title);
    titleRow->addWidget(m_headStatus);
    titleRow->addStretch();
    auto* subtitle = new QLabel(tr("精准定位系统冗余文件、应用及网页缓存、日志与卸载残留，安全释放宝贵磁盘空间。"));
    subtitle->setStyleSheet("color:#6C7A77; background:transparent;");
    titleCol->addLayout(titleRow);
    titleCol->addWidget(subtitle);
    headRow->addLayout(titleCol, 1);

    auto* whitelistBtn = new QPushButton(tr("排除设置"));
    whitelistBtn->setProperty("class", "secondary");
    connect(whitelistBtn, &QPushButton::clicked, this, &CleanPage::openSettings);
    headRow->addWidget(whitelistBtn);
    root->addLayout(headRow);

    // ===== 上半区（页头下所有卡）放滚动区，保证明细树始终可见不挤压 =====
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea{background:transparent;} QWidget#cleanScrollBody{background:transparent;}");
    auto* body = new QWidget;
    body->setObjectName("cleanScrollBody");
    auto* topLayout = new QVBoxLayout(body);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(12);
    auto*& troot = topLayout; // 后续上半区布局一律写入 troot

    // ===== Hero 汇总卡 =====
    auto* hero = new QFrame;
    hero->setProperty("class", "card");
    auto* hv = new QVBoxLayout(hero);
    hv->setContentsMargins(20, 16, 20, 16);
    hv->setSpacing(10);
    auto* heroTop = new QHBoxLayout;
    heroTop->setSpacing(16);
    heroTop->addWidget(iconChip(Icons::P::broom, {Icons::P::broom, 0xE6, 0xF2, 0xEF, 0x00, 0x6B, 0x5F}, 56));
    auto* heroText = new QVBoxLayout;
    m_heroState = new QLabel(tr("尚未扫描，点击「开始扫描」定位可清理垃圾"));
    m_heroState->setStyleSheet("color:#6C7A77; background:transparent;");
    auto* numRow = new QHBoxLayout;
    numRow->setSpacing(6);
    m_heroTotal = new QLabel("0");
    m_heroTotal->setStyleSheet("font-size:32px; font-weight:800; color:#181445; background:transparent;");
    m_heroUnit = new QLabel(QStringLiteral("MB"));
    m_heroUnit->setStyleSheet("font-size:16px; font-weight:700; color:#006B5F; background:transparent;");
    auto* gbHint = new QLabel(tr("可安全释放"));
    gbHint->setStyleSheet("color:#6C7A77; background:transparent;");
    numRow->addWidget(m_heroTotal);
    numRow->addWidget(m_heroUnit);
    numRow->addWidget(gbHint);
    numRow->addStretch();
    heroText->addWidget(m_heroState);
    heroText->addLayout(numRow);
    heroTop->addLayout(heroText, 1);

    // 全选 pill + 回收站开关
    auto* selectAll = new QPushButton(tr("全选建议类别"));
    selectAll->setProperty("class", "secondary");
    connect(selectAll, &QPushButton::clicked, this, [this] {
        for (auto& c : m_cards) if (c.check) c.check->setChecked(true);
    });
    heroTop->addWidget(selectAll);
    hv->addLayout(heroTop);
    troot->addWidget(hero);

    // ===== 分类卡片 grid =====
    auto* catHeader = new QHBoxLayout;
    auto* catTitle = new QLabel(tr("清理项目分类推荐"));
    catTitle->setStyleSheet("font-size:16px; font-weight:700; color:#181445; background:transparent;");
    auto* catHint = new QLabel(tr("可根据需要单独取消某项勾选"));
    catHint->setStyleSheet("color:#6C7A77; background:transparent; font-size:12px;");
    catHeader->addWidget(catTitle);
    catHeader->addStretch();
    catHeader->addWidget(catHint);
    troot->addLayout(catHeader);

    auto* grid = new QGridLayout;
    grid->setSpacing(12);
    const char* names[12] = {
        "临时文件", "回收站", "浏览器缓存", "系统日志", "Windows 更新缓存",
        "缩略图缓存", "预读文件", "转储/错误报告", "安装程序缓存", "空文件夹",
        "零字节文件", "自定义规则"
    };
    const char* tags[12] = {
        "建议清理", "一键清空", "安全释放", "建议清理", "占用极大",
        "安全清理", "建议清理", "安全清理", "谨慎清理", "谨慎清理",
        "谨慎清理", "自定义"
    };
    const int nCats = int(CleanCategory::CustomRules) + 1;
    for (int i = 0; i < nCats; ++i) {
        auto& cc = m_cards[i];
        cc.card = new QFrame;
        cc.card->setProperty("class", "card");
        cc.card->setMinimumHeight(104); // 防止滚动区压缩时卡片底行被裁
        auto* cv = new QVBoxLayout(cc.card);
        cv->setContentsMargins(14, 12, 14, 12);
        cv->setSpacing(8);
        auto* top = new QHBoxLayout;
        top->setSpacing(10);
        top->addWidget(iconChip(cardSkin(i).icon, cardSkin(i)));
        auto* textCol = new QVBoxLayout;
        auto* name = new QLabel(QString::fromUtf8(names[i]));
        name->setStyleSheet("font-weight:700; color:#181445; background:transparent;");
        cc.countLabel = new QLabel(tr("未扫描"));
        cc.countLabel->setStyleSheet("color:#6C7A77; font-size:11px; background:transparent;");
        textCol->addWidget(name);
        textCol->addWidget(cc.countLabel);
        top->addLayout(textCol, 1);
        cc.check = new QCheckBox;
        cc.check->setChecked(i < 7);
        connect(cc.check, &QCheckBox::toggled, this, [this] { updateSummary(); });
        top->addWidget(cc.check);
        cv->addLayout(top);

        auto* bottom = new QHBoxLayout;
        cc.tagLabel = new QLabel(QString::fromUtf8(tags[i]));
        cc.tagLabel->setStyleSheet(
            "padding:2px 8px; border-radius:8px; background-color:#F0FDFA; color:#0F766E;"
            "font-size:11px; font-weight:600;");
        cc.sizeLabel = new QLabel("--");
        cc.sizeLabel->setStyleSheet("font-size:18px; font-weight:800; color:#181445; background:transparent;");
        bottom->addWidget(cc.tagLabel);
        bottom->addStretch();
        bottom->addWidget(cc.sizeLabel);
        cv->addLayout(bottom);
        grid->addWidget(cc.card, i / 3, i % 3);
    }
    troot->addLayout(grid);

    topLayout->addStretch();
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    // ===== 明细树卡 =====
    auto* treeCard = new QFrame;
    treeCard->setProperty("class", "card");
    auto* tv = new QVBoxLayout(treeCard);
    tv->setContentsMargins(8, 8, 8, 8);
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({tr("文件 / 类别"), tr("大小"), tr("说明")});
    m_tree->setColumnWidth(0, 420);
    m_tree->setColumnWidth(1, 110);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    connect(m_tree, &QTreeWidget::itemChanged, this, &CleanPage::onItemChanged);
    tv->addWidget(m_tree);
    root->addWidget(treeCard, 1);

    // ===== 底部操作条 =====
    m_progress = new QProgressBar;
    m_progress->setFixedHeight(10);
    m_progress->setTextVisible(false);

    auto* btnRow = new QHBoxLayout;
    m_summary = new QLabel(tr("尚未扫描"));
    m_summary->setStyleSheet("color:#6C7A77; background:transparent;");
    m_scanBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")), tr("重新扫描"));
    m_cleanBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::broom), QColor("white")), tr("一键立即清理"));
    m_cleanBtn->setEnabled(false);
    auto* recycle = new QCheckBox(tr("删除到回收站"));
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope, "DiskOrganizer", "DiskOrganizer");
        m_recycleBin = s.value("clean/toRecycleBin", true).toBool();
    }
    recycle->setChecked(m_recycleBin);
    m_recycleCheck = recycle;
    connect(recycle, &QCheckBox::toggled, this, [this](bool on) { m_recycleBin = on; });

    btnRow->addWidget(m_summary, 1);
    btnRow->addWidget(recycle);
    btnRow->addWidget(m_scanBtn);
    btnRow->addWidget(m_cleanBtn);
    root->addWidget(m_progress);
    root->addLayout(btnRow);

    connect(m_scanBtn, &QPushButton::clicked, this, &CleanPage::doScan);
    connect(m_cleanBtn, &QPushButton::clicked, this, &CleanPage::doClean);
}

void CleanPage::openSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted && m_recycleCheck) {
        QSettings s(QSettings::IniFormat, QSettings::UserScope, "DiskOrganizer", "DiskOrganizer");
        m_recycleBin = s.value("clean/toRecycleBin", true).toBool();
        m_recycleCheck->setChecked(m_recycleBin);
    }
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

void CleanPage::rebuildCategoryCards() {
    // 扫描结果同步到分类卡（数量 + 大小）
    QMap<int, QPair<int, qint64>> byCat; // cat → (count, bytes)
    for (const auto& it : m_items) {
        auto& agg = byCat[int(it.category)];
        agg.first += 1;
        agg.second += it.size;
    }
    const int nCats = int(CleanCategory::CustomRules) + 1;
    for (int i = 0; i < nCats; ++i) {
        auto& cc = m_cards[i];
        if (!cc.card) continue;
        if (byCat.contains(i)) {
            cc.countLabel->setText(tr("%1 项").arg(byCat[i].first));
            cc.sizeLabel->setText(formatSize(byCat[i].second));
            const bool cautious = !byCat.contains(i) || i >= 8; // 尾部类别标记谨慎
            Q_UNUSED(cautious)
        } else {
            cc.countLabel->setText(tr("无项目"));
            cc.sizeLabel->setText("0");
        }
    }
}

void CleanPage::doScan() {
    m_scanBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);
    m_tree->clear();
    m_items.clear();
    m_headStatus->setText(tr("正在扫描……"));
    m_heroState->setText(tr("正在扫描……"));
    m_summary->setText(tr("正在扫描……"));
    m_progress->setRange(0, 0);

    QList<CleanCategory> cats;
    for (int i = 0; i <= int(CleanCategory::CustomRules); ++i)
        if (m_cards[i].check && m_cards[i].check->isChecked())
            cats.append(CleanCategory(i));

    if (cats.isEmpty()) {
        m_summary->setText(tr("请至少勾选一个清理类别"));
        m_scanBtn->setEnabled(true);
        m_progress->setRange(0, 1);
        return;
    }

    (void)QtConcurrent::run([this, cats]() {
        CleanerService svc;
        const QList<CleanItem> found = svc.findCleanableItems(cats);
        QMetaObject::invokeMethod(this, [this, found]() {
            m_progress->setRange(0, 1);
            m_progress->setValue(1);
            m_items = found;
            rebuildCategoryCards();

            m_tree->blockSignals(true);
            m_tree->clear();
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
                    const QString name = item->path.startsWith(QStringLiteral("RecycleBin://"))
                        ? tr("回收站内容")
                        : item->path;
                    auto* child = new QTreeWidgetItem(node,
                        {name, formatSize(item->size),
                         item->safeToDelete ? tr("[安全]") : tr("[谨慎] ") + item->description});
                    child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                    child->setCheckState(0, Qt::Checked);
                    child->setData(0, kRolePath, item->path);
                    child->setData(0, kRoleSize, item->size);
                    child->setData(0, kRoleCautious, !item->safeToDelete);
                    child->setToolTip(0, item->path);
                    total += item->size;
                }
                node->setText(1, formatSize(total));
            }
            m_tree->blockSignals(false);
            m_tree->collapseAll();

            m_headStatus->setText(tr("智能扫描已完成"));
            m_heroState->setText(tr("扫描完成 · 发现 %1 个可清理项").arg(m_items.size()));
            updateSummary();
            m_scanBtn->setEnabled(true);
            m_cleanBtn->setEnabled(!m_items.isEmpty());
            if (m_items.isEmpty())
                m_summary->setText(tr("扫描完成：未发现可清理项目"));
            LOG << "CleanPage UI items=" << m_items.size();
        }, Qt::QueuedConnection);
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
    }
    // hero 大数字 + 单位（与底部 formatSize 一致，避免 GB/MB 错配）
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    if (total >= qint64(gb)) {
        m_heroTotal->setText(QString::number(total / gb, 'f', 2));
        m_heroUnit->setText(QStringLiteral("GB"));
    } else if (total >= qint64(mb)) {
        m_heroTotal->setText(QString::number(total / mb, 'f', 1));
        m_heroUnit->setText(QStringLiteral("MB"));
    } else if (total >= qint64(kb)) {
        m_heroTotal->setText(QString::number(total / kb, 'f', 1));
        m_heroUnit->setText(QStringLiteral("KB"));
    } else {
        m_heroTotal->setText(QString::number(total));
        m_heroUnit->setText(QStringLiteral("B"));
    }
    m_summary->setText(tr("已勾选 %1 项 · 预计释放 %2").arg(count).arg(formatSize(total)));
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
            // 用户已勾选即确认删除（含「谨慎」项）
            it.safeToDelete = true;
            it.description = c->text(2);
            selected.append(it);
        }
    }
    if (selected.isEmpty()) {
        m_summary->setText(tr("请先选择要清理的项目"));
        return;
    }

    m_scanBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);
    m_summary->setText(tr("正在清理 %1 项……").arg(selected.size()));
    m_progress->setRange(0, 0);

    (void)QtConcurrent::run([this, selected, recycle = m_recycleBin]() {
        CleanerService svc;
        QObject::connect(&svc, &CleanerService::progress, this,
            [this](int percent, const QString& path) {
                QMetaObject::invokeMethod(this, [this, percent, path]() {
                    m_progress->setRange(0, 100);
                    m_progress->setValue(percent);
                    m_summary->setText(path);
                }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
        const qint64 freed = svc.clean(selected, recycle);
        QMetaObject::invokeMethod(this, [this, freed, n = selected.size()]() {
            m_progress->setRange(0, 1);
            m_progress->setValue(1);
            m_summary->setText(tr("已释放 %1（目标 %2 项），正在重新扫描……")
                                   .arg(formatSize(freed)).arg(n));
            LOG << "CleanPage clean freed=" << freed << " requested=" << n;
            doScan();
        }, Qt::QueuedConnection);
    });
}

} // namespace DiskOrganizer
