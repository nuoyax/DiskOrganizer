#include "Icons.h"
#include "DefragPage.h"
#include "ClusterMapWidget.h"
#include "Charts.h"
#include "FlatStyle.h"
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "services/DefragService.h"
#include "util/FileSystemUtil.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

namespace {
QFrame* metricBox(const QString& caption, QLabel** valueOut, QLabel** tagOut, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setProperty("class", "card");
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(4);
    auto* cap = new QLabel(caption, card);
    cap->setStyleSheet("font-size:11px; color:#6C7A77; background:transparent;");
    auto* val = new QLabel(QStringLiteral("--"), card);
    val->setStyleSheet("font-size:22px; font-weight:800; color:#181445; background:transparent;");
    v->addWidget(cap);
    v->addWidget(val);
    if (tagOut) {
        auto* tag = new QLabel(card);
        tag->setStyleSheet(
            "padding:2px 8px; border-radius:8px; background:#FEF2F2; color:#B91C1C;"
            "font-size:11px; font-weight:600;");
        tag->hide();
        v->addWidget(tag);
        *tagOut = tag;
    }
    *valueOut = val;
    return card;
}
} // namespace

DefragPage::DefragPage(QWidget* parent) : PageBase(parent) {
    buildUi();
    refreshDrives();
}

void DefragPage::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // 页头
    auto* head = new QHBoxLayout;
    auto* headCol = new QVBoxLayout;
    auto* title = new QLabel(tr("碎片整理"), this);
    title->setStyleSheet("font-size:24px; font-weight:800; color:#181445; background:transparent;");
    auto* subtitle = new QLabel(
        tr("分析磁盘簇分配状态并重排非连续数据块，优化读取性能与存储寿命"), this);
    subtitle->setStyleSheet("color:#6C7A77; background:transparent;");
    headCol->addWidget(title);
    headCol->addWidget(subtitle);
    head->addLayout(headCol, 1);

    m_driveBox = new QComboBox(this);
    m_driveBox->setMinimumWidth(280);
    m_trimPill = new QLabel(tr("TRIM 已激活"), this);
    m_trimPill->setStyleSheet(
        "padding:3px 10px; border-radius:12px; background:#ECFDF5; color:#047857;"
        "font-size:12px; font-weight:600;");
    auto* refreshBtn = new QPushButton(tr("刷新"), this);
    refreshBtn->setProperty("class", "secondary");
    m_analyzeBtn = new QPushButton(tr("分析"), this);
    m_analyzeBtn->setProperty("class", "secondary");
    m_defragBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::rocket), QColor("white")),
                                  tr("整理/优化"), this);
    head->addWidget(m_driveBox);
    head->addWidget(m_trimPill);
    head->addWidget(refreshBtn);
    head->addWidget(m_analyzeBtn);
    head->addWidget(m_defragBtn);
    root->addLayout(head);

    m_ssdHint = new QLabel(this);
    m_ssdHint->setStyleSheet("color:#92400E; background:transparent;");
    root->addWidget(m_ssdHint);

    // 指标卡
    auto* metrics = new QHBoxLayout;
    metrics->setSpacing(12);
    metrics->addWidget(metricBox(tr("磁盘碎片率"), &m_fragMetric, &m_fragTag, this), 1);
    metrics->addWidget(metricBox(tr("连续性状态"), &m_contMetric, nullptr, this), 1);
    metrics->addWidget(metricBox(tr("碎片文件数"), &m_filesMetric, nullptr, this), 1);
    metrics->addWidget(metricBox(tr("分析摘要"), &m_sectorMetric, nullptr, this), 1);
    root->addLayout(metrics);

    m_segBar = new SegmentedBar(this);
    m_segBar->setFixedHeight(14);
    root->addWidget(m_segBar);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->hide();
    root->addWidget(m_progress);

    // 簇图卡
    auto* mapCard = new QFrame(this);
    mapCard->setProperty("class", "card");
    auto* mv = new QVBoxLayout(mapCard);
    mv->setContentsMargins(14, 12, 14, 12);
    auto* mapTitle = new QLabel(tr("物理簇分布图（近似可视化）"), mapCard);
    mapTitle->setStyleSheet("font-weight:700; color:#181445; background:transparent;");
    mv->addWidget(mapTitle);
    m_clusterMap = new ClusterMapWidget(mapCard);
    m_clusterMap->setMinimumHeight(180);
    mv->addWidget(m_clusterMap, 1);
    auto* legend = new QLabel(
        tr("<span style='color:#3B82F6'>■</span> 顺序 &nbsp; "
           "<span style='color:#F43F5E'>■</span> 碎片 &nbsp; "
           "<span style='color:#14B8A6'>■</span> 系统/MFT &nbsp; "
           "<span style='color:#94A3B8'>■</span> 空闲"), mapCard);
    legend->setTextFormat(Qt::RichText);
    legend->setStyleSheet("color:#6C7A77; font-size:11px; background:transparent;");
    mv->addWidget(legend);
    root->addWidget(mapCard, 1);

    // 输出/文件表
    auto* tableCard = new QFrame(this);
    tableCard->setProperty("class", "card");
    auto* tv = new QVBoxLayout(tableCard);
    tv->setContentsMargins(12, 10, 12, 10);
    m_tabs = new QTabWidget(tableCard);
    m_fileTable = new QTableWidget(0, 3, m_tabs);
    m_fileTable->setHorizontalHeaderLabels({tr("时间"), tr("类型"), tr("输出")});
    m_fileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTable->setAlternatingRowColors(true);
    m_tabs->addTab(m_fileTable, tr("分析输出"));
    tv->addWidget(m_tabs);
    m_footer = new QLabel(tr("分析后将更新碎片率与簇图（近似比例，非真实物理簇）"), tableCard);
    m_footer->setStyleSheet("color:#6C7A77; font-size:12px; background:transparent;");
    tv->addWidget(m_footer);
    root->addWidget(tableCard, 1);

    connect(refreshBtn, &QPushButton::clicked, this, &DefragPage::refreshDrives);
    connect(m_analyzeBtn, &QPushButton::clicked, this, &DefragPage::doAnalyze);
    connect(m_defragBtn, &QPushButton::clicked, this, &DefragPage::doDefrag);
    connect(m_driveBox, &QComboBox::currentTextChanged, this, [this](const QString&) {
        updateDriveHint();
    });

    DiskOrganizer::applyCardShadows(this);
}

