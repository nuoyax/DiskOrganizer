#pragma once
#include <QMainWindow>

namespace DiskOrganizer {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void buildMenus();
    void loadDisks();
};

} // namespace DiskOrganizer
