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

    QComboBox* m_driveCombo = nullptr;
    QLineEdit* m_minSizeEdit = nullptr;
    SearchableComboBox* m_extCombo = nullptr;
    QCheckBox* m_groupByDrive = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;

    QList<FileInfo> m_files;
    std::atomic<bool> m_scanCancelled{false};   // 取消令牌
    bool m_scanning = false;
};

} // namespace DiskOrganizer
