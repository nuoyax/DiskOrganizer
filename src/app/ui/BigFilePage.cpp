#include "BigFilePage.h"
#include "Icons.h"
#include "SearchableComboBox.h"
#include "util/FileSystemUtil.h"
#include "util/SizeFormatter.h"
#include "services/ScannerService.h"
#include "services/BigFileFinder.h"
#include "services/Logger.h"
#include "services/CleanerService.h"
#include "Charts.h"

#include <QCheckBox>
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
    m_summary->setStyleSheet("color:#636E88; background:transparent;");
    // 关键：长路径不改变布局宽度，超出即省略号
    m_summary->setMinimumWidth(0);
    m_summary->setMaximumWidth(QWIDGETSIZE_MAX);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 实时计时标签（扫描中显示"已用时 X 秒"，参考 WizTree/TreeSize）
    m_scanTimerLabel = new QLabel;
    m_scanTimerLabel->setStyleSheet("color:#2B6CB0; font-weight:600; background:transparent;");
    m_scanTimerLabel->hide();
    progressRow->addWidget(m_progress);
    progressRow->addWidget(m_summary, 1);
    progressRow->addWidget(m_scanTimerLabel);
    root->addLayout(progressRow);

    // 结果表（可自由排序 + 复选框多选）
    m_table = new QTableWidget(0, 6);
    m_table->setHorizontalHeaderLabels({tr(""), tr("磁盘"), tr("文件大小"), tr("文件名"), tr("完整路径"), tr("修改时间")});
    m_table->horizontalHeader()->setSectionResizeMode(kColPath, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSortIndicator(kColSize, Qt::DescendingOrder);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    // 表头全选复选框
    m_headerCheck = new QCheckBox(m_table);
    m_headerCheck->setStyleSheet("QCheckBox::indicator{width:16px;height:16px}");
    m_headerCheck->setToolTip(tr("全选/全不选"));
    connect(m_headerCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_table->setSortingEnabled(false);
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (auto* it = m_table->item(r, kColCheck)) it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        }
        m_table->setSortingEnabled(true);
        updateDeleteButtonState();
    });
    root->addWidget(m_table, 1);

    connect(m_scanBtn, &QPushButton::clicked, this, &BigFilePage::doScan);
    connect(m_deleteBtn, &QPushButton::clicked, this, &BigFilePage::doDelete);
    // 勾选变化 → 删除按钮可用性 + 全选框三态
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (it->column() != kColCheck) return;
        QSignalBlocker blocker(m_headerCheck);
        int checked = 0;
        for (int r = 0; r < m_table->rowCount(); ++r)
            if (m_table->item(r, kColCheck)->checkState() == Qt::Checked) ++checked;
        if (checked == 0) m_headerCheck->setCheckState(Qt::Unchecked);
        else if (checked == m_table->rowCount()) m_headerCheck->setCheckState(Qt::Checked);
        else m_headerCheck->setCheckState(Qt::PartiallyChecked);
        updateDeleteButtonState();
    });
}