void DefragPage::refreshDrives() {
    m_driveBox->clear();
    for (const auto& d : enumerateDisks()) {
        const QString label = d.volumeLabel.isEmpty()
            ? tr("%1 (本地磁盘)").arg(d.driveLetter)
            : tr("%1 (%2)").arg(d.driveLetter, d.volumeLabel);
        m_driveBox->addItem(label, d.driveLetter);
    }
    updateDriveHint();
}

void DefragPage::updateDriveHint() {
    const QString drive = m_driveBox->currentData().toString();
    if (drive.isEmpty()) return;
    const bool isSsd = DefragService::isSsd(drive);
    m_defragBtn->setText(isSsd ? tr("优化 (TRIM)") : tr("立即整理"));
    m_trimPill->setVisible(isSsd);
    m_trimPill->setText(isSsd ? tr("TRIM 已激活") : tr("HDD 模式"));
    m_ssdHint->setText(isSsd
        ? tr("此驱动器是 SSD：无需传统碎片整理，建议使用「优化」发送 TRIM。")
        : tr("此驱动器是机械硬盘：可执行碎片整理以提升顺序读取性能。"));
}

void DefragPage::doAnalyze() {
    const QString drive = m_driveBox->currentData().toString();
    if (drive.isEmpty()) return;
    m_progress->show();
    m_analyzeBtn->setEnabled(false);
    (void)QtConcurrent::run([this, drive]() {
        const DefragResult r = DefragService::analyze(drive);
        QMetaObject::invokeMethod(this, [this, drive, r]() {
            applyAnalyzeResult(drive, r.success, r.output, r.fragmentedPercent);
        }, Qt::QueuedConnection);
    });
}

