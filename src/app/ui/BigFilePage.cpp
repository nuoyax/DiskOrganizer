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
#include "services/ReportService.h"
#include "Charts.h"

#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QProcess>
#include <QFrame>
#include <QItemSelectionModel>
#include <QItemSelection>
#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QMap>
#include <QSignalBlocker>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <QFileInfo>
#include <QGridLayout>
#include <QSizePolicy>
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

bool isUndeletableSystemFile(const QString& path) {
    const QString name = QFileInfo(path).fileName().toLower();
    return name == QLatin1String("pagefile.sys")
        || name == QLatin1String("hiberfil.sys")
        || name == QLatin1String("swapfile.sys");
}

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
} // namespace

BigFilePage::BigFilePage(QWidget* parent) : PageBase(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* root = new QVBoxLayout(this);
    // contentArea 已有留白，页内不再叠外边距；收紧间距把高度让给列表
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto fieldLabel = [](const QString& text, QWidget* parent) {
        auto* lab = new QLabel(text, parent);
        lab->setStyleSheet(
            "font-size:12px; font-weight:600; color:#6C7A77; background:transparent;");
        return lab;
    };

    // ===== 页头：仅标题与状态 =====
    auto* head = new QHBoxLayout;
    head->setSpacing(12);
    auto* headCol = new QVBoxLayout;
    headCol->setSpacing(4);
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto* title = new QLabel(tr("大文件"), this);
    title->setStyleSheet(
        "font-size:24px; font-weight:800; color:#181445; background:transparent;");
    m_headStatus = new QLabel(tr("扫描引擎已就绪"), this);
    m_headStatus->setStyleSheet(
        "padding:3px 10px; border-radius:12px; background-color:#E6F2EF; color:#006B5F;"
        "font-size:12px; font-weight:600;");
    titleRow->addWidget(title);
    titleRow->addWidget(m_headStatus);
    titleRow->addStretch();
    auto* subtitle = new QLabel(
        tr("扫描磁盘上的大文件与旧文件，结果按磁盘分类，点击表头自由排序"), this);
    subtitle->setStyleSheet("color:#6C7A77; background:transparent;");
    headCol->addLayout(titleRow);
    headCol->addWidget(subtitle);
    head->addLayout(headCol, 1);
    root->addLayout(head);

    // ===== 三列指标卡对齐 =====
    auto* metrics = new QHBoxLayout;
    metrics->setSpacing(12);
    auto makeMetric = [this](const char* iconPath, const QColor& tint, const QColor& bg,
                             const QString& cap) -> QLabel* {
        auto* card = new QFrame(this);
        card->setProperty("class", "card");
        card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(12, 8, 12, 8);
        h->setSpacing(8);
        auto* chip = new QLabel(card);
        chip->setFixedSize(36, 36);
        chip->setAlignment(Qt::AlignCenter);
        chip->setPixmap(Icons::tinted(QString::fromUtf8(iconPath), tint, 18).pixmap(18, 18));
        chip->setStyleSheet(QString("background:%1; border-radius:10px;").arg(bg.name()));
        auto* col = new QVBoxLayout;
        col->setSpacing(2);
        auto* c = new QLabel(cap, card);
        c->setStyleSheet("font-size:11px; color:#6C7A77; background:transparent;");
        auto* v = new QLabel(QStringLiteral("--"), card);
        v->setStyleSheet(
            "font-size:16px; font-weight:800; color:#181445; background:transparent;");
        col->addWidget(c);
        col->addWidget(v);
        h->addWidget(chip);
        h->addLayout(col, 1);
        return v;
    };
    m_countMetric = makeMetric(Icons::P::bigfile, QColor(0x4B, 0x41, 0xE1),
                               QColor(0xEE, 0xF2, 0xFF), tr("发现大文件数"));
    metrics->addWidget(m_countMetric->parentWidget(), 1);
    m_totalMetric = makeMetric(Icons::P::disk, QColor(0x0D, 0x94, 0x88),
                               QColor(0xF0, 0xFD, 0xFA), tr("累计占用总计"));
    metrics->addWidget(m_totalMetric->parentWidget(), 1);
    m_selectedMetric = makeMetric(Icons::P::trash, QColor(0xE1, 0x1D, 0x48),
                                  QColor(0xFF, 0xF1, 0xF2), tr("已筛选选中容量"));
    metrics->addWidget(m_selectedMetric->parentWidget(), 1);
    root->addLayout(metrics);

    // ===== 筛选卡：标签上行、控件下行，四列网格对齐 =====
    auto* filterCard = new QFrame(this);
    filterCard->setProperty("class", "card");
    filterCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* filterLay = new QVBoxLayout(filterCard);
    filterLay->setContentsMargins(12, 10, 12, 10);
    filterLay->setSpacing(6);

    m_driveCombo = new SearchableComboBox;
    const QIcon driveIcon =
        Icons::tinted(QString::fromUtf8(Icons::P::drive), QColor(0x6C, 0x7A, 0x77), 18);
    const QIcon allIcon =
        Icons::tinted(QString::fromUtf8(Icons::P::disk), QColor(0x4B, 0x41, 0xE1), 18);
    m_driveCombo->addItem(allIcon, tr("全部磁盘"), QString());
    for (const auto& d : enumerateDisks())
        m_driveCombo->addItem(
            driveIcon,
            QString("%1 (%2)").arg(d.driveLetter,
                                   d.volumeLabel.isEmpty() ? QStringLiteral("本地磁盘")
                                                          : d.volumeLabel),
            d.driveLetter);
    m_driveCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_driveCombo->setMinimumHeight(36);

    m_dirEdit = new QLineEdit;
    m_dirEdit->setPlaceholderText(tr("留空 = 整盘扫描，如 C:/Users"));
    m_dirEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_dirEdit->setMinimumHeight(36);
    auto* browseBtn = new QPushButton(tr("浏览…"), filterCard);
    browseBtn->setProperty("class", "secondary");
    browseBtn->setMinimumHeight(36);
    browseBtn->setMinimumWidth(72);
    browseBtn->setStyleSheet(
        QStringLiteral("QPushButton{padding:6px 14px; min-width:72px; border-radius:10px;}"));
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString dir =
            QFileDialog::getExistingDirectory(this, tr("选择扫描目录"), m_dirEdit->text());
        if (!dir.isEmpty())
            m_dirEdit->setText(QDir::fromNativeSeparators(dir));
    });
    auto* dirRow = new QWidget(filterCard);
    auto* dirLay = new QHBoxLayout(dirRow);
    dirLay->setContentsMargins(0, 0, 0, 0);
    dirLay->setSpacing(8);
    dirLay->addWidget(m_dirEdit, 1);
    dirLay->addWidget(browseBtn);

    m_sizeCombo = new SearchableComboBox;
    m_sizeCombo->setToolTip(tr("只显示大于该大小的文件"));
    m_sizeCombo->addItem(tr("> 100 MB"), 100);
    m_sizeCombo->addItem(tr("> 500 MB"), 500);
    m_sizeCombo->addItem(tr("> 1 GB"), 1024);
    m_sizeCombo->addItem(tr("> 5 GB"), 5 * 1024);
    m_sizeCombo->addItem(tr("> 10 GB"), 10 * 1024);
    m_sizeCombo->setCurrentIndex(0);
    m_sizeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sizeCombo->setMinimumHeight(36);

    m_extCombo = new SearchableComboBox;
    m_extCombo->addItem(tr("全部类型"), QString());
    const struct {
        const char* name;
        const char* exts;
    } extGroups[] = {
        {"压缩包", ".zip .7z .rar .tar .gz .bz2 .xz .iso .cab .tgz"},
        {"视频", ".mp4 .mkv .avi .mov .wmv .flv .webm .m4v .mpg .rmvb .ts"},
        {"音频", ".mp3 .wav .flac .aac .ogg .wma .m4a .ape .mid"},
        {"图片", ".jpg .jpeg .png .gif .bmp .webp .svg .tif .tiff .raw .ico .heic"},
        {"文档", ".pdf .doc .docx .xls .xlsx .ppt .pptx .txt .md .csv .odt"},
        {"程序/库", ".exe .dll .lib .so .apk .msi .bin .sys .ocx .jar"},
        {"代码", ".cpp .h .hpp .c .cs .py .java .js .ts .html .css .json .xml .sql .go .rs"},
        {"开发环境", ".pdb .idb .obj .o .a .exp .wim .vhd .vhdx"},
        {"光盘镜像", ".iso .img .vhd .vhdx .wim .gho .mds"},
        {"数据库", ".db .sqlite .mdb .mdf .ldf .bak"},
        {"虚拟机", ".vmdk .vdi .qcow2 .ova .ovf .vmx"},
        {"其他", ".dat .log .tmp .cache .dmp .etl .evtx"},
    };
    const char* groupIcons[] = {
        Icons::P::duplicate, Icons::P::rocket, Icons::P::pie, Icons::P::bigfile,
        Icons::P::file, Icons::P::chip, Icons::P::code, Icons::P::terminal,
        Icons::P::disk, Icons::P::drive, Icons::P::duplicate, Icons::P::folder,
    };
    for (int gi = 0; gi < 12; ++gi) {
        QIcon ic =
            Icons::tinted(QString::fromUtf8(groupIcons[gi]), QColor(0x6C, 0x7A, 0x77), 18);
        const QString extsStr = QString::fromUtf8(extGroups[gi].exts);
        QStringList sample;
        const QStringList all = extsStr.split(' ', Qt::SkipEmptyParts);
        for (int e = 0; e < qMin(3, all.size()); ++e)
            sample.append(all[e].mid(1));
        const QString label = QString::fromUtf8(extGroups[gi].name)
            + QStringLiteral(" (%1…)").arg(sample.join(", "));
        m_extCombo->addItem(ic, label, extsStr);
    }
    m_extCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_extCombo->setMinimumHeight(36);

    m_ageCombo = new SearchableComboBox;
    m_ageCombo->setToolTip(tr("只显示早于指定天数未修改的文件"));
    m_ageCombo->addItem(tr("不限修改时间"), 0);
    m_ageCombo->addItem(tr("超过 30 天"), 30);
    m_ageCombo->addItem(tr("超过 90 天"), 90);
    m_ageCombo->addItem(tr("超过 180 天"), 180);
    m_ageCombo->addItem(tr("超过 365 天"), 365);
    m_ageCombo->setCurrentIndex(0);
    m_ageCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ageCombo->setMinimumHeight(36);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 4);
    grid->setColumnStretch(2, 2);
    grid->setColumnStretch(3, 3);
    // 上行标签与下行控件同一列左对齐
    grid->addWidget(fieldLabel(tr("目标磁盘范围"), filterCard), 0, 0);
    grid->addWidget(fieldLabel(tr("指定扫描目录（可选）"), filterCard), 0, 1);
    grid->addWidget(fieldLabel(tr("文件体积阈值"), filterCard), 0, 2);
    grid->addWidget(fieldLabel(tr("文件分类类型"), filterCard), 0, 3);
    grid->addWidget(m_driveCombo, 1, 0);
    grid->addWidget(dirRow, 1, 1);
    grid->addWidget(m_sizeCombo, 1, 2);
    grid->addWidget(m_extCombo, 1, 3);
    filterLay->addLayout(grid);

    auto* optRow = new QHBoxLayout;
    optRow->setContentsMargins(0, 4, 0, 0);
    optRow->setSpacing(10);
    m_groupByDrive = new QCheckBox(tr("按磁盘分组显示"), filterCard);
    m_groupByDrive->setChecked(true);
    auto* ageLab = fieldLabel(tr("修改时间"), filterCard);
    m_ageCombo->setMinimumWidth(140);
    m_ageCombo->setMaximumWidth(200);
    optRow->addWidget(m_groupByDrive);
    optRow->addWidget(ageLab);
    optRow->addWidget(m_ageCombo);
    optRow->addStretch();
    // 主操作集中在此行：扫描 / 导出 / 打开 / 删除
    m_scanBtn = new QPushButton(
        Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")),
        tr("开始扫描"), filterCard);
    m_scanBtn->setMinimumHeight(36);
    m_exportBtn = new QPushButton(tr("导出清单"), filterCard);
    m_exportBtn->setProperty("class", "secondary");
    m_exportBtn->setEnabled(false);
    m_exportBtn->setMinimumHeight(36);
    m_openBtn = new QPushButton(tr("打开所在文件夹"), filterCard);
    m_openBtn->setProperty("class", "secondary");
    m_openBtn->setEnabled(false);
    m_openBtn->setMinimumHeight(36);
    m_deleteBtn = new QPushButton(
        Icons::tinted(QString::fromUtf8(Icons::P::trash), QColor("white")),
        tr("删除选中文件"), filterCard);
    m_deleteBtn->setProperty("class", "danger");
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setMinimumHeight(36);
    optRow->addWidget(m_scanBtn);
    optRow->addWidget(m_exportBtn);
    optRow->addWidget(m_openBtn);
    optRow->addWidget(m_deleteBtn);
    filterLay->addLayout(optRow);
    root->addWidget(filterCard);

    // 进度 + 状态
    auto* progressRow = new QHBoxLayout;
    progressRow->setSpacing(10);
    m_progress = new QProgressBar;
    m_progress->setFixedHeight(10);
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(160);
    m_summary = new QLabel(tr("尚未扫描"));
    m_summary->setStyleSheet("color:#6C7A77; background:transparent;");
    m_summary->setMinimumWidth(0);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_scanTimerLabel = new QLabel;
    m_scanTimerLabel->setStyleSheet(
        "color:#4B41E1; font-weight:600; background:transparent;");
    m_scanTimerLabel->hide();
    progressRow->addWidget(m_progress);
    progressRow->addWidget(m_summary, 1);
    progressRow->addWidget(m_scanTimerLabel);
    root->addLayout(progressRow);

    // 结果表卡：占满剩余高度并贴底
    auto* tableCard = new QFrame(this);
    tableCard->setProperty("class", "card");
    tableCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* tv = new QVBoxLayout(tableCard);
    tv->setContentsMargins(8, 8, 8, 8);
    tv->setSpacing(6);

    m_table = new QTableWidget(0, 6);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_table->setMinimumHeight(280);
    m_table->setHorizontalHeaderLabels(
        {QString(), tr("磁盘"), tr("文件大小"), tr("文件名"), tr("完整路径"), tr("修改时间")});
    auto* hdr = m_table->horizontalHeader();
    hdr->setMinimumHeight(36);
    hdr->setStretchLastSection(false);
    hdr->setSectionResizeMode(kColCheck, QHeaderView::Fixed);
    m_table->setColumnWidth(kColCheck, 28);
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
    m_table->setShowGrid(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &BigFilePage::showTableContextMenu);

    auto updateHeaderArrows = [this]() {
        auto* hdr = m_table->horizontalHeader();
        const int sc = hdr->sortIndicatorSection();
        const QString arrow = hdr->sortIndicatorOrder() == Qt::AscendingOrder
            ? QStringLiteral(" ▲")
            : QStringLiteral(" ▼");
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
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [updateHeaderArrows](int, Qt::SortOrder) { updateHeaderArrows(); });

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
            if (!it || !(it->flags() & Qt::ItemIsUserCheckable))
                continue;
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            auto* pathItem = m_table->item(r, kColPath);
            if (!pathItem)
                continue;
            const QString path = QDir::fromNativeSeparators(pathItem->text());
            if (on)
                m_checkedPaths.insert(path);
            else
                m_checkedPaths.remove(path);
        }
        m_table->setSortingEnabled(true);
        updateDeleteButtonState();
    });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int logical) {
        if (logical != kColCheck)
            return;
        if (!m_headerCheck->isVisible())
            return;
        m_headerCheck->toggle();
        m_table->horizontalHeader()->setSortIndicator(
            kColSize, m_table->horizontalHeader()->sortIndicatorOrder());
    });
    tv->addWidget(m_table, /*stretch*/ 1);

    auto* pageRow = new QHBoxLayout;
    pageRow->addWidget(new QLabel(tr("每页")));
    m_pageSizeCombo = new QComboBox;
    m_pageSizeCombo->setObjectName(QStringLiteral("pageSizeCombo"));
    m_pageSizeCombo->addItem(tr("20 条"), 20);
    m_pageSizeCombo->addItem(tr("100 条"), 100);
    m_pageSizeCombo->setMinimumWidth(100);
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
    tv->addLayout(pageRow);
    root->addWidget(tableCard, 1);

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
    connect(m_exportBtn, &QPushButton::clicked, this, [this] {
        if (m_files.isEmpty())
            return;
        const QString path = QFileDialog::getSaveFileName(
            this, tr("导出大文件清单"), QStringLiteral("bigfiles.csv"),
            tr("CSV (*.csv);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        if (ReportService::exportScanReport(path, tr("大文件清单"), m_files))
            m_summary->setText(tr("清单已导出：%1").arg(path));
        else
            QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件"));
    });
    connect(m_openBtn, &QPushButton::clicked, this, [this] {
        QString path;
        const auto rows = m_table->selectionModel()->selectedRows();
        if (!rows.isEmpty())
            path = pathAtRow(rows.first().row());
        if (path.isEmpty() && !m_checkedPaths.isEmpty())
            path = *m_checkedPaths.constBegin();
        if (!path.isEmpty())
            openContainingFolder(path);
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (!it || it->column() != kColCheck)
            return;
        auto* pathItem = m_table->item(it->row(), kColPath);
        if (!pathItem)
            return;
        const QString path = QDir::fromNativeSeparators(pathItem->text());
        QSignalBlocker blocker(m_headerCheck);
        if (it->checkState() == Qt::Checked)
            m_checkedPaths.insert(path);
        else
            m_checkedPaths.remove(path);
        int checked = 0;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            auto* cell = m_table->item(r, kColCheck);
            if (cell && cell->checkState() == Qt::Checked)
                ++checked;
        }
        if (checked == 0)
            m_headerCheck->setCheckState(Qt::Unchecked);
        else if (checked == m_table->rowCount())
            m_headerCheck->setCheckState(Qt::Checked);
        else
            m_headerCheck->setCheckState(Qt::PartiallyChecked);
        updateDeleteButtonState();
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&) { updateDeleteButtonState(); });

    DiskOrganizer::applyCardShadows(this);
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
    if (m_openBtn)
        m_openBtn->setEnabled((!m_checkedPaths.isEmpty() ||
            (m_table->selectionModel() && !m_table->selectionModel()->selectedRows().isEmpty()) ||
            !m_files.isEmpty()) && !m_scanning);

    qint64 selectedBytes = 0;
    QHash<QString, qint64> sizeOf;
    for (const auto& f : m_files) sizeOf.insert(f.absolutePath, f.size);
    QStringList paths = m_checkedPaths.values();
    if (paths.isEmpty() && m_table->selectionModel()) {
        for (const QModelIndex& idx : m_table->selectionModel()->selectedRows()) {
            auto* pathItem = m_table->item(idx.row(), kColPath);
            if (pathItem) paths << QDir::fromNativeSeparators(pathItem->text());
        }
    }
    paths.removeDuplicates();
    for (const QString& p : paths) selectedBytes += sizeOf.value(p, 0);
    if (m_selectedMetric) {
        m_selectedMetric->setText(n > 0
            ? tr("%1 · %2 项").arg(formatSize(selectedBytes)).arg(n)
            : QStringLiteral("--"));
    }
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
    m_headStatus->setText(tr("扫描中……"));

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
    filter.olderThanDays = m_ageCombo ? m_ageCombo->currentData().toInt() : 0;
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
            m_countMetric->setText(tr("%1 个").arg(m_files.size()));
            qint64 totalBytes = 0;
            for (const auto& f : m_files) totalBytes += f.size;
            if (m_totalMetric) m_totalMetric->setText(formatSize(totalBytes));
            if (m_exportBtn) m_exportBtn->setEnabled(!m_files.isEmpty());
            if (m_openBtn) m_openBtn->setEnabled(!m_files.isEmpty());
            m_headStatus->setText(tr("扫描完成"));
            m_deleteBtn->setEnabled(false); // 需勾选后才可删
            updateDeleteButtonState();
            const QString elapsed = elapsedMs >= 1000
                ? tr("%1 秒").arg(QString::number(elapsedMs / 1000.0, 'f', 1))
                : tr("%1 毫秒").arg(elapsedMs);
            m_summary->setText(tr("共 %1 个文件，合计 %2，耗时 %3")
                                   .arg(m_files.size()).arg(formatSize(totalBytes)).arg(elapsed));
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
        const bool locked = isUndeletableSystemFile(f.absolutePath);
        if (locked)
            m_checkedPaths.remove(f.absolutePath);

        auto* itCheck = new QTableWidgetItem;
        if (locked) {
            itCheck->setFlags(Qt::ItemIsSelectable);
            itCheck->setCheckState(Qt::Unchecked);
            itCheck->setToolTip(tr("系统锁定文件，无法删除"));
        } else {
            itCheck->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            itCheck->setCheckState(m_checkedPaths.contains(f.absolutePath) ? Qt::Checked
                                                                          : Qt::Unchecked);
        }
        auto* itDrive = new QTableWidgetItem(f.absolutePath.left(2).toUpper());
        auto* itSize = new SizeTableItem(f.size);
        auto* itName = new QTableWidgetItem(f.name);
        auto* itPath = new QTableWidgetItem(QDir::toNativeSeparators(f.absolutePath));
        auto* itTime = new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(f.lastModified).toString("yyyy-MM-dd HH:mm"));
        if (locked) {
            const QBrush dim(QColor(0x9C, 0xA3, 0xAF));
            QTableWidgetItem* cells[] = {itCheck, itDrive, itSize, itName, itPath, itTime};
            for (QTableWidgetItem* it : cells) {
                it->setForeground(dim);
                it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            }
            itName->setText(f.name + tr("（系统文件）"));
            itName->setToolTip(tr("pagefile / 休眠文件等由系统锁定，运行中无法删除"));
        }
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
        int checked = 0;
        if (m_table->rowCount() > 0) {
            for (int r = 0; r < m_table->rowCount(); ++r) {
                auto* it = m_table->item(r, kColCheck);
                if (it && it->checkState() == Qt::Checked) ++checked;
            }
            allChecked = checked == m_table->rowCount();
        }
        {
            QSignalBlocker block(m_headerCheck);
            if (checked == 0)
                m_headerCheck->setCheckState(Qt::Unchecked);
            else if (allChecked)
                m_headerCheck->setCheckState(Qt::Checked);
            else
                m_headerCheck->setCheckState(Qt::PartiallyChecked);
        }
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

    const auto reply = [&] {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("确认删除"));
        box.setText(tr("即将删除 %1 个文件（约 %2）。").arg(items.size()).arg(formatSize(bytes)));
        box.setInformativeText(tr("文件将移到回收站；系统保护路径会被跳过。此操作请确认无误。"));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        box.setWindowModality(Qt::ApplicationModal);
        if (auto* yes = box.button(QMessageBox::Yes)) yes->setText(tr("确认删除"));
        if (auto* no = box.button(QMessageBox::No)) no->setText(tr("取消"));
        return box.exec();
    }();
    if (reply != QMessageBox::Yes) return;

    m_scanBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
    m_openBtn->setEnabled(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_summary->setText(tr("正在删除 0/%1 …").arg(items.size()));

    (void)QtConcurrent::run([this, items]() {
        CleanerService svc;
        QObject::connect(&svc, &CleanerService::progress, this,
                         [this, total = items.size()](int percent, const QString& path) {
            const int done = qBound(0, (percent * total + 99) / 100, total);
            m_progress->setValue(qBound(0, percent, 100));
            m_summary->setText(tr("正在删除 %1/%2 … %3")
                                   .arg(done).arg(total).arg(QFileInfo(path).fileName()));
        },
                         Qt::QueuedConnection);
        int failed = 0;
        QObject::connect(&svc, &CleanerService::finished, this,
                         [&failed](qint64, int failedCount) { failed = failedCount; },
                         Qt::DirectConnection);
        const qint64 freed = svc.clean(items, true);
        QMetaObject::invokeMethod(this, [this, freed, failed, total = items.size()]() {
            m_progress->setValue(100);
            m_checkedPaths.clear();
            if (failed > 0 && freed <= 0) {
                m_summary->setText(tr("未能删除（%1 个失败）。pagefile.sys / 休眠文件等系统锁定文件无法删除，请在「系统属性 → 高级 → 性能」中调整虚拟内存。")
                                       .arg(failed));
                m_scanBtn->setEnabled(true);
                updateDeleteButtonState();
                return;
            }
            m_summary->setText(tr("已释放 %1（目标 %2 个，失败 %3），正在重新扫描列表……")
                                   .arg(formatSize(freed)).arg(total).arg(failed));
            LOG << "delete done freed=" << freed << " failed=" << failed << " requested=" << total;
            doScan();
        }, Qt::QueuedConnection);
    });
}

