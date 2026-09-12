#pragma once
#include "PageBase.h"
#include "services/CleanerService.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QHBoxLayout;
class QFrame;

namespace DiskOrganizer {

class PieChart;

// 按 clean-light 稿重构：hero 汇总卡 + 分类卡片 grid + 明细树卡 + 底部操作条
class CleanPage : public PageBase {
    Q_OBJECT
public:
    explicit CleanPage(QWidget* parent = nullptr);
    void openSettings();

private slots:
    void doScan();
    void doClean();
    void onItemChanged(QTreeWidgetItem* item, int column);

private:
    void updateSummary();
    void rebuildCategoryCards();
    static QString categoryDisplayName(CleanCategory cat);

    // 分类卡片（按 CleanCategory 索引）
    struct CatCard {
        QFrame* card = nullptr;
        QCheckBox* check = nullptr;
        QLabel* countLabel = nullptr;   // “8,421 项 · …”
        QLabel* tagLabel = nullptr;     // 建议清理 / 谨慎清理
        QLabel* sizeLabel = nullptr;    // 大数字
    };
    CatCard m_cards[int(CleanCategory::CustomRules) + 1];

    QTreeWidget* m_tree = nullptr;           // 类别 → 文件 两级树（明细）
    QLabel* m_headStatus = nullptr;          // 页头状态 pill 文本
    QLabel* m_heroState = nullptr;           // 扫描完成 · 发现 N 项
    QLabel* m_heroTotal = nullptr;           // 38.60 大数字
    QLabel* m_summary = nullptr;             // 底部操作条汇总
    QProgressBar* m_progress = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QPushButton* m_cleanBtn = nullptr;
    PieChart* m_pie = nullptr;
    QList<CleanItem> m_items;
    bool m_updating = false;                 // 防递归
    bool m_recycleBin = true;
};

} // namespace DiskOrganizer
