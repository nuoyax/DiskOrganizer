#pragma once
#include "PageBase.h"
#include "TreemapWidget.h"
#include "models/FileInfo.h"
#include "services/SpaceAnalyzer.h"

#include <QList>
#include <QSet>
#include <atomic>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QStackedWidget;
class QScrollArea;
class QCheckBox;
class QComboBox;

namespace DiskOrganizer {

class PieChart;
class SegmentedBar;

class SpaceAnalyzerPage : public PageBase {
    Q_OBJECT
public:
    explicit SpaceAnalyzerPage(QWidget* parent = nullptr);
    void scanPath(const QString& path);

private slots:
    void doCleanSelected();
    void doExport();
    void cancelOrRescan();

private:
    void buildUi();
    void applyResults(const QList<QPair<QString, qint64>>& dirs,
                      const QList<TypeStat>& types, qint64 fileCount, qint64 ms);
    TreemapNode buildTree(const QList<QPair<QString, qint64>>& dirs, const QString& root) const;
    void rebuildFolderList();
    void updateBreadcrumb(const QString& path);
    void updateCleanButton();
    void resetScanUi();

    QLabel* m_titlePill = nullptr;
    QLabel* m_breadcrumb = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QPushButton* m_cleanBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QComboBox* m_depthCombo = nullptr;
    QProgressBar* m_progress = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    TreemapWidget* m_treemap = nullptr;
    QTreeWidget* m_listTree = nullptr;
    PieChart* m_pie = nullptr;
    SegmentedBar* m_typeBar = nullptr;
    QLabel* m_typeLegend = nullptr;
    QScrollArea* m_folderScroll = nullptr;
    QWidget* m_folderList = nullptr;
    QLabel* m_folderSummary = nullptr;
    QLabel* m_focusLabel = nullptr;

    QString m_rootPath;
    QList<QPair<QString, qint64>> m_dirs;
    QList<TypeStat> m_types;
    QList<FileInfo> m_topFiles; // 可选清理目标路径（目录）
    QSet<QString> m_checked;
    std::atomic<bool> m_cancelled{false};
    bool m_scanning = false;
};

} // namespace DiskOrganizer
