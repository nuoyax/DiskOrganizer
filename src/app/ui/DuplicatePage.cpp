#include "DuplicatePage.h"
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include "services/CleanerService.h"
#include "services/DuplicateFinder.h"
#include "services/ScannerService.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

DuplicatePage::DuplicatePage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    m_pathEdit = new QLineEdit(QDir::homePath(), this);
    auto* browse = new QPushButton(tr("浏览…"), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择目录"), m_pathEdit->text());
        if (!dir.isEmpty()) m_pathEdit->setText(dir);
    });
    m_findBtn = new QPushButton(tr("查找重复"), this);
    top->addWidget(new QLabel(tr("目录："), this));
    top->addWidget(m_pathEdit, 1);
    top->addWidget(browse);
    top->addWidget(m_findBtn);
    layout->addLayout(top);

    m_progress = new QProgressBar(this);
    m_progress->hide();
    layout->addWidget(m_progress);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::MultiSelection);
    layout->addWidget(m_list);

    auto* bottom = new QHBoxLayout;
    m_keepBtn = new QPushButton(tr("保留每组最早修改的"), this);
    m_keepBtn->setEnabled(false);
    m_deleteBtn = new QPushButton(tr("删除选中（回收站）"), this);
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
    const QString root = m_pathEdit->text();
    if (!QFileInfo::exists(root)) {
        QMessageBox::warning(this, tr("错误"), tr("目录不存在：%1").arg(root));
        return;
    }
    m_list->clear();
    m_progress->setRange(0, 0);
    m_progress->show();
    m_summary->setText(tr("正在扫描目录…"));
    m_findBtn->setEnabled(false);
    m_keepBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    // 先扫描文件列表，再三级比对
    auto* scanner = new ScannerService(this);
    auto* finder = new DuplicateFinder(this);
    QList<FileInfo>* files = new QList<FileInfo>;
    connect(scanner, &ScannerService::fileScanned, this, [files](const FileInfo& fi) {
        if (!fi.isDir) files->append(fi);
    });
    connect(scanner, &ScannerService::finished, this,
            [this, finder, files](qint64, qint64, qint64) {
        m_summary->setText(tr("共 %1 个文件，正在比对…").arg(files->size()));
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
            m_progress->hide();
            m_summary->setText(tr("发现 %1 组重复，浪费 %2").arg(groups)
                                   .arg(DiskOrganizer::formatSize(wasted)));
            m_findBtn->setEnabled(true);
            m_keepBtn->setEnabled(groups > 0);
            m_deleteBtn->setEnabled(groups > 0);
        });
        finder->find(*files, true);
    }, Qt::QueuedConnection);
    connect(scanner, &ScannerService::progress, this, [this](int percent, const QString& path) {
        m_progress->setValue(percent);
        m_summary->setText(tr("扫描中：%1").arg(path));
    });
    scanner->startScan({root});
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
