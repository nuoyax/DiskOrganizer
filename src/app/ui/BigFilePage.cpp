#include "BigFilePage.h"
#include "Icons.h"
#include "SearchableComboBox.h"
#include "util/FileSystemUtil.h"
#include "util/SizeFormatter.h"
#include "services/ScannerService.h"
#include "services/BigFileFinder.h"
#include "services/CleanerService.h"
#include "Charts.h"

#include <QCheckBox>
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
constexpr int kRolePath = Qt::UserRole;        // 完整路径
constexpr int kRoleSize = Qt::UserRole + 1;    // 字节数（用于删除）
constexpr int kColDrive = 0;
constexpr int kColSize  = 1;
constexpr int kColName  = 2;
constexpr int kColPath  = 3;
constexpr int kColMtime = 4;
}

BigFilePage::BigFilePage(QWidget* parent) : PageBase(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("扫描磁盘上的大文件与旧文件，结果按磁盘分类，点击表头自由排序"));
    title->setStyleSheet("color:#636E88; background:transparent;");
    root->addWidget(title);

    // 过滤条件行
    auto* filterRow = new QHBoxLayout;
    m_driveCombo = new SearchableComboBox;
    const auto drives = enumerateDisks();
    m_driveCombo->addItem(tr("全部磁盘"), QString());
    for (const auto& d : drives)
        m_driveCombo->addItem(QString("%1 (%2)").arg(d.driveLetter, d.volumeLabel.isEmpty()
            ? QStringLiteral("本地磁盘") : d.volumeLabel), d.driveLetter);
    m_driveCombo->setFixedWidth(260);
    // 大小阈值：预设下拉（可搜索复用），单位 MB；含自定义数值
    m_sizeCombo = new SearchableComboBox;
    m_sizeCombo->setFixedWidth(150);
    m_sizeCombo->addItem("1 MB", 1);
    m_sizeCombo->addItem("10 MB", 10);
    m_sizeCombo->addItem("50 MB", 50);
    m_sizeCombo->addItem("100 MB", 100);
    m_sizeCombo->addItem("500 MB", 500);
    m_sizeCombo->addItem("1 GB", 1024);
    m_sizeCombo->addItem("5 GB", 5 * 1024);
    m_sizeCombo->addItem("10 GB", 10 * 1024);
    m_sizeCombo->setCurrentIndex(3);   // 默认 100MB

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
        QIcon ic = Icons::tinted(QString::fromUtf8(groupIcons[gi]), QColor(0x5A, 0x64, 0x78), 18);
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
    filterRow->addWidget(new QLabel(tr("大小:")));
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
    m_summary->setStyleSheet("color:#636E88; background:transparent;");
    // 关键：长路径不改变布局宽度，超出即省略号
    m_summary->setMinimumWidth(0);
    m_summary->setMaximumWidth(QWIDGETSIZE_MAX);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    progressRow->addWidget(m_progress);
    progressRow->addWidget(m_summary, 1);
    root->addLayout(progressRow);

    // 结果表（可自由排序）
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({tr("磁盘"), tr("大小"), tr("文件名"), tr("完整路径"), tr("修改时间")});
    m_table->horizontalHeader()->setSectionResizeMode(kColPath, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSortIndicator(kColSize, Qt::DescendingOrder);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    connect(m_scanBtn, &QPushButton::clicked, this, &BigFilePage::doScan);
    connect(m_deleteBtn, &QPushButton::clicked, this, &BigFilePage::doDelete);
}

