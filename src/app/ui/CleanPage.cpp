#include "CleanPage.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "services/CleanerService.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

CleanPage::CleanPage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    m_scanBtn = new QPushButton(tr("扫描垃圾"), this);
    m_cleanBtn = new QPushButton(tr("清理选中项"), this);
    m_cleanBtn->setEnabled(false);
    auto* recycleCheck = new QCheckBox(tr("删除到回收站"), this);
    recycleCheck->setChecked(true);
    connect(recycleCheck, &QCheckBox::toggled, this, [this](bool on) { m_recycleBin = on; });
    top->addWidget(m_scanBtn);
    top->addWidget(m_cleanBtn);
    top->addWidget(recycleCheck);
    top->addStretch();
    layout->addLayout(top);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->hide();
    layout->addWidget(m_progress);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::MultiSelection);
    layout->addWidget(m_list);

    m_summary = new QLabel(tr("尚未扫描"), this);
    layout->addWidget(m_summary);

    connect(m_cleanBtn, &QPushButton::clicked, this, &CleanPage::doClean);
    connect(m_scanBtn, &QPushButton::clicked, this, &CleanPage::doScan);
}

void CleanPage::openSettings() {
    extern void showSettingsDialog(QWidget* parent);
    showSettingsDialog(this);
}

void CleanPage::doScan() {
    m_list->clear();
    m_progress->show();
    m_summary->setText(tr("扫描中…"));
    m_scanBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);

    (void)QtConcurrent::run([this]() {
        CleanerService cleaner;
        const QList<CleanCategory> cats = {
            CleanCategory::TempFiles, CleanCategory::RecycleBin,
            CleanCategory::BrowserCache, CleanCategory::SystemLogs,
            CleanCategory::WindowsUpdate, CleanCategory::ThumbnailCache,
            CleanCategory::DumpFiles,
        };
        const QList<CleanItem> items = cleaner.findCleanableItems(cats);
        QMetaObject::invokeMethod(this, [this, items]() {
            m_progress->hide();
            qint64 total = 0;
            for (const auto& item : items) {
                auto* wi = new QListWidgetItem(
                    QString("[%1] %2 — %3").arg(item.safeToDelete ? tr("安全") : tr("谨慎"))
                        .arg(item.path, DiskOrganizer::formatSize(item.size)),
                    m_list);
                wi->setData(Qt::UserRole, item.size);
                wi->setSelected(item.safeToDelete);
                total += item.size;
            }
            m_summary->setText(tr("发现 %1 项，可释放 %2").arg(items.size())
                                   .arg(DiskOrganizer::formatSize(total)));
            m_scanBtn->setEnabled(true);
            m_cleanBtn->setEnabled(!items.isEmpty());
        }, Qt::QueuedConnection);
    });
}

void CleanPage::doClean() {
    const auto selected = m_list->selectedItems();
    if (selected.isEmpty()) return;
    qint64 total = 0;
    QList<CleanItem> items;
    for (auto* wi : selected) {
        CleanItem item;
        const QString text = wi->text();
        const int start = text.indexOf(QLatin1Char(']')) + 2;
        const int end = text.lastIndexOf(QStringLiteral(" — "));
        item.path = text.mid(start, end - start);
        item.size = wi->data(Qt::UserRole).toLongLong();
        item.safeToDelete = text.startsWith(QLatin1Char('[') + tr("安全"));
        items.append(item);
        total += item.size;
    }
    if (QMessageBox::question(this, tr("确认清理"),
            tr("即将清理 %1 项（共 %2）%3，是否继续？")
                .arg(items.size()).arg(DiskOrganizer::formatSize(total))
                .arg(m_recycleBin ? tr("（进回收站）") : tr("（永久删除，不可恢复！）")))
        != QMessageBox::Yes) return;

    CleanerService cleaner;
    const qint64 freed = cleaner.clean(items, m_recycleBin);
    m_summary->setText(tr("已释放 %1").arg(DiskOrganizer::formatSize(freed)));
    doScan();
}

void CleanPage::onScanProgress(int percent, const QString& currentPath) {
    m_progress->setValue(percent);
    m_summary->setText(tr("扫描中：%1").arg(currentPath));
}

} // namespace DiskOrganizer
