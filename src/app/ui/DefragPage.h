#pragma once
#include "PageBase.h"

class QLabel;
class QComboBox;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace DiskOrganizer {

class ClusterMapWidget;
class SegmentedBar;

class DefragPage : public PageBase {
    Q_OBJECT
public:
    explicit DefragPage(QWidget* parent = nullptr);

private slots:
    void refreshDrives();
    void doAnalyze();
    void doDefrag();

private:
    void buildUi();
    void updateDriveHint();
    void applyAnalyzeResult(const QString& drive, bool success, const QString& output,
                            int fragPercent);

    QComboBox* m_driveBox = nullptr;
    QLabel* m_trimPill = nullptr;
    QLabel* m_ssdHint = nullptr;
    QLabel* m_fragMetric = nullptr;
    QLabel* m_contMetric = nullptr;
    QLabel* m_filesMetric = nullptr;
    QLabel* m_sectorMetric = nullptr;
    QLabel* m_fragTag = nullptr;
    SegmentedBar* m_segBar = nullptr;
    ClusterMapWidget* m_clusterMap = nullptr;
    QTableWidget* m_fileTable = nullptr;
    QTabWidget* m_tabs = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_analyzeBtn = nullptr;
    QPushButton* m_defragBtn = nullptr;
    QLabel* m_footer = nullptr;
};

} // namespace DiskOrganizer
