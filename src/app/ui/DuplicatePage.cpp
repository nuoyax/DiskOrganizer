#include "Icons.h"
#include "DuplicatePage.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
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

DuplicatePage::DuplicatePage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    // 指定盘符扫描（整卷走 MFT 快速路径，秒级）
    m_driveCombo = new SearchableComboBox;
    const QIcon driveIcon = Icons::tinted(QString::fromUtf8(Icons::P::drive), QColor(0x6C, 0x7A, 0x77), 18);
    for (const auto& d : enumerateDisks())
        m_driveCombo->addItem(driveIcon, QString("%1 (%2)").arg(d.driveLetter, d.volumeLabel.isEmpty()
            ? QStringLiteral("本地磁盘") : d.volumeLabel), d.driveLetter);
    if (m_driveCombo->count() > 0) m_driveCombo->setCurrentIndex(0);
    m_driveCombo->setFixedWidth(260);
    m_findBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")), tr("查找重复"), this);
    top->addWidget(new QLabel(tr("磁盘："), this));
    top->addWidget(m_driveCombo);
    top->addWidget(m_findBtn);
    layout->addLayout(top);

    m_progress = new QProgressBar(this);
    m_progress->hide();
    layout->addWidget(m_progress);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::MultiSelection);
    layout->addWidget(m_list);

    auto* bottom = new QHBoxLayout;
    m_keepBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::check), QColor(0x4B,0x41,0xE1)), tr("保留每组最早修改的"), this);
    m_keepBtn->setEnabled(false);
    m_deleteBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::trash), QColor("white")), tr("删除选中（回收站）"), this);
    m_deleteBtn->setEnabled(false);
    bottom->addWidget(m_keepBtn);
    bottom->addWidget(m_deleteBtn);
    bottom->addStretch();
    layout->addLayout(bottom);

    m_summary = new QLabel(tr("尚未扫描"), this);
    layout->addWidget(m_summary);

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
        m_summary->setText(tr("正在取消……"));
        return;
    }
    m_finding = true;
    m_cancelled.store(false);
    m_findBtn->setText(tr("取消"));
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
                }
            }
            auto* sep = new QListWidgetItem(m_list);
            sep->setFlags(Qt::NoItemFlags);
        });
        connect(finder, &DuplicateFinder::finished, this,
                [this](int groups, qint64 wasted) {
            m_finding = false;
            m_progress->hide();
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
