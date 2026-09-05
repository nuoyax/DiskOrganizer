#pragma once
#include "PageBase.h"
#include "models/FileInfo.h"

#include <QList>
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

private:
    void populateResults();
    void updateDeleteButtonState();

    SearchableComboBox* m_driveCombo = nullptr;
    QLineEdit* m_dirEdit = nullptr;     // 目录级扫描（空=整盘）
    SearchableComboBox* m_sizeCombo = nullptr;
    SearchableComboBox* m_extCombo = nullptr;
    QCheckBox* m_groupByDrive = nullptr;
    QCheckBox* m_headerCheck = nullptr; // 表头全选复选框
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_scanTimerLabel = nullptr; // 扫描中实时计时（已用时 X 秒）
    QProgressBar* m_progress = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;

    QList<FileInfo> m_files;
    std::atomic<bool> m_scanCancelled{false};   // 取消令牌
    std::atomic<qint64> m_lastScanElapsedMs{0}; // 上次扫描耗时（后台线程写、UI 读）
    bool m_scanning = false;
};

} // namespace DiskOrganizer
