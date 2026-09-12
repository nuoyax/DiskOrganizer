#pragma once
#include <QMainWindow>
#include <QTableWidget>
#include <QStackedWidget>
#include <QLabel>

namespace DiskOrganizer {

class CleanPage;
class DuplicatePage;
class DefragPage;
class SpaceAnalyzerPage;
class BigFilePage;
class PieChart;
class BarChart;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshDisks();
    void openDiskInAnalyzer(int row, int column);

private:
    void buildUi();
    void buildMenus();
    QWidget* buildOverviewPage();

    QStackedWidget* m_stack = nullptr;
    QTableWidget* m_diskTable = nullptr;
    QLabel* m_overviewPill = nullptr; // 「N 卷已装载」pill
    PieChart* m_diskPie = nullptr;
    BarChart* m_diskBar = nullptr;
    CleanPage* m_cleanPage = nullptr;
    DuplicatePage* m_duplicatePage = nullptr;
    SpaceAnalyzerPage* m_analyzerPage = nullptr;
    DefragPage* m_defragPage = nullptr;
    BigFilePage* m_bigFilePage = nullptr;
};

} // namespace DiskOrganizer
