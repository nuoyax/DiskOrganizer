#pragma once
#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;

namespace DiskOrganizer {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void save();
    void addExclude();
    void removeExclude();

private:
    QListWidget* m_excludeList = nullptr;
    QLineEdit* m_excludeEdit = nullptr;
    QCheckBox* m_recycleCheck = nullptr;
    QCheckBox* m_symlinkCheck = nullptr;
};

} // namespace DiskOrganizer
