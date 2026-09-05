#pragma once
#include "PageBase.h"

class QProgressBar;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;

namespace DiskOrganizer {

class CleanPage : public PageBase {
    Q_OBJECT
public:
    explicit CleanPage(QWidget* parent = nullptr);
    void openSettings();

private slots:
    void doScan();
    void doClean();
    void onScanProgress(int percent, const QString& currentPath);

private:
    QListWidget* m_list = nullptr;
    QLabel* m_summary = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_scanBtn = nullptr;
    QPushButton* m_cleanBtn = nullptr;
    bool m_recycleBin = true;
};

} // namespace DiskOrganizer
