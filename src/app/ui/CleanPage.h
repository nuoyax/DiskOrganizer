#pragma once
#include "PageBase.h"
#include "services/CleanerService.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace DiskOrganizer {

class PieChart;

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
    static QString categoryDisplayName(CleanCategory cat);

    QTreeWidget* m_tree = nullptr;           // 类别 → 文件 两级树
    QCheckBox* m_catChecks[int(CleanCategory::CustomRules) + 1] = {};
    QLabel* m_summary = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QPushButton* m_cleanBtn = nullptr;
    PieChart* m_pie = nullptr;
    QList<CleanItem> m_items;
    bool m_updating = false;                 // 防递归
    bool m_recycleBin = true;
};

} // namespace DiskOrganizer
