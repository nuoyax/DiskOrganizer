#pragma once
#include <QDialog>

namespace DiskOrganizer {
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
};
} // namespace DiskOrganizer
