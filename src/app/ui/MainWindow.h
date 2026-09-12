#pragma once
#include <QMainWindow>
#include <QTableWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>

namespace DiskOrganizer {

class CleanPage;
class DuplicatePage;
class DefragPage;
class SpaceAnalyzerPage;
class BigFilePage;
class PieChart;
class CleanPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshDisks();
    void openDiskInAnalyzer(int row, int column);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    struct BarFill { QFrame* fill; double ratio; };
    QList<BarFill> m_barFills;
    void buildUi();
    void buildMenus();
    QWidget* buildOverviewPage();

    QStackedWidget* m_stack = nullptr;
    QTableWidget* m_diskTable = nullptr;
    QLabel* m_overviewPill = nullptr; // 「N 卷已装载」pill
    PieChart* m_diskPie = nullptr;
    QLabel* m_poolTotal = nullptr;    // 存储池总使用率 “3.08 TB / 4.50 TB”
    QLabel* m_poolPill = nullptr;     // 「已用 68.4%」
    QLabel* m_pieLegend = nullptr;    // 环形图例（各盘已用）
    QLabel* m_cmpSubtitle = nullptr;  // 「N 卷在线分配情况」
    QVBoxLayout* m_barList = nullptr; // 驱动器进度条列表
    QLabel* m_barLegend = nullptr;    // 状态图例（彩色圆点富文本）
    CleanPage* m_cleanPage = nullptr;
    DuplicatePage* m_duplicatePage = nullptr;
    SpaceAnalyzerPage* m_analyzerPage = nullptr;
    DefragPage* m_defragPage = nullptr;
    BigFilePage* m_bigFilePage = nullptr;
};

} // namespace DiskOrganizer
