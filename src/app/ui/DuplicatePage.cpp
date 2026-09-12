#include "Icons.h"
#include "DuplicatePage.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
#include <QtConcurrent>
#include "services/CleanerService.h"
#include "services/DuplicateFinder.h"
#include "services/ScannerService.h"
#include "SearchableComboBox.h"
#include "util/FileSystemUtil.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

namespace {
// 白底指标小卡（发现重复组 / 可释放空间）
QFrame* metricCard(const char* iconPath, const QColor& iconTint, const QColor& iconBg,
                   const QString& title, QLabel** valueOut, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setProperty("class", "card");
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(10);
    auto* chip = new QLabel(card);
    const int px = 18;
    chip->setPixmap(Icons::tinted(QString::fromUtf8(iconPath), iconTint, px).pixmap(px, px));
    chip->setFixedSize(34, 34);
    chip->setAlignment(Qt::AlignCenter);
    chip->setStyleSheet(QString("background:%1; border-radius:8px;").arg(iconBg.name()));
    auto* col = new QVBoxLayout;
    col->setSpacing(0);
    auto* cap = new QLabel(title, card);
    cap->setStyleSheet("font-size:11px; color:#6C7A77; background:transparent;");
    auto* val = new QLabel("--", card);
    val->setStyleSheet("font-size:15px; font-weight:800; color:#181445; background:transparent;");
    col->addWidget(cap);
    col->addWidget(val);
    h->addWidget(chip);
    h->addLayout(col, 1);
    *valueOut = val;
    return card;
}
} // namespace

DuplicatePage::DuplicatePage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(12);

    // ===== 页头：大标题 + 状态 pill + 副标题 + 指标卡 =====
    auto* head = new QHBoxLayout;
    auto* headCol = new QVBoxLayout;
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto* title = new QLabel(tr("重复文件"), this);
    title->setStyleSheet("font-size:24px; font-weight:800; color:#181445; background:transparent;");
    m_headStatus = new QLabel(tr("哈希匹配引擎已就绪"), this);
    m_headStatus->setStyleSheet(
        "padding:3px 10px; border-radius:12px; background-color:#ECFDF5; color:#047857;"
        "font-size:12px; font-weight:600;");
    titleRow->addWidget(title);
    titleRow->addWidget(m_headStatus);
    titleRow->addStretch();
    auto* subtitle = new QLabel(
        tr("自动识别二进制完全相同的文件副本，默认为您安全保留每组最早创建的原作版本。"), this);
    subtitle->setStyleSheet("color:#6C7A77; background:transparent;");
    headCol->addLayout(titleRow);
    headCol->addWidget(subtitle);
    head->addLayout(headCol, 1);

    head->addWidget(metricCard(Icons::P::duplicate, QColor(0x4B, 0x41, 0xE1), QColor(0xEE, 0xF2, 0xFF),
                               tr("发现重复组"), &m_groupsMetric, this));
    head->addWidget(metricCard(Icons::P::trash, QColor(0x0D, 0x94, 0x88), QColor(0xF0, 0xFD, 0xFA),
                               tr("可释放空间"), &m_wastedMetric, this));
    layout->addLayout(head);

    // ===== 过滤行：盘符 + 查找按钮 =====
    auto* filterRow = new QHBoxLayout;
    auto* driveLabel = new QLabel(tr("目标磁盘:"), this);
    driveLabel->setStyleSheet("color:#6C7A77; font-size:12px; background:transparent;");
    m_driveCombo = new SearchableComboBox;
    const QIcon driveIcon = Icons::tinted(QString::fromUtf8(Icons::P::drive), QColor(0x4B, 0x41, 0xE1), 18);
    for (const auto& d : enumerateDisks())
        m_driveCombo->addItem(driveIcon, QString("%1 (%2)").arg(d.driveLetter, d.volumeLabel.isEmpty()
            ? QStringLiteral("本地磁盘") : d.volumeLabel), d.driveLetter);
    if (m_driveCombo->count() > 0) m_driveCombo->setCurrentIndex(0);
    m_driveCombo->setFixedWidth(260);
    m_findBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")), tr("查找重复"), this);
    filterRow->addWidget(driveLabel);
    filterRow->addWidget(m_driveCombo);
    filterRow->addStretch();
    filterRow->addWidget(m_findBtn);
    layout->addLayout(filterRow);

    m_progress = new QProgressBar(this);
    m_progress->hide();
    layout->addWidget(m_progress);

    // ===== 结果列表（组卡片式列表）=====
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::MultiSelection);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list, 1);

    // ===== 底部操作条 =====
    auto* bottom = new QHBoxLayout;
    m_summary = new QLabel(tr("尚未扫描"), this);
    m_summary->setStyleSheet("color:#6C7A77; background:transparent;");
    m_keepBtn = new QPushButton(tr("智能勾选（保留最早）"), this);
    m_keepBtn->setProperty("class", "secondary");
    m_keepBtn->setEnabled(false);
    m_deleteBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::trash), QColor("white")), tr("删除选中（回收站）"), this);
    m_deleteBtn->setEnabled(false);
    bottom->addWidget(m_summary, 1);
    bottom->addWidget(m_keepBtn);
    bottom->addWidget(m_deleteBtn);
    layout->addLayout(bottom);

    connect(m_findBtn, &QPushButton::clicked, this, &DuplicatePage::doFind);
    connect(m_keepBtn, &QPushButton::clicked, this, &DuplicatePage::keepOldest);
    connect(m_deleteBtn, &QPushButton::clicked, this, &DuplicatePage::deleteSelected);
}

