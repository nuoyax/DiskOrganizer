#include "BigFilePage.h"
#include "Icons.h"
#include "SearchableComboBox.h"
#include "FlatStyle.h"
#include "util/FileSystemUtil.h"
#include "util/SizeFormatter.h"
#include "services/ScannerService.h"
#include "services/BigFileFinder.h"
#include "services/Logger.h"
#include "services/CleanerService.h"
#include "Charts.h"

#include <QMessageBox>
#include <QItemSelectionModel>
#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QSignalBlocker>
#include <QDateTime>
#include <QDir>
#include <QTimer>
#include <functional>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStorageInfo>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace DiskOrganizer {

namespace {
constexpr int kColCheck = 0;
constexpr int kColDrive = 1;
constexpr int kColSize  = 2;
constexpr int kColName  = 3;
constexpr int kColPath  = 4;
constexpr int kColMtime = 5;

// 展示友好大小，排序按字节
class SizeTableItem : public QTableWidgetItem {
public:
    explicit SizeTableItem(qint64 bytes)
        : QTableWidgetItem(formatSize(bytes)) {
        setData(Qt::UserRole, bytes);
        setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
    }
};
}

BigFilePage::BigFilePage(QWidget* parent) : PageBase(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("扫描磁盘上的大文件与旧文件，结果按磁盘分类，点击表头自由排序"));
    title->setStyleSheet("color:#6C7A77; background:transparent;");
    root->addWidget(title);

    // 过滤条件行
    auto* filterRow = new QHBoxLayout;
    m_driveCombo = new SearchableComboBox;
    const auto drives = enumerateDisks();
    m_driveCombo->addItem(tr("全部磁盘"), QString());
    // 磁盘项统一用硬盘图标（灰蓝色），与"全部磁盘"的 disk 图标呼应
    const QIcon driveIcon = Icons::tinted(QString::fromUtf8(Icons::P::drive), QColor(0x6C, 0x7A, 0x77), 18);
    const QIcon allIcon = Icons::tinted(QString::fromUtf8(Icons::P::disk), QColor(0x4B, 0x41, 0xE1), 18);
    m_driveCombo->addItem(allIcon, tr("全部磁盘"), QString());
    for (const auto& d : drives)
        m_driveCombo->addItem(driveIcon, QString("%1 (%2)").arg(d.driveLetter, d.volumeLabel.isEmpty()
            ? QStringLiteral("本地磁盘") : d.volumeLabel), d.driveLetter);
    m_driveCombo->setFixedWidth(260);
    // 目录级扫描：可输入具体目录（如 C:/Users），留空 = 整盘扫描
    m_dirEdit = new QLineEdit;
    m_dirEdit->setPlaceholderText(tr("目录（可选，如 C:/Users，留空=整盘）"));
    m_dirEdit->setFixedWidth(260);
    m_driveCombo->setFixedWidth(260);
    // 大小阈值：预设下拉（可搜索复用），单位 MB；起步 100MB（大文件定位场景）
    m_sizeCombo = new SearchableComboBox;
    m_sizeCombo->setFixedWidth(150);
    m_sizeCombo->setToolTip(tr("只显示大于该大小的文件"));
    m_sizeCombo->addItem("100 MB", 100);
    m_sizeCombo->addItem("500 MB", 500);
    m_sizeCombo->addItem("1 GB", 1024);
    m_sizeCombo->addItem("5 GB", 5 * 1024);
    m_sizeCombo->addItem("10 GB", 10 * 1024);
    m_sizeCombo->setCurrentIndex(0);   // 默认 100MB

    // 扩展名可搜索下拉：覆盖常见所有类型 + 可输入子串过滤
    m_extCombo = new SearchableComboBox;
    m_extCombo->setFixedWidth(260);
    m_extCombo->addItem(tr("全部类型"), QString());
    const struct { const char* name; const char* exts; } extGroups[] = {
        {"压缩包",  ".zip .7z .rar .tar .gz .bz2 .xz .iso .cab .tgz"},
        {"视频",    ".mp4 .mkv .avi .mov .wmv .flv .webm .m4v .mpg .rmvb .ts"},
        {"音频",    ".mp3 .wav .flac .aac .ogg .wma .m4a .ape .mid"},
        {"图片",    ".jpg .jpeg .png .gif .bmp .webp .svg .tif .tiff .raw .ico .heic"},
        {"文档",    ".pdf .doc .docx .xls .xlsx .ppt .pptx .txt .md .csv .odt"},
        {"程序/库", ".exe .dll .lib .so .apk .msi .bin .sys .ocx .jar"},
        {"代码",    ".cpp .h .hpp .c .cs .py .java .js .ts .html .css .json .xml .sql .go .rs"},
        {"开发环境",".pdb .idb .obj .o .a .exp .wim .vhd .vhdx"},
        {"光盘镜像",".iso .img .vhd .vhdx .wim .gho .mds"},
        {"数据库",  ".db .sqlite .mdb .mdf .ldf .bak"},
        {"虚拟机",  ".vmdk .vdi .qcow2 .ova .ovf .vmx"},
        {"其他",    ".dat .log .tmp .cache .dmp .etl .evtx"},
    };
    const char* groupIcons[] = {
        Icons::P::duplicate,   // 压缩包
        Icons::P::rocket,      // 视频 -> 播放含义近似
        Icons::P::pie,         // 音频
        Icons::P::bigfile,     // 图片
        Icons::P::file,        // 文档
        Icons::P::chip,        // 程序/库
        Icons::P::code,        // 代码
        Icons::P::terminal,    // 开发环境
        Icons::P::disk,        // 光盘镜像
        Icons::P::drive,       // 数据库
        Icons::P::duplicate,   // 虚拟机
        Icons::P::folder,      // 其他
    };
    for (int gi = 0; gi < 12; ++gi) {
        QIcon ic = Icons::tinted(QString::fromUtf8(groupIcons[gi]), QColor(0x6C, 0x7A, 0x77), 18);
        // 显示名带后缀示例：文档 (pdf, doc, txt…)；过滤 data 不变
        const QString extsStr = QString::fromUtf8(extGroups[gi].exts);
        QStringList sample;
        const QStringList all = extsStr.split(' ', Qt::SkipEmptyParts);
        for (int e = 0; e < qMin(3, all.size()); ++e)
            sample.append(all[e].mid(1));   // 去掉点
        const QString label = QString::fromUtf8(extGroups[gi].name)
            + QStringLiteral(" (%1…)").arg(sample.join(", "));
        m_extCombo->addItem(ic, label, extsStr);
    }

    m_groupByDrive = new QCheckBox(tr("按磁盘分组显示"));
    m_groupByDrive->setChecked(true);

    filterRow->addWidget(new QLabel(tr("磁盘:")));
    filterRow->addWidget(m_driveCombo);
    filterRow->addWidget(m_dirEdit);
    auto* sizeLabel = new QLabel(tr("文件大小 >"));
    sizeLabel->setToolTip(tr("只显示大于该大小的文件"));
    filterRow->addWidget(sizeLabel);
    filterRow->addWidget(m_sizeCombo);
    filterRow->addWidget(new QLabel(tr("类型:")));
    filterRow->addWidget(m_extCombo);
    filterRow->addWidget(m_groupByDrive);
    filterRow->addStretch();

    m_scanBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")), tr("开始扫描"));
    m_deleteBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::trash), QColor("white")), tr("删除选中文件"));
    m_deleteBtn->setProperty("class", "danger");
    m_deleteBtn->setEnabled(false);
    filterRow->addWidget(m_scanBtn);
    filterRow->addWidget(m_deleteBtn);
    root->addLayout(filterRow);

    // 进度条 + 状态行：放在表格上方（过滤行与结果表之间）
    auto* progressRow = new QHBoxLayout;
    m_progress = new QProgressBar;
    m_progress->setFixedHeight(10);
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(180);
    m_summary = new QLabel(tr("尚未扫描"));
    m_summary->setStyleSheet("color:#6C7A77; background:transparent;");
    // 关键：长路径不改变布局宽度，超出即省略号
    m_summary->setMinimumWidth(0);
    m_summary->setMaximumWidth(QWIDGETSIZE_MAX);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 实时计时标签（扫描中显示"已用时 X 秒"，参考 WizTree/TreeSize）
    m_scanTimerLabel = new QLabel;
    m_scanTimerLabel->setStyleSheet("color:#4B41E1; font-weight:600; background:transparent;");
    m_scanTimerLabel->hide();
    progressRow->addWidget(m_progress);
    progressRow->addWidget(m_summary, 1);
    progressRow->addWidget(m_scanTimerLabel);
    root->addLayout(progressRow);

    // 结果表（可自由排序 + 复选框多选）
    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels({
        QString(), tr("磁盘"), tr("文件大小"), tr("文件名"), tr("完整路径"), tr("修改时间")});
    auto* hdr = m_table->horizontalHeader();
    hdr->setMinimumHeight(36);
    hdr->setStretchLastSection(false);
    hdr->setSectionResizeMode(kColCheck, QHeaderView::Fixed);
    m_table->setColumnWidth(kColCheck, 28);
    // 其余列在填数据后按内容估算；完整路径吃剩余宽度
    for (int c : {kColDrive, kColSize, kColName, kColMtime})
        hdr->setSectionResizeMode(c, QHeaderView::Interactive);
    hdr->setSectionResizeMode(kColPath, QHeaderView::Stretch);
    hdr->setSortIndicatorShown(true);
    hdr->setSortIndicator(kColSize, Qt::DescendingOrder);
    hdr->setSectionsClickable(true);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideMiddle);

    // 表头文案带 ▲▼（静态 Qt 常丢原生排序箭头图，用字符兜底）
    auto updateHeaderArrows = [this]() {
        auto* hdr = m_table->horizontalHeader();
        const int sc = hdr->sortIndicatorSection();
        const QString arrow = hdr->sortIndicatorOrder() == Qt::AscendingOrder
            ? QStringLiteral(" ▲") : QStringLiteral(" ▼");
        auto label = [&](int col, const QString& title) {
            return title + (col == sc ? arrow : QString());
        };
        m_table->setHorizontalHeaderLabels({
            QString(),
            label(kColDrive, tr("磁盘")),
            label(kColSize, tr("文件大小")),
            label(kColName, tr("文件名")),
            label(kColPath, tr("完整路径")),
            label(kColMtime, tr("修改时间")),
        });
    };
    updateHeaderArrows();
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, [updateHeaderArrows](int, Qt::SortOrder) { updateHeaderArrows(); });

    // 表头全选：挂在表头第 0 列，点击表头空白也可切换
    m_headerCheck = new QCheckBox(m_table->horizontalHeader());
    m_headerCheck->setText(QString());
    m_headerCheck->setToolTip(tr("全选当前页"));
    m_headerCheck->setCursor(Qt::PointingHandCursor);
    m_headerCheck->setStyleSheet(checkBoxIndicatorStyle());
    m_headerCheck->hide();
    connect(m_headerCheck, &QCheckBox::toggled, this, [this](bool on) {
        const QSignalBlocker blockTable(m_table);
        m_table->setSortingEnabled(false);
        for (int r = 0; r < m_table->rowCount(); ++r) {
            auto* it = m_table->item(r, kColCheck);
            if (!it) continue;
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            auto* pathItem = m_table->item(r, kColPath);
            if (!pathItem) continue;
            const QString path = QDir::fromNativeSeparators(pathItem->text());
            if (on) m_checkedPaths.insert(path); else m_checkedPaths.remove(path);
        }
        m_table->setSortingEnabled(true);
        updateDeleteButtonState();
    });
    // 点击第 0 列表头区域 = 全选（避免误触发按复选列排序）
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int logical) {
        if (logical != kColCheck) return;
        if (!m_headerCheck->isVisible()) return;
        m_headerCheck->toggle();
        // 恢复大小列排序指示，避免表头落到复选列
        m_table->horizontalHeader()->setSortIndicator(kColSize,
            m_table->horizontalHeader()->sortIndicatorOrder());
    });
    root->addWidget(m_table, 1);

    // 分页栏：每页条数用完整文案，避免窄下拉 + 坏箭头夹在「20」「条」之间像乱码
    auto* pageRow = new QHBoxLayout;
    pageRow->addWidget(new QLabel(tr("每页")));
    m_pageSizeCombo = new QComboBox;
    m_pageSizeCombo->setObjectName(QStringLiteral("pageSizeCombo"));
    m_pageSizeCombo->addItem(tr("20 条"), 20);
    m_pageSizeCombo->addItem(tr("100 条"), 100);
    m_pageSizeCombo->setMinimumWidth(100);
    // 不自定义 down-arrow（border 三角在静态 Qt 下常显示成 □）
    m_pageSizeCombo->setStyleSheet(
        "QComboBox{padding:6px 10px; min-height:28px; border-radius:8px; border:1.5px solid #D8D5E8;}");
    pageRow->addWidget(m_pageSizeCombo);
    m_prevBtn = new QPushButton(tr("上一页"));
    m_nextBtn = new QPushButton(tr("下一页"));
    m_prevBtn->setProperty("class", "secondary");
    m_nextBtn->setProperty("class", "secondary");
    m_prevBtn->setStyleSheet("QPushButton{padding:6px 12px; min-width:72px;}");
    m_nextBtn->setStyleSheet("QPushButton{padding:6px 12px; min-width:72px;}");
    m_pageLabel = new QLabel;
    m_pageLabel->setStyleSheet("color:#6C7A77; background:transparent;");
    pageRow->addSpacing(8);
    pageRow->addWidget(m_prevBtn);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_nextBtn);
    pageRow->addStretch();
    root->addLayout(pageRow);

    auto gotoPage = [this](int page) {
        const int tp = qMax(1, totalPages());
        m_currentPage = qBound(0, page, tp - 1);
        renderPage();
    };
    connect(m_prevBtn, &QPushButton::clicked, this, [this, gotoPage]() { gotoPage(m_currentPage - 1); });
    connect(m_nextBtn, &QPushButton::clicked, this, [this, gotoPage]() { gotoPage(m_currentPage + 1); });
    connect(m_pageSizeCombo, &QComboBox::currentIndexChanged, this, [this, gotoPage](int) {
        m_pageSize = m_pageSizeCombo->currentData().toInt();
        gotoPage(0);
    });

    connect(m_scanBtn, &QPushButton::clicked, this, &BigFilePage::doScan);
    connect(m_deleteBtn, &QPushButton::clicked, this, &BigFilePage::doDelete);
    // 勾选变化 → 同步跨页集合 + 删除按钮可用性 + 全选框三态
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (!it || it->column() != kColCheck) return;
        auto* pathItem = m_table->item(it->row(), kColPath);
        if (!pathItem) return;
        const QString path = QDir::fromNativeSeparators(pathItem->text());
        QSignalBlocker blocker(m_headerCheck);
        if (it->checkState() == Qt::Checked) m_checkedPaths.insert(path);
        else m_checkedPaths.remove(path);
        int checked = 0;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            auto* cell = m_table->item(r, kColCheck);
            if (cell && cell->checkState() == Qt::Checked) ++checked;
        }
        if (checked == 0) m_headerCheck->setCheckState(Qt::Unchecked);
        else if (checked == m_table->rowCount()) m_headerCheck->setCheckState(Qt::Checked);
        else m_headerCheck->setCheckState(Qt::PartiallyChecked);
        updateDeleteButtonState();
    });
    // 行选中也可删（用户常点行高亮却不点勾选）
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection&, const QItemSelection&) {
        updateDeleteButtonState();
    });
}