void BigFilePage::updateDeleteButtonState() {
    int checked = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (m_table->item(r, kColCheck) && m_table->item(r, kColCheck)->checkState() == Qt::Checked)
            ++checked;
    m_deleteBtn->setEnabled(checked > 0);
    m_deleteBtn->setText(checked > 0
        ? tr("删除勾选文件 (%1)").arg(checked) : tr("删除选中文件"));
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
    filter.topN = 500;

    // 取消令牌：扫描中再点按钮即置位
    m_scanCancelled.store(false);
    m_lastScanElapsedMs.store(0);
    auto cancelled = [this]() { return m_scanCancelled.load(); };

    // 进度由后台线程经QueuedConnection回UI：文件数 + 当前路径
    m_progress->setRange(0, 0);

    QtConcurrent::run([this, scanRoot, filter, cancelled]() {
        const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
        // 扫描中实时计时（参考 WizTree/TreeSize：状态栏显示已用时）
        const auto tickTimer = new QTimer(this);
        tickTimer->setParent(this);
        connect(tickTimer, &QTimer::timeout, this, [this, startMs]() {
            const qint64 ms = QDateTime::currentMSecsSinceEpoch() - startMs;
            m_scanTimerLabel->setText(tr("已用时 %1 秒").arg(QString::number(ms / 1000.0, 'f', 1)));
        });
        tickTimer->start(100);
        QMetaObject::invokeMethod(this, [this]() {
            m_scanTimerLabel->setText(tr("已用时 0.0 秒"));
        }, Qt::QueuedConnection);
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
        if (scanRoot.isEmpty()) {
            // 整盘：枚举所有磁盘逐个扫
            for (const auto& d : enumerateDisks()) {
                if (cancelled()) break;
                if (d.driveLetter.startsWith("A:") || d.driveLetter.startsWith("B:")) continue;
                all += scanner.scanBlocking(QStringList{d.driveLetter + "/"}, onProgress, minFile, minDir, cancelled);
            }
        } else {
            // 整卷或目录级（MFT 快速路径均支持，目录按前缀过滤）
            all = scanner.scanBlocking(QStringList{scanRoot}, onProgress, minFile, minDir, cancelled);
        }
        if (cancelled()) return QList<FileInfo>();
        BigFileFinder finder;
        auto result = finder.find(all, filter);
        LOG << "scan finished, raw=" << all.size() << " filtered=" << result.size()
              << " cancelled=" << cancelled()
              << " elapsedMs=" << (QDateTime::currentMSecsSinceEpoch() - startMs);
        // 经由 this 传递扫描耗时到 then 回调（原子写，跨线程安全）
        m_lastScanElapsedMs.store(QDateTime::currentMSecsSinceEpoch() - startMs);
        QMetaObject::invokeMethod(this, [this]() { m_scanTimerLabel->hide(); }, Qt::QueuedConnection);
        tickTimer->stop();
        tickTimer->deleteLater();
        return result;
    }).then(this, [this](QList<FileInfo> result) {
        m_scanning = false;
        m_scanBtn->setText(tr("开始扫描"));
        m_scanBtn->setIcon(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")));
        m_progress->setRange(0, 1);
        if (m_scanCancelled) {
            m_progress->setValue(0);
            m_scanTimerLabel->hide();
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
        // 扫描时长人性化展示（秒/毫秒）
        const qint64 elapsedMs = m_lastScanElapsedMs.load();
        const QString elapsed = elapsedMs >= 1000
            ? tr("%1 秒").arg(QString::number(elapsedMs / 1000.0, 'f', 1))
            : tr("%1 毫秒").arg(elapsedMs);
        m_summary->setText(tr("共 %1 个文件，合计 %2，耗时 %3")
                               .arg(m_files.size()).arg(formatSize(total)).arg(elapsed));
    });
}

void BigFilePage::populateResults() {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    const auto makeRow = [this](const FileInfo& f) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        // 复选框列（行选择以复选框为准）
        auto* itCheck = new QTableWidgetItem;
        itCheck->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        itCheck->setCheckState(Qt::Unchecked);
        // 磁盘列
        auto* itDrive = new QTableWidgetItem(f.absolutePath.left(2).toUpper());
        // 文件大小列：原始字节数作 DisplayRole 排序键，展示用 formatSize
        auto* itSize = new QTableWidgetItem;
        itSize->setData(Qt::DisplayRole, f.size);            // 排序按字节
        itSize->setData(Qt::UserRole + 10, formatSize(f.size)); // 展示文本（代理读 UserRole）
        itSize->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    if (m_groupByDrive->isChecked()) {
        QMap<QString, QList<const FileInfo*>> byDrive;
        for (const auto& f : m_files)
            byDrive[f.absolutePath.left(2).toUpper()].append(&f);
        for (auto it = byDrive.constBegin(); it != byDrive.constEnd(); ++it)
            for (const FileInfo* f : it.value())
                makeRow(*f);
    } else {
        for (const auto& f : m_files) makeRow(f);
    }
    m_table->setSortingEnabled(true);
    m_headerCheck->setVisible(m_table->rowCount() > 0);
    m_headerCheck->setChecked(false);
    updateDeleteButtonState();
}

void BigFilePage::doDelete() {
    // 以复选框勾选为准（支持跨页/排序后稳定选择）
    QList<CleanItem> items;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto* itCheck = m_table->item(r, kColCheck);
        if (!itCheck || itCheck->checkState() != Qt::Checked) continue;
        const QString path = QDir::fromNativeSeparators(m_table->item(r, kColPath)->text());
        CleanItem it;
        it.category = CleanCategory::CustomRules;
        it.path = path;
        it.size = m_table->item(r, kColSize)->data(Qt::DisplayRole).toLongLong();
        it.safeToDelete = true;
        it.description = tr("大文件清理");
        items.append(it);
    }
    if (items.isEmpty()) { m_summary->setText(tr("请先勾选要删除的文件")); return; }

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