void DuplicatePage::doFind() {
    const QString drive = m_driveCombo->currentData().toString();
    if (drive.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请选择要扫描的磁盘"));
        return;
    }
    const QString root = drive + "/";
    // 扫描中再点 = 取消
    if (m_finding) {
        m_cancelled.store(true);
        m_headStatus->setText(tr("正在取消……"));
        m_summary->setText(tr("正在取消……"));
        return;
    }
    m_finding = true;
    m_cancelled.store(false);
    m_findBtn->setText(tr("取消"));
    m_headStatus->setText(tr("SHA-256 哈希匹配中……"));
    m_list->clear();
    m_progress->setRange(0, 0);
    m_progress->show();
    m_summary->setText(tr("正在扫描磁盘……"));
    m_keepBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    // 后台：scanBlocking 整卷扫描（NTFS 走 MFT，秒级），完成后三级比对
    (void)QtConcurrent::run([this, root]() {
        ScannerService scanner;
        auto cancelled = [this]() { return m_cancelled.load(); };
        std::function<bool(qint64, const QString&)> onProgress =
            [this, cancelled](qint64 n, const QString& path) -> bool {
            if (cancelled()) return false;
            QMetaObject::invokeMethod(this, [this, n, path]() {
                m_summary->setText(tr("扫描中：%1").arg(path));
            }, Qt::QueuedConnection);
            return true;
        };
        QList<FileInfo> files = scanner.scanBlocking({root}, onProgress, 0, 0, cancelled);
        if (cancelled()) {
            QMetaObject::invokeMethod(this, [this]() { resetUiAfterCancel(); }, Qt::QueuedConnection);
            return;
        }

        auto* finder = new DuplicateFinder(this);
        connect(finder, &DuplicateFinder::groupFound, this,
                [this](const DuplicateGroup& g) {
            const qint64 firstModified = g.files.first().lastModified;
            for (const auto& f : g.files) {
                auto* wi = new QListWidgetItem(
                    QString("%1  %2  [%3]").arg(f.absolutePath,
                        DiskOrganizer::formatSize(f.size),
                        QDateTime::fromMSecsSinceEpoch(f.lastModified).toString("yyyy-MM-dd")),
                    m_list);
                // 组内最早的标记为"建议保留"
                wi->setData(Qt::UserRole, f.absolutePath);
                wi->setData(Qt::UserRole + 1, f.lastModified == firstModified);
                if (f.lastModified == firstModified) {
                    wi->setText(wi->text() + tr("  ←建议保留"));
                    wi->setForeground(QColor(0x04, 0x78, 0x57));
                } else {
                    wi->setForeground(QColor(0xBA, 0x1A, 0x1A));
                }
            }
            auto* sep = new QListWidgetItem(m_list);
            sep->setFlags(Qt::NoItemFlags);
        });
        connect(finder, &DuplicateFinder::finished, this,
                [this](int groups, qint64 wasted) {
            m_finding = false;
            m_progress->hide();
            m_groupsMetric->setText(tr("%1 组").arg(groups));
            m_wastedMetric->setText(DiskOrganizer::formatSize(wasted));
            m_headStatus->setText(tr("哈希匹配完成"));
            m_summary->setText(tr("发现 %1 组重复，浪费 %2").arg(groups)
                                   .arg(DiskOrganizer::formatSize(wasted)));
            m_findBtn->setText(tr("查找重复"));
            m_keepBtn->setEnabled(groups > 0);
            m_deleteBtn->setEnabled(groups > 0);
        }, Qt::QueuedConnection);
        connect(finder, &DuplicateFinder::finished, finder, &QObject::deleteLater,
                Qt::QueuedConnection);
        finder->find(files, true);
    });
}

void DuplicatePage::resetUiAfterCancel() {
    m_finding = false;
    m_progress->hide();
    m_headStatus->setText(tr("哈希匹配引擎已就绪"));
    m_summary->setText(tr("扫描已取消"));
    m_findBtn->setText(tr("查找重复"));
}

void DuplicatePage::keepOldest() {
    // 按组分隔符分段，每组选中除"建议保留"外的全部
    QList<QListWidgetItem*> group;
    for (int i = 0; i < m_list->count(); ++i) {
        auto* it = m_list->item(i);
        if (!(it->flags() & Qt::ItemIsSelectable)) {
            // 组结束：选中组内非保留项
            for (auto* wi : std::as_const(group)) {
                if (!wi->data(Qt::UserRole + 1).toBool())
                    wi->setSelected(true);
            }
            group.clear();
        } else {
            group.append(it);
        }
    }
    for (auto* wi : std::as_const(group)) {
        if (!wi->data(Qt::UserRole + 1).toBool())
            wi->setSelected(true);
    }
}

void DuplicatePage::deleteSelected() {
    const auto selected = m_list->selectedItems();
    if (selected.isEmpty()) return;
    if (QMessageBox::question(this, tr("确认删除"),
            tr("将 %1 个文件移入回收站，是否继续？").arg(selected.size())) != QMessageBox::Yes)
        return;
    CleanerService cleaner; // 复用 IFileOperation 回收站删除
    QList<CleanItem> items;
    qint64 total = 0;
    for (auto* wi : selected) {
        CleanItem ci;
        ci.path = wi->data(Qt::UserRole).toString();
        ci.size = QFileInfo(ci.path).size();
        ci.safeToDelete = true;
        items.append(ci);
        total += ci.size;
    }
    const qint64 freed = cleaner.clean(items, true);
    m_summary->setText(tr("已删除 %1，释放 %2").arg(selected.size())
                           .arg(DiskOrganizer::formatSize(freed)));
    for (auto* wi : selected) delete wi;
}

} // namespace DiskOrganizer
