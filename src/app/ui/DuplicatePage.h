#pragma once
#include "PageBase.h"

#include <atomic>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QComboBox;
class QButtonGroup;

namespace DiskOrganizer {

class SearchableComboBox;

class DuplicatePage : public PageBase {
    Q_OBJECT
public:
    explicit DuplicatePage(QWidget* parent = nullptr);

private slots:
    void doFind();
    void keepOldest();
    void keepNewest();
    void keepShortestPath();
    void deleteSelected();

private:
    void resetUiAfterCancel();
    void rebuildGroupCards();
    void updateFooter();
    QStringList selectedPaths() const;
    bool passTypeFilter(const QString& path) const;
    bool passSizeFilter(qint64 size) const;

    SearchableComboBox* m_driveCombo = nullptr;
    QComboBox* m_sizeFilter = nullptr;
    QButtonGroup* m_typeGroup = nullptr;
    QPushButton* m_findBtn = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_cardHost = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_headStatus = nullptr;
    QLabel* m_groupsMetric = nullptr;
    QLabel* m_wastedMetric = nullptr;
    QPushButton* m_keepOldestBtn = nullptr;
    QPushButton* m_keepNewestBtn = nullptr;
    QPushButton* m_keepShortBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    std::atomic<bool> m_cancelled{false};
    bool m_finding = false;

    struct DupRow {
        QString path;
        qint64 size = 0;
        qint64 modified = 0;
        bool keepSuggested = false;
        bool checked = false;
    };
    struct DupGroupUi {
        QString name;
        QString hashPrefix;
        qint64 fileSize = 0;
        qint64 wasted = 0;
        QList<DupRow> rows;
    };
    QList<DupGroupUi> m_groups;
};

} // namespace DiskOrganizer
