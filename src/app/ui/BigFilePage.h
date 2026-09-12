#pragma once
#include "PageBase.h"
#include "models/FileInfo.h"

#include <QList>
#include <QSet>
#include <atomic>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QCheckBox;

namespace DiskOrganizer {

class SearchableComboBox;

// 大文件扫描清理页：按磁盘扫描 → 结果按磁盘分组展示，列可自由排序
class BigFilePage : public PageBase {
    Q_OBJECT
public:
    explicit BigFilePage(QWidget* parent = nullptr);

private slots:
    void doScan();
    void doDelete();
    void showTableContextMenu(const QPoint& pos);

private:
    void populateResults();
    void renderPage();          // 渲染当前页切片（m_files → 表格）
    int totalPages() const;
    void updateDeleteButtonState();
    void fitColumnsToContents(); // 按当前页内容估算列宽
    QString pathAtRow(int row) const;
    void openContainingFolder(const QString& path) const;
    void showFileDetails(const QString& path) const;

    SearchableComboBox* m_driveCombo = nullptr;
    QLineEdit* m_dirEdit = nullptr;     // 目录级扫描（空=整盘）
    SearchableComboBox* m_sizeCombo = nullptr;
    SearchableComboBox* m_extCombo = nullptr;
    SearchableComboBox* m_ageCombo = nullptr;
    QCheckBox* m_groupByDrive = nullptr;
    QCheckBox* m_headerCheck = nullptr; // 表头全选复选框
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_headStatus = nullptr;   // 页头状态 pill
    QLabel* m_countMetric = nullptr;    // 发现大文件数
    QLabel* m_totalMetric = nullptr;    // 累计占用
    QLabel* m_selectedMetric = nullptr; // 已选容量
    QLabel* m_scanTimerLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_openBtn = nullptr;

    QList<FileInfo> m_files;

    // 分页：每页条数可切换（20 跳页 / 100 滚动），勾选按路径跨页保留
    QComboBox* m_pageSizeCombo = nullptr;
    QLabel* m_pageLabel = nullptr;
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    int m_pageSize = 20;
    int m_currentPage = 0;
    QSet<QString> m_checkedPaths;   // 已勾选文件的绝对路径（跨页保留）

    std::atomic<bool> m_scanCancelled{false};   // 取消令牌
    std::atomic<qint64> m_lastScanElapsedMs{0}; // 上次扫描耗时（后台线程写、UI 读）
    bool m_scanning = false;
};

} // namespace DiskOrganizer
