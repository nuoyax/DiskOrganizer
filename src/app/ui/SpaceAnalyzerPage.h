#pragma once
#include "PageBase.h"

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace DiskOrganizer {

class SpaceAnalyzerPage : public PageBase {
    Q_OBJECT
public:
    explicit SpaceAnalyzerPage(QWidget* parent = nullptr);
    void scanPath(const QString& path);

private:
    QLineEdit* m_pathEdit = nullptr;
    QTreeWidget* m_dirTree = nullptr;    // 目录排行
    QTreeWidget* m_typeTree = nullptr;   // 扩展名分布
    QLabel* m_summary = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_scanBtn = nullptr;
};

} // namespace DiskOrganizer
