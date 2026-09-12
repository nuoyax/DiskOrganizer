#pragma once
#include "PageBase.h"

#include <atomic>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace DiskOrganizer {

class SearchableComboBox;

// 重复文件查找页：指定盘符整卷扫描（NTFS 走 MFT 快速路径）
// 按 duplicate-light 稿重构：页头 + 指标卡 + 过滤行 + 组卡片列表 + 底部操作条
class DuplicatePage : public PageBase {
    Q_OBJECT
public:
    explicit DuplicatePage(QWidget* parent = nullptr);

private slots:
    void doFind();
    void keepOldest();
    void deleteSelected();

private:
    void resetUiAfterCancel();

    SearchableComboBox* m_driveCombo = nullptr;
    QPushButton* m_findBtn = nullptr;
    QListWidget* m_list = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_headStatus = nullptr;   // 页头状态 pill
    QLabel* m_groupsMetric = nullptr; // 指标卡：重复组
    QLabel* m_wastedMetric = nullptr; // 指标卡：可释放
    QPushButton* m_keepBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    std::atomic<bool> m_cancelled{false};
    bool m_finding = false;
};

} // namespace DiskOrganizer