void BigFilePage::fitColumnsToContents() {
    // 短列按内容收缩；文件名设上限；完整路径 Stretch 吃剩余
    auto* hdr = m_table->horizontalHeader();
    const QSignalBlocker block(hdr);

    m_table->setColumnWidth(kColCheck, 28);

    auto fit = [this](int col, int minW, int maxW, int pad = 12) {
        m_table->resizeColumnToContents(col);
        const int w = qBound(minW, m_table->columnWidth(col) + pad, maxW);
        m_table->setColumnWidth(col, w);
    };
    fit(kColDrive, 44, 56, 8);
    fit(kColSize, 96, 120, 20);   // 预留排序箭头
    fit(kColName, 100, 260, 16);
    fit(kColMtime, 140, 168, 12);

    hdr->setSectionResizeMode(kColCheck, QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColDrive, QHeaderView::Interactive);
    hdr->setSectionResizeMode(kColSize, QHeaderView::Interactive);
    hdr->setSectionResizeMode(kColName, QHeaderView::Interactive);
    hdr->setSectionResizeMode(kColMtime, QHeaderView::Interactive);
    hdr->setSectionResizeMode(kColPath, QHeaderView::Stretch);
}

void BigFilePage::updateDeleteButtonState() {
    int n = m_checkedPaths.size();
    if (n == 0 && m_table->selectionModel())
        n = m_table->selectionModel()->selectedRows().size();
    m_deleteBtn->setEnabled(n > 0 && !m_scanning);
    m_deleteBtn->setText(n > 0
        ? tr("删除选中文件 (%1)").arg(n) : tr("删除选中文件"));
}