void DefragPage::applyAnalyzeResult(const QString& drive, bool success, const QString& output,
                                    int fragPercent) {
    m_progress->hide();
    m_analyzeBtn->setEnabled(true);

    // 从输出解析碎片百分比（若服务未填）
    int frag = fragPercent;
    if (frag <= 0) {
        static const QRegularExpression re(
            QStringLiteral("(\\d+)\\s*%"), QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(output);
        if (m.hasMatch()) frag = m.captured(1).toInt();
    }
    if (frag < 0) frag = 0;
    if (frag > 100) frag = 100;

    const double freeEst = 0.22;
    const double sysEst = 0.05;
    const double seq = qMax(0.0, 1.0 - frag / 100.0 - freeEst - sysEst);
    const double cont = qBound(0.0, 100.0 - frag * 0.8, 100.0);

    m_fragMetric->setText(QStringLiteral("%1%").arg(frag));
    m_contMetric->setText(QStringLiteral("%1%").arg(cont, 0, 'f', 1));
    m_filesMetric->setText(success ? tr("见输出") : tr("失败"));
    m_sectorMetric->setText(drive);
    if (frag >= 10) {
        m_fragTag->setText(tr("建议整理"));
        m_fragTag->show();
        m_fragMetric->setStyleSheet(
            "font-size:22px; font-weight:800; color:#B91C1C; background:transparent;");
    } else {
        m_fragTag->hide();
        m_fragMetric->setStyleSheet(
            "font-size:22px; font-weight:800; color:#047857; background:transparent;");
    }

    m_segBar->setSegments({
        {QColor(0x3B, 0x82, 0xF6), seq},
        {QColor(0xF4, 0x3F, 0x5E), frag / 100.0},
        {QColor(0x14, 0xB8, 0xA6), sysEst},
        {QColor(0xE2, 0xE8, 0xF0), freeEst},
    });
    m_clusterMap->setRatios(seq, frag / 100.0, sysEst, freeEst,
                            quint32(qHash(drive) ^ frag));

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const int row = m_fileTable->rowCount();
        m_fileTable->insertRow(row);
        m_fileTable->setItem(row, 0, new QTableWidgetItem(
            QDateTime::currentDateTime().toString("HH:mm:ss")));
        m_fileTable->setItem(row, 1, new QTableWidgetItem(tr("分析")));
        m_fileTable->setItem(row, 2, new QTableWidgetItem(line.trimmed()));
    }
    if (lines.isEmpty()) {
        const int row = m_fileTable->rowCount();
        m_fileTable->insertRow(row);
        m_fileTable->setItem(row, 0, new QTableWidgetItem(
            QDateTime::currentDateTime().toString("HH:mm:ss")));
        m_fileTable->setItem(row, 1, new QTableWidgetItem(tr("分析")));
        m_fileTable->setItem(row, 2, new QTableWidgetItem(
            success ? tr("分析完成") : tr("分析失败（可能需要管理员权限）")));
    }
    m_footer->setText(tr("碎片率约 %1% · 连续性约 %2% · 簇图为近似可视化")
                          .arg(frag).arg(cont, 0, 'f', 1));
}

void DefragPage::doDefrag() {
    const QString drive = m_driveBox->currentData().toString();
    if (drive.isEmpty()) return;
    if (QMessageBox::question(this, tr("确认"),
            tr("对 %1 执行%2？此操作可能耗时较长。")
                .arg(drive, m_defragBtn->text())) != QMessageBox::Yes)
        return;
    m_progress->show();
    m_defragBtn->setEnabled(false);
    m_analyzeBtn->setEnabled(false);
    (void)QtConcurrent::run([this, drive]() {
        const bool isSsd = DefragService::isSsd(drive);
        const DefragResult r = isSsd ? DefragService::optimize(drive)
                                     : DefragService::defrag(drive);
        QMetaObject::invokeMethod(this, [this, r]() {
            m_progress->hide();
            m_defragBtn->setEnabled(true);
            m_analyzeBtn->setEnabled(true);
            const int row = m_fileTable->rowCount();
            m_fileTable->insertRow(row);
            m_fileTable->setItem(row, 0, new QTableWidgetItem(
                QDateTime::currentDateTime().toString("HH:mm:ss")));
            m_fileTable->setItem(row, 1, new QTableWidgetItem(tr("执行")));
            m_fileTable->setItem(row, 2, new QTableWidgetItem(
                r.success ? tr("完成") : tr("失败（需要管理员权限）")));
            m_footer->setText(r.success ? tr("操作完成") : tr("操作失败"));
        }, Qt::QueuedConnection);
    });
}

} // namespace DiskOrganizer
