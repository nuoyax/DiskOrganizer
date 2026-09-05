#pragma once
#include "PageBase.h"

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QComboBox;
class QLineEdit;

namespace DiskOrganizer {

class DuplicatePage : public PageBase {
    Q_OBJECT
public:
    explicit DuplicatePage(QWidget* parent = nullptr);

private slots:
    void doFind();
    void keepOldest();
    void deleteSelected();

private:
    QLineEdit* m_pathEdit = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_summary = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_findBtn = nullptr;
    QPushButton* m_keepBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
};

} // namespace DiskOrganizer