void BigFilePage::doScan() {
    // 扫描中再点 = 取消
    if (!m_scanBtn->isEnabled() || m_scanning) {
        m_scanCancelled = true;
        return;
    }
    m_scanning = true;
    m_scanBtn->setText(tr("取消扫描"));
    m_scanBtn->setEnabled(true);
    m_scanBtn->setIcon(Icons::tinted(QString::fromUtf8(Icons::P::warning), QColor("white")));
    m_deleteBtn->setEnabled(false);
    m_table->setRowCount(0);
    m_progress->setRange(0, 0);
    m_summary->setText(tr("正在扫描……"));

    const QString targetDrive = m_driveCombo->currentData().toString();
    BigFileFilter filter;
    filter.minSizeBytes = qMax(1, m_sizeCombo->currentData().toInt()) * 1024LL * 1024;
    // 类型下拉：data 为空格分隔的扩展名集合，拆成 QStringList 精确匹配
    const QStringList exts = m_extCombo->currentData().toString()
                                 .split(' ', Qt::SkipEmptyParts);
    if (!exts.isEmpty()) filter.extensionFilter = exts;
    filter.topN = 500;

    // 取消令牌：扫描中再点按钮即置位
    m_scanCancelled.store(false);
    auto cancelled = [this]() { return m_scanCancelled.load(); };

    // 进度由后台线程经QueuedConnection回UI：文件数 + 当前路径
    m_progress->setRange(0, 0);

    QtConcurrent::run([this, targetDrive, filter, cancelled]() {
        ScannerService scanner;
        // 进度回调：更新忙碌条 + 汇总文本（跨线程→Queued）
        std::function<bool(qint64, const QString&)> onProgress =
            [this, cancelled](qint64 n, const QString& path) -> bool {
            if (cancelled()) return false;
            QMetaObject::invokeMethod(this, [this, n, path]() {
                // elided：长路径省略号，避免撑宽窗口
                const QString elided = fontMetrics().elidedText(
                    tr("已扫描 %1 个文件  %2").arg(n).arg(path),
                    Qt::ElideMiddle, m_summary->width());
                m_summary->setText(elided);
                m_summary->setToolTip(path);
            }, Qt::QueuedConnection);
            return true;
        };
        QList<FileInfo> all;
        // 提速剪枝：按用户设置的大小阈值跳过小文件；
        // 总大小低于阈值×2 的小目录整体跳过（零碎文件不值得遍历）
        const qint64 minFile = filter.minSizeBytes;
        const qint64 minDir = qMax<qint64>(minFile * 2, 16LL * 1024 * 1024);
        if (targetDrive.isEmpty()) {
            for (const auto& d : enumerateDisks()) {
                if (cancelled()) break;
                if (d.driveLetter.startsWith("A:") || d.driveLetter.startsWith("B:")) continue;
                all += scanner.scanBlocking(QStringList{d.driveLetter + "/"}, onProgress, minFile, minDir, cancelled);
            }
        } else {
            all = scanner.scanBlocking(QStringList{targetDrive + "/"}, onProgress, minFile, minDir, cancelled);
        }
        if (cancelled()) return QList<FileInfo>();
        BigFileFinder finder;
        return finder.find(all, filter);
    }).then(this, [this](QList<FileInfo> result) {
        m_scanning = false;
        m_scanBtn->setText(tr("开始扫描"));
        m_scanBtn->setIcon(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")));
        m_progress->setRange(0, 1);
        if (m_scanCancelled) {
            m_progress->setValue(0);
            m_summary->setText(tr("扫描已取消"));
            m_scanBtn->setEnabled(true);
            return;
        }
        m_progress->setValue(1);
        m_files = result;
        populateResults();
        m_scanBtn->setEnabled(true);
        m_deleteBtn->setEnabled(!m_files.isEmpty());
        qint64 total = 0;
        for (const auto& f : m_files) total += f.size;
        m_summary->setText(tr("共 %1 个文件，合计 %2")
                               .arg(m_files.size()).arg(formatSize(total)));
    });
}

void BigFilePage::populateResults() {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    if (m_groupByDrive->isChecked()) {
        // 按磁盘聚合：同盘文件相邻（QMultiMap 按键排序），键内保持插入序
        QMap<QString, QList<const FileInfo*>> byDrive;
        for (const auto& f : m_files)
            byDrive[f.absolutePath.left(2).toUpper()].append(&f);
        for (auto it = byDrive.constBegin(); it != byDrive.constEnd(); ++it) {
            const QString& driveKey = it.key();
            for (const FileInfo* f : it.value()) {
                const int r = m_table->rowCount();
                m_table->insertRow(r);
                auto* itDrive = new QTableWidgetItem(driveKey);
                itDrive->setData(kRolePath, f->absolutePath);
                itDrive->setData(kRoleSize, f->size);
                auto* itSize = new QTableWidgetItem;
                itSize->setData(Qt::DisplayRole, formatSize(f->size));
                itSize->setData(Qt::UserRole + 10, f->size);  // 排序用
                auto* itName = new QTableWidgetItem(f->name);
                auto* itPath = new QTableWidgetItem(f->absolutePath);
                auto* itTime = new QTableWidgetItem(
                    QDateTime::fromMSecsSinceEpoch(f->lastModified)
                        .toString("yyyy-MM-dd HH:mm"));
                m_table->setItem(r, kColDrive, itDrive);
                m_table->setItem(r, kColSize, itSize);
                m_table->setItem(r, kColName, itName);
                m_table->setItem(r, kColPath, itPath);
                m_table->setItem(r, kColMtime, itTime);
            }
        }
    } else {
        for (const auto& f : m_files) {
            const int r = m_table->rowCount();
            m_table->insertRow(r);
            auto* itDrive = new QTableWidgetItem(f.absolutePath.left(2).toUpper());
            itDrive->setData(kRolePath, f.absolutePath);
            itDrive->setData(kRoleSize, f.size);
            auto* itSize = new QTableWidgetItem;
            itSize->setData(Qt::DisplayRole, formatSize(f.size));
            itSize->setData(Qt::UserRole + 10, f.size);
            m_table->setItem(r, kColDrive, itDrive);
            m_table->setItem(r, kColSize, itSize);
            m_table->setItem(r, kColName, new QTableWidgetItem(f.name));
            m_table->setItem(r, kColPath, new QTableWidgetItem(f.absolutePath));
            m_table->setItem(r, kColMtime, new QTableWidgetItem(
                QDateTime::fromMSecsSinceEpoch(f.lastModified).toString("yyyy-MM-dd HH:mm")));
        }
    }
    m_table->setSortingEnabled(true);
}

void BigFilePage::doDelete() {
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) { m_summary->setText(tr("请先选中要删除的文件行")); return; }

    QList<CleanItem> items;
    for (const auto& idx : rows) {
        const int r = idx.row();
        auto* itDrive = m_table->item(r, kColDrive);
        CleanItem it;
        it.category = CleanCategory::CustomRules;
        it.path = itDrive->data(kRolePath).toString();
        it.size = itDrive->data(kRoleSize).toLongLong();
        it.safeToDelete = true;
        it.description = tr("大文件清理");
        items.append(it);
    }

    m_scanBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
    m_progress->setRange(0, 0);
    m_summary->setText(tr("正在删除 %1 个文件（到回收站）……").arg(items.size()));

    QtConcurrent::run([items]() {
        CleanerService svc;
        return svc.clean(items, true);
    }).then(this, [this](qint64 freed) {
        m_progress->setRange(0, 1);
        m_progress->setValue(1);
        m_summary->setText(tr("已释放 %1，正在刷新列表……").arg(formatSize(freed)));
        doScan();
    });
}

} // namespace DiskOrganizer
