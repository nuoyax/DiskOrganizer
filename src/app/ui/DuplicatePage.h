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
    QListWidget* m_list = nullptr;
    QLabel* m_summary = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_findBtn = nullptr;
    QPushButton* m_keepBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    std::atomic<bool> m_cancelled{false}; // 扫描取消令牌
    bool m_finding = false;
};

} // namespace DiskOrganizer