void BigFilePage::doScan() {
    // 扫描中再点 = 取消（按钮保持可点以便取消）
    if (m_scanning) {
        m_scanCancelled.store(true);
        m_summary->setText(tr("正在取消……"));
        return;
    }
    m_scanning = true;
    m_scanCancelled.store(false);
    m_scanBtn->setText(tr("取消扫描"));
    m_scanBtn->setEnabled(true);
    m_scanBtn->setIcon(Icons::tinted(QString::fromUtf8(Icons::P::warning), QColor("white")));
    m_deleteBtn->setEnabled(false);
    m_table->setRowCount(0);
    m_progress->setRange(0, 0);
    m_summary->setText(tr("正在扫描……"));

    const QString targetDrive = m_driveCombo->currentData().toString();
    // 目录级扫描：优先用输入的目录（必须与所选磁盘同卷）
    QString scanRoot = targetDrive.isEmpty() ? QString() : targetDrive + "/";
    const QString dirText = m_dirEdit->text().trimmed();
    if (!dirText.isEmpty()) {
        const QFileInfo dfi(dirText);
        if (dfi.isDir()) {
            scanRoot = QDir::fromNativeSeparators(dfi.absoluteFilePath());
        } else {
            m_summary->setText(tr("目录不存在：%1").arg(dirText));
            m_scanning = false;
            m_scanBtn->setText(tr("开始扫描"));
            m_scanBtn->setIcon(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")));
            return;
        }
    }
    BigFileFilter filter;
    filter.minSizeBytes = qMax(1, m_sizeCombo->currentData().toInt()) * 1024LL * 1024;
    // 类型下拉：data 为空格分隔的扩展名集合，拆成 QStringList 精确匹配
    const QStringList exts = m_extCombo->currentData().toString()
                                 .split(' ', Qt::SkipEmptyParts);
    if (!exts.isEmpty()) filter.extensionFilter = exts;
    filter.topN = 100000; // 与分页配合：全量保留，不再截断 top 500

    m_lastScanElapsedMs.store(0);
    m_checkedPaths.clear();
    auto cancelled = [this]() { return m_scanCancelled.load(); };

    // 计时器必须在 UI 线程创建
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    m_scanTimerLabel->show();
    m_scanTimerLabel->setText(tr("已用时 0.0 秒"));
    auto* tickTimer = new QTimer(this);
    connect(tickTimer, &QTimer::timeout, this, [this, startMs]() {
        const qint64 ms = QDateTime::currentMSecsSinceEpoch() - startMs;
        m_scanTimerLabel->setText(tr("已用时 %1 秒").arg(QString::number(ms / 1000.0, 'f', 1)));
    });
    tickTimer->start(100);

    // 注意：不要用 QFuture::then(this, ...) 传 QList<FileInfo>——该类型未注册元对象，
    // 跨线程续体可能被静默丢弃（日志有结果、界面仍空白）。改用 invokeMethod 回传。
    (void)QtConcurrent::run([this, scanRoot, filter, cancelled, startMs, tickTimer]() {
        ScannerService scanner;
        std::function<bool(qint64, const QString&)> onProgress =
            [this, cancelled](qint64 n, const QString& path) -> bool {
            if (cancelled()) return false;
            QMetaObject::invokeMethod(this, [this, n, path]() {
                const QString elided = fontMetrics().elidedText(
                    tr("已扫描 %1 个文件  %2").arg(n).arg(path),
                    Qt::ElideMiddle, m_summary->width());
                m_summary->setText(elided);
                m_summary->setToolTip(path);
            }, Qt::QueuedConnection);
            return true;
        };
        QList<FileInfo> all;
        const qint64 minFile = filter.minSizeBytes;
        const qint64 minDir = qMax<qint64>(minFile * 2, 16LL * 1024 * 1024);
        if (scanRoot.isEmpty()) {
            for (const auto& d : enumerateDisks()) {
                if (cancelled()) break;
                if (d.driveLetter.startsWith("A:") || d.driveLetter.startsWith("B:")) continue;
                all += scanner.scanBlocking(QStringList{d.driveLetter + "/"}, onProgress, minFile, minDir, cancelled);
            }
        } else {
            all = scanner.scanBlocking(QStringList{scanRoot}, onProgress, minFile, minDir, cancelled);
        }
        QList<FileInfo> result;
        if (!cancelled()) {
            BigFileFinder finder;
            result = finder.find(all, filter);
        }
        const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - startMs;
        m_lastScanElapsedMs.store(elapsedMs);
        LOG << "scan finished, raw=" << all.size() << " filtered=" << result.size()
              << " cancelled=" << cancelled()
              << " elapsedMs=" << elapsedMs;

        QMetaObject::invokeMethod(this, [this, tickTimer, result, elapsedMs]() {
            tickTimer->stop();
            tickTimer->deleteLater();
            m_scanTimerLabel->hide();
            m_scanning = false;
            m_scanBtn->setText(tr("开始扫描"));
            m_scanBtn->setIcon(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")));
            m_scanBtn->setEnabled(true);
            m_progress->setRange(0, 1);
            if (m_scanCancelled.load()) {
                m_progress->setValue(0);
                m_summary->setText(tr("扫描已取消"));
                LOG << "UI: scan cancelled, drop results=" << result.size();
                return;
            }
            m_progress->setValue(1);
            m_files = result;
            populateResults();
            m_deleteBtn->setEnabled(false); // 需勾选后才可删
            updateDeleteButtonState();
            qint64 total = 0;
            for (const auto& f : m_files) total += f.size;
            const QString elapsed = elapsedMs >= 1000
                ? tr("%1 秒").arg(QString::number(elapsedMs / 1000.0, 'f', 1))
                : tr("%1 毫秒").arg(elapsedMs);
            m_summary->setText(tr("共 %1 个文件，合计 %2，耗时 %3")
                                   .arg(m_files.size()).arg(formatSize(total)).arg(elapsed));
            LOG << "UI: populated files=" << m_files.size()
                  << " tableRows=" << m_table->rowCount();
        }, Qt::QueuedConnection);
    });
}

void BigFilePage::populateResults() {
    // 新扫描结果：清空跨页勾选，回到第 1 页
    m_checkedPaths.clear();
    m_currentPage = 0;
    renderPage();
}

int BigFilePage::totalPages() const {
    return (m_files.size() + m_pageSize - 1) / m_pageSize;
}

void BigFilePage::renderPage() {
    // 填充期间阻断 itemChanged：否则 setItem(复选框) 时路径列尚未就位 → 空指针崩溃，结果「扫到了但界面空白」
    const QSignalBlocker blockTable(m_table);
    const QSignalBlocker blockHeader(m_headerCheck);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    // 当前页切片（m_files 已按大小排序；排序由表头触发时重新切片）
    const int total = m_files.size();
    const int begin = qBound(0, m_currentPage * m_pageSize, total);
    const int end = qMin(begin + m_pageSize, total);
    const auto makeRow = [this](const FileInfo& f) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        // 复选框列（行选择以复选框为准）
        auto* itCheck = new QTableWidgetItem;
        itCheck->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        // 勾选状态按路径跨页保留
        itCheck->setCheckState(m_checkedPaths.contains(f.absolutePath) ? Qt::Checked : Qt::Unchecked);
        // 磁盘列
        auto* itDrive = new QTableWidgetItem(f.absolutePath.left(2).toUpper());
        // 文件大小列：友好展示 + 按字节排序
        auto* itSize = new SizeTableItem(f.size);
        auto* itName = new QTableWidgetItem(f.name);
        auto* itPath = new QTableWidgetItem(QDir::toNativeSeparators(f.absolutePath));
        auto* itTime = new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(f.lastModified).toString("yyyy-MM-dd HH:mm"));
        m_table->setItem(r, kColCheck, itCheck);
        m_table->setItem(r, kColDrive, itDrive);
        m_table->setItem(r, kColSize, itSize);
        m_table->setItem(r, kColName, itName);
        m_table->setItem(r, kColPath, itPath);
        m_table->setItem(r, kColMtime, itTime);
    };
    // 分组模式也走统一分页（组内顺序展示，页切片全局生效）
    const QList<FileInfo> pageFiles = m_files.mid(begin, end - begin);
    if (m_groupByDrive->isChecked()) {
        QMap<QString, QList<FileInfo>> byDrive;
        for (const auto& f : pageFiles)
            byDrive[f.absolutePath.left(2).toUpper()].append(f);
        for (auto it = byDrive.constBegin(); it != byDrive.constEnd(); ++it)
            for (const FileInfo& f : it.value())
                makeRow(f);
    } else {
        for (const auto& f : pageFiles) makeRow(f);
    }
    m_table->setSortingEnabled(true);
    fitColumnsToContents();
    // 表头全选框定位：悬浮在第 0 列表头上（排序禁用期间表头视口稳定）
    m_headerCheck->setVisible(total > 0);
    if (total > 0) {
        const int colW = m_table->columnWidth(kColCheck);
        const int headerH = m_table->horizontalHeader()->height();
        m_headerCheck->setFixedSize(14, 14);
        m_headerCheck->move(qMax(0, (colW - 14) / 2), qMax(0, (headerH - 14) / 2));
        m_headerCheck->raise();
        m_headerCheck->show();
    }
    // 保持「文件大小」为默认排序列指示
    if (m_table->horizontalHeader()->sortIndicatorSection() == kColCheck)
        m_table->horizontalHeader()->setSortIndicator(kColSize, Qt::DescendingOrder);
    {
        bool allChecked = false;
        if (m_table->rowCount() > 0) {
            int checked = 0;
            for (int r = 0; r < m_table->rowCount(); ++r) {
                auto* it = m_table->item(r, kColCheck);
                if (it && it->checkState() == Qt::Checked) ++checked;
            }
            allChecked = checked == m_table->rowCount();
        }
        m_headerCheck->setChecked(allChecked);
    }
    // 分页栏状态
    const int tp = qMax(1, totalPages());
    m_pageLabel->setText(tr("第 %1 / %2 页，共 %3 条").arg(m_currentPage + 1).arg(tp).arg(total));
    m_prevBtn->setEnabled(m_currentPage > 0);
    m_nextBtn->setEnabled(m_currentPage < tp - 1);
    updateDeleteButtonState();
}

