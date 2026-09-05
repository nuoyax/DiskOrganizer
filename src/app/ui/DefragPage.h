#pragma once
#include "PageBase.h"

class QLabel;
class QComboBox;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace DiskOrganizer {

class DefragPage : public PageBase {
    Q_OBJECT
public:
    explicit DefragPage(QWidget* parent = nullptr);

private slots:
    void refreshDrives();
    void doAnalyze();
    void doDefrag();

private:
    QComboBox* m_driveBox = nullptr;
    QTableWidget* m_output = nullptr;
    QLabel* m_ssdHint = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_analyzeBtn = nullptr;
    QPushButton* m_defragBtn = nullptr;
};

} // namespace DiskOrganizer