QString BigFilePage::pathAtRow(int row) const {
    if (!m_table || row < 0 || row >= m_table->rowCount())
        return {};
    auto* it = m_table->item(row, kColPath);
    return it ? QDir::fromNativeSeparators(it->text()) : QString();
}

void BigFilePage::openContainingFolder(const QString& path) const {
    if (path.isEmpty()) return;
    const QString native = QDir::toNativeSeparators(path);
    if (QFileInfo::exists(native)) {
        // 在资源管理器中定位并选中该文件
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), native});
        return;
    }
    const QString dir = QFileInfo(native).absolutePath();
    if (!dir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void BigFilePage::showFileDetails(const QString& path) const {
    if (path.isEmpty()) return;
    const QFileInfo fi(QDir::toNativeSeparators(path));
    QString attrs;
    if (fi.exists()) {
        QStringList bits;
        if (fi.isHidden()) bits << tr("隐藏");
        if (!fi.isWritable()) bits << tr("只读/不可写");
        if (fi.isSymLink()) bits << tr("符号链接");
        if (isUndeletableSystemFile(path)) bits << tr("系统锁定（不可删除）");
        attrs = bits.isEmpty() ? tr("普通文件") : bits.join(QStringLiteral(" · "));
    } else {
        attrs = tr("文件当前不存在（可能已删除或移走）");
    }

    const QString body = tr(
        "文件名：%1\n"
        "完整路径：%2\n"
        "大小：%3（%4 字节）\n"
        "修改时间：%5\n"
        "创建时间：%6\n"
        "属性：%7")
        .arg(fi.fileName(),
             QDir::toNativeSeparators(fi.absoluteFilePath()),
             formatSize(fi.exists() ? fi.size() : 0),
             QString::number(fi.exists() ? fi.size() : 0),
             fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
             fi.birthTime().isValid()
                 ? fi.birthTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                 : tr("未知"),
             attrs);

    QMessageBox box(const_cast<BigFilePage*>(this));
    box.setWindowTitle(tr("文件详情"));
    box.setIcon(QMessageBox::Information);
    box.setText(fi.fileName());
    box.setInformativeText(body);
    box.setStandardButtons(QMessageBox::Ok);
    auto* copyBtn = box.addButton(tr("复制路径"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == copyBtn)
        QApplication::clipboard()->setText(QDir::toNativeSeparators(fi.absoluteFilePath()));
}

void BigFilePage::showTableContextMenu(const QPoint& pos) {
    const QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid()) return;
    const int row = idx.row();
    m_table->selectRow(row);
    const QString path = pathAtRow(row);
    if (path.isEmpty()) return;

    const bool locked = isUndeletableSystemFile(path);
    const bool checked = m_checkedPaths.contains(path);
    auto* checkItem = m_table->item(row, kColCheck);
    const bool canCheck = checkItem && (checkItem->flags() & Qt::ItemIsUserCheckable);

    QMenu menu(this);
    QAction* actOpenFolder = menu.addAction(tr("打开所在文件夹"));
    QAction* actOpenFile = menu.addAction(tr("打开文件"));
    actOpenFile->setEnabled(!locked && QFileInfo::exists(QDir::toNativeSeparators(path)));
    menu.addSeparator();
    QAction* actDetails = menu.addAction(tr("查看文件详情"));
    QAction* actCopyPath = menu.addAction(tr("复制完整路径"));
    QAction* actCopyName = menu.addAction(tr("复制文件名"));
    menu.addSeparator();
    QAction* actToggle = menu.addAction(checked ? tr("取消勾选") : tr("勾选此文件"));
    actToggle->setEnabled(canCheck && !locked);
    QAction* actDelete = menu.addAction(tr("删除此文件…"));
    actDelete->setEnabled(!locked);
    if (locked) {
        menu.addSeparator();
        QAction* tip = menu.addAction(tr("系统锁定文件，无法删除"));
        tip->setEnabled(false);
    }

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == actOpenFolder) {
        openContainingFolder(path);
    } else if (chosen == actOpenFile) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::toNativeSeparators(path)));
    } else if (chosen == actDetails) {
        showFileDetails(path);
    } else if (chosen == actCopyPath) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        m_summary->setText(tr("已复制路径"));
    } else if (chosen == actCopyName) {
        QApplication::clipboard()->setText(QFileInfo(path).fileName());
        m_summary->setText(tr("已复制文件名"));
    } else if (chosen == actToggle && checkItem) {
        checkItem->setCheckState(checked ? Qt::Unchecked : Qt::Checked);
    } else if (chosen == actDelete) {
        m_checkedPaths.clear();
        m_checkedPaths.insert(path);
        // 同步当前页勾选显示
        for (int r = 0; r < m_table->rowCount(); ++r) {
            auto* it = m_table->item(r, kColCheck);
            auto* p = m_table->item(r, kColPath);
            if (!it || !p || !(it->flags() & Qt::ItemIsUserCheckable)) continue;
            const QString rowPath = QDir::fromNativeSeparators(p->text());
            it->setCheckState(rowPath == path ? Qt::Checked : Qt::Unchecked);
        }
        updateDeleteButtonState();
        doDelete();
    }
}

} // namespace DiskOrganizer
