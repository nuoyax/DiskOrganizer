#pragma once
#include <QWidget>

// 扁平化设计公共样式与自绘图表控件
namespace DiskOrganizer {

// 全局扁平化 QSS（app.setFont 后由 main.cpp 设置到 QApplication）
inline const char* flatStyleSheet() {
    return R"(
QMainWindow, QDialog { background: #F5F6FA; }
QWidget { font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif; font-size: 13px; color: #2D3436; }

QTabWidget::pane { border: none; top: -1px; background: #F5F6FA; }
QTabBar::tab {
    background: transparent; color: #636E88; padding: 10px 22px;
    border: none; border-bottom: 3px solid transparent; font-weight: 600;
    margin-right: 4px;
}
QTabBar::tab:selected { color: #2F6FED; border-bottom: 3px solid #2F6FED; }
QTabBar::tab:hover:!selected { color: #2F6FED; background: #EDF1FB; }

QPushButton {
    background: #2F6FED; color: white; border: none; border-radius: 6px;
    padding: 8px 20px; font-weight: 600;
}
QPushButton:hover { background: #1E5BD6; }
QPushButton:pressed { background: #174BB8; }
QPushButton:disabled { background: #C3CEE8; color: #EFF2FA; }
QPushButton[class="secondary"] {
    background: #FFFFFF; color: #2F6FED; border: 1.5px solid #C9D6F5;
}
QPushButton[class="secondary"]:hover { background: #EDF1FB; }
QPushButton[class="danger"] { background: #E5484D; }
QPushButton[class="danger"]:hover { background: #CE3B40; }

QLineEdit, QComboBox {
    background: #FFFFFF; border: 1.5px solid #D7DEEC; border-radius: 6px;
    padding: 7px 12px; selection-background-color: #2F6FED;
}
QLineEdit:focus, QComboBox:focus { border-color: #2F6FED; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 18px; height: 18px; border: 1.5px solid #C3CEE8; border-radius: 4px;
    background: white;
}
QCheckBox::indicator:checked { background: #2F6FED; border-color: #2F6FED;
    image: none; }

QListWidget, QTreeWidget, QTableWidget {
    background: #FFFFFF; border: 1.5px solid #E3E8F3; border-radius: 8px;
    padding: 4px; outline: none;
}
QListWidget::item, QTreeWidget::item { padding: 6px 8px; border-radius: 5px; }
QListWidget::item:selected, QTreeWidget::item:selected {
    background: #E8F0FE; color: #1D4FD8;
}
QListWidget::item:hover, QTreeWidget::item:hover { background: #F0F4FD; }

QHeaderView::section {
    background: #F0F3FA; color: #636E88; border: none;
    padding: 8px 10px; font-weight: 600;
}

QProgressBar {
    background: #E8ECF7; border: none; border-radius: 5px; height: 10px;
    text-align: center; color: transparent;
}
QProgressBar::chunk { background: #2F6FED; border-radius: 5px; }

QStatusBar { background: transparent; color: #8891A8; }
QMenuBar { background: #FFFFFF; border-bottom: 1px solid #E3E8F3; }
QMenuBar::item { padding: 8px 14px; border-radius: 5px; }
QMenuBar::item:selected { background: #EDF1FB; color: #2F6FED; }

QSplitter::handle { background: #E3E8F3; width: 2px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #C9D2E8; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #A8B5D6; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #C9D2E8; border-radius: 4px; min-width: 30px; }
)";
}

} // namespace DiskOrganizer