void BigFilePage::doDelete() {
    // 优先勾选集合；无勾选则用当前行选中（点行高亮未打勾也能删）
    QStringList paths = m_checkedPaths.values();
    if (paths.isEmpty() && m_table->selectionModel()) {
        for (const QModelIndex& idx : m_table->selectionModel()->selectedRows()) {
            auto* pathItem = m_table->item(idx.row(), kColPath);
            if (!pathItem) continue;
            paths << QDir::fromNativeSeparators(pathItem->text());
        }
    }
    paths.removeDuplicates();
    if (paths.isEmpty()) {
        m_summary->setText(tr("请先勾选或选中要删除的文件"));
        return;
    }

    QHash<QString, qint64> sizeOf;
    for (const auto& f : m_files) sizeOf.insert(f.absolutePath, f.size);
    qint64 bytes = 0;
    QList<CleanItem> items;
    for (const QString& path : paths) {
        CleanItem it;
        it.category = CleanCategory::CustomRules;
        it.path = path;
        it.size = sizeOf.value(path, 0);
        it.safeToDelete = true;
        it.description = tr("大文件清理");
        items.append(it);
        bytes += it.size;
    }

    const auto reply = QMessageBox::question(
        this, tr("确认删除"),
        tr("将把 %1 个文件移到回收站（约 %2）。\n系统目录下的文件会被跳过。\n\n确定继续？")
            .arg(items.size()).arg(formatSize(bytes)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    m_scanBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
    m_progress->setRange(0, 0);
    m_summary->setText(tr("正在删除 %1 个文件（到回收站）……").arg(items.size()));

    (void)QtConcurrent::run([this, items]() {
        CleanerService svc;
        const qint64 freed = svc.clean(items, true);
        QMetaObject::invokeMethod(this, [this, freed, total = items.size()]() {
            m_progress->setRange(0, 1);
            m_progress->setValue(1);
            m_checkedPaths.clear();
            m_summary->setText(tr("已释放 %1（目标 %2 个），正在刷新列表……")
                                   .arg(formatSize(freed)).arg(total));
            LOG << "delete done freed=" << freed << " requested=" << total;
            m_scanBtn->setEnabled(true);
            doScan();
        }, Qt::QueuedConnection);
    });
}

} // namespace DiskOrganizer
