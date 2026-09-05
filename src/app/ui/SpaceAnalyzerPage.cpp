#include "Icons.h"
#include "SpaceAnalyzerPage.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "services/ScannerService.h"
#include "services/SpaceAnalyzer.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

SpaceAnalyzerPage::SpaceAnalyzerPage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    m_pathEdit = new QLineEdit(QDir::rootPath(), this);
    auto* browse = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::folder), QColor("white")), tr("浏览…"), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("选择目录"), m_pathEdit->text());
        if (!dir.isEmpty()) { m_pathEdit->setText(dir); scanPath(dir); }
    });
    m_scanBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")), tr("开始扫描"), this);
    connect(m_scanBtn, &QPushButton::clicked, this, [this] { scanPath(m_pathEdit->text()); });
    top->addWidget(new QLabel(tr("路径："), this));
    top->addWidget(m_pathEdit, 1);
    top->addWidget(browse);
    top->addWidget(m_scanBtn);
    layout->addLayout(top);

    m_progress = new QProgressBar(this);
    m_progress->hide();
    layout->addWidget(m_progress);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_dirTree = new QTreeWidget(splitter);
    m_dirTree->setHeaderLabels({tr("目录"), tr("大小")});
    m_dirTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_dirTree->setSortingEnabled(true);
    m_dirTree->sortByColumn(1, Qt::DescendingOrder);

    m_typeTree = new QTreeWidget(splitter);
    m_typeTree->setHeaderLabels({tr("扩展名"), tr("大小"), tr("文件数")});
    m_typeTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_typeTree->setSortingEnabled(true);
    m_typeTree->sortByColumn(1, Qt::DescendingOrder);
    splitter->addWidget(m_dirTree);
    splitter->addWidget(m_typeTree);
    splitter->setSizes({600, 400});
    layout->addWidget(splitter, 1);

    m_summary = new QLabel(tr("尚未扫描"), this);
    layout->addWidget(m_summary);
}

void SpaceAnalyzerPage::scanPath(const QString& path) {
    if (!QFileInfo::exists(path)) {
        m_summary->setText(tr("路径不存在：%1").arg(path));
        return;
    }
    m_dirTree->clear();
    m_typeTree->clear();
    m_progress->setRange(0, 0);
    m_progress->show();
    m_summary->setText(tr("正在扫描 %1 …").arg(path));
    m_scanBtn->setEnabled(false);

    auto* scanner = new ScannerService(this);
    QList<FileInfo>* files = new QList<FileInfo>;
    connect(scanner, &ScannerService::fileScanned, this, [files](const FileInfo& fi) {
        files->append(fi);
    });
    connect(scanner, &ScannerService::finished, this, [this, files, path](qint64 n, qint64, qint64 ms) {
        // 聚合计算也放后台，避免大列表卡 UI
        (void)QtConcurrent::run([this, files, path, n, ms]() {
            SpaceAnalyzer analyzer;
            const auto dirSizes = analyzer.directorySizes(*files);
            const auto typeStats = analyzer.typeDistribution(*files);
            QMetaObject::invokeMethod(this, [this, dirSizes, typeStats, path, n, ms]() {
                m_dirTree->clear();
                int shown = 0;
                for (const auto& [dir, size] : dirSizes) {
                    if (++shown > 200) break;   // 只展示 Top 200
                    auto* it = new QTreeWidgetItem(m_dirTree, {dir, DiskOrganizer::formatSize(size)});
                    it->setData(0, Qt::UserRole, dir);
                }
                m_typeTree->clear();
                for (const auto& ts : typeStats) {
                    if (ts.extension.isEmpty()) continue;
                    new QTreeWidgetItem(m_typeTree,
                        {ts.extension, DiskOrganizer::formatSize(ts.totalBytes),
                         QString::number(ts.count)});
                }
                m_progress->hide();
                m_summary->setText(tr("%1：%2 个文件/目录，耗时 %3")
                                       .arg(path).arg(n)
                                       .arg(DiskOrganizer::formatDuration(ms)));
                m_scanBtn->setEnabled(true);
            }, Qt::QueuedConnection);
        });
    }, Qt::QueuedConnection);
    connect(scanner, &ScannerService::progress, this, [this](int percent, const QString& cur) {
        m_progress->setRange(0, 100);
        m_progress->setValue(percent);
        m_summary->setText(tr("扫描中：%1").arg(cur));
    });
    scanner->startScan({path});
}

} // namespace DiskOrganizer
