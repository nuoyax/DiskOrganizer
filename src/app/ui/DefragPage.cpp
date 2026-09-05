#include "Icons.h"
#include "DefragPage.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include "services/DefragService.h"
#include "util/FileSystemUtil.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

DefragPage::DefragPage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("驱动器："), this));
    m_driveBox = new QComboBox(this);
    top->addWidget(m_driveBox);
    auto* refreshBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::refresh), QColor(0x2F,0x6F,0xED)), tr("刷新"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &DefragPage::refreshDrives);
    top->addWidget(refreshBtn);
    m_analyzeBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")), tr("分析"), this);
    m_defragBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::rocket), QColor("white")), tr("整理/优化"), this);
    top->addWidget(m_analyzeBtn);
    top->addWidget(m_defragBtn);
    top->addStretch();
    layout->addLayout(top);

    m_ssdHint = new QLabel(this);
    m_ssdHint->setStyleSheet("color: #B26A00;");
    layout->addWidget(m_ssdHint);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->hide();
    layout->addWidget(m_progress);

    m_output = new QTableWidget(0, 2, this);
    m_output->setHorizontalHeaderLabels({tr("时间"), tr("输出")});
    m_output->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_output->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_output, 1);

    connect(m_driveBox, &QComboBox::currentTextChanged, this, [this](const QString& drive) {
        if (drive.isEmpty()) return;
        const bool isSsd = DefragService::isSsd(drive);
        m_defragBtn->setText(isSsd ? tr("优化 (TRIM)") : tr("整理"));
        m_ssdHint->setText(isSsd
            ? tr("此驱动器是 SSD：无需碎片整理，建议使用“优化”（发送 TRIM 指令）。")
            : tr("此驱动器是机械硬盘：可执行碎片整理以提升性能。"));
    });
    connect(m_analyzeBtn, &QPushButton::clicked, this, &DefragPage::doAnalyze);
    connect(m_defragBtn, &QPushButton::clicked, this, &DefragPage::doDefrag);
    refreshDrives();
}

void DefragPage::refreshDrives() {
    m_driveBox->clear();
    for (const auto& d : enumerateDisks())
        m_driveBox->addItem(d.driveLetter);
}

void DefragPage::doAnalyze() {
    const QString drive = m_driveBox->currentText();
    m_progress->show();
    m_analyzeBtn->setEnabled(false);
    (void)QtConcurrent::run([this, drive]() {
        const DefragResult r = DefragService::analyze(drive);
        QMetaObject::invokeMethod(this, [this, r]() {
            m_progress->hide();
            m_analyzeBtn->setEnabled(true);
            const int row = m_output->rowCount();
            m_output->insertRow(row);
            m_output->setItem(row, 0, new QTableWidgetItem(
                QDateTime::currentDateTime().toString("HH:mm:ss")));
            m_output->setItem(row, 1, new QTableWidgetItem(
                r.output.split('\n').value(1, tr("分析完成"))));
        }, Qt::QueuedConnection);
    });
}

void DefragPage::doDefrag() {
    const QString drive = m_driveBox->currentText();
    if (QMessageBox::question(this, tr("确认"),
            tr("对 %1 执行%2？此操作可能耗时较长。")
                .arg(drive, m_defragBtn->text())) != QMessageBox::Yes) return;
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
            const int row = m_output->rowCount();
            m_output->insertRow(row);
            m_output->setItem(row, 0, new QTableWidgetItem(
                QDateTime::currentDateTime().toString("HH:mm:ss")));
            m_output->setItem(row, 1, new QTableWidgetItem(
                r.success ? tr("完成") : tr("失败（需要管理员权限）")));
        }, Qt::QueuedConnection);
    });
}

} // namespace DiskOrganizer
