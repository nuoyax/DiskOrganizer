#pragma once
#include <QWidget>

// 扁平化设计 QSS —— 参考主流清理工具（CleanMaster / Wise / CCleaner 新版）的现代风格：
// 左侧导航 + 卡片式内容区 + 主色强调 + 大圆角 + 柔和阴影色
namespace DiskOrganizer {

inline const char* flatStyleSheet() {
    return R"(
/* ===== 全局 ===== */
QMainWindow, QDialog { background: #F7F8FC; }
QWidget {
    font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
    font-size: 13px; color: #23262F;
}

/* ===== 侧边导航 ===== */
#sidebar {
    background: #FFFFFF;
    border-right: 1px solid #ECF0F7;
}
#appTitle {
    font-size: 17px; font-weight: 700; color: #23262F;
    padding: 18px 20px 4px 20px; background: transparent;
}
#appSubtitle {
    font-size: 11px; color: #9AA3B5; padding: 0 20px 14px 20px;
    background: transparent;
}
QPushButton.navBtn {
    background: transparent; color: #5A6478; border: none;
    border-radius: 10px; padding: 11px 16px; margin: 2px 10px;
    font-size: 13.5px; font-weight: 600; text-align: left;
}
QPushButton.navBtn:hover { background: #F0F4FD; color: #2F6FED; }
QPushButton.navBtn:checked {
    background: #EAF0FE; color: #2F6FED;
}

/* ===== 内容卡片 ===== */
#contentArea { background: #F7F8FC; }
QFrame.card {
    background: #FFFFFF; border: 1px solid #ECF0F7; border-radius: 12px;
}
QLabel.cardTitle { font-size: 14px; font-weight: 700; color: #23262F; background: transparent; }
QLabel.cardSub   { font-size: 12px; color: #9AA3B5; background: transparent; }
QLabel.hint      { color: #9AA3B5; background: transparent; }
QLabel.metric    { font-size: 26px; font-weight: 800; color: #2F6FED; background: transparent; }

/* ===== Tab（页内小 tab）===== */
QTabWidget::pane { border: none; background: transparent; top: -1px; }
QTabBar::tab {
    background: transparent; color: #8A93A8; padding: 8px 18px;
    border: none; border-bottom: 2.5px solid transparent; font-weight: 600;
    margin-right: 2px;
}
QTabBar::tab:selected { color: #2F6FED; border-bottom: 2.5px solid #2F6FED; }
QTabBar::tab:hover:!selected { color: #2F6FED; background: #EDF1FB; }

/* ===== 按钮 ===== */
QPushButton {
    background: #2F6FED; color: white; border: none; border-radius: 8px;
    padding: 9px 22px; font-weight: 600;
}
QPushButton:hover { background: #1E5BD6; }
QPushButton:pressed { background: #174BB8; }
QPushButton:disabled { background: #C9D4EF; color: #F2F5FC; }
QPushButton[class="secondary"] {
    background: #FFFFFF; color: #2F6FED; border: 1.5px solid #D5DEF5;
}
QPushButton[class="secondary"]:hover { background: #F0F4FD; border-color: #2F6FED; }
QPushButton[class="danger"]  { background: #E5484D; }
QPushButton[class="danger"]:hover  { background: #CE3B40; }
QPushButton[class="ghost"] {
    background: transparent; color: #5A6478; border: none; padding: 8px 12px;
}
QPushButton[class="ghost"]:hover { background: #EEF1F8; color: #23262F; }
QPushButton:flat { icon-size: 18px 18px; }

/* ===== 输入 ===== */
QLineEdit, QComboBox {
    background: #FFFFFF; border: 1.5px solid #DDE3F0; border-radius: 8px;
    padding: 8px 12px; selection-background-color: #2F6FED;
}
QLineEdit:focus, QComboBox:focus { border-color: #2F6FED; }
QComboBox::drop-down { border: none; width: 0; }   /* 箭头由代码绘制（TrailingAction） */
QComboBox::down-arrow { width: 0; height: 0; }
QComboBox QAbstractItemView {
    background: #FFFFFF; border: 1px solid #ECF0F7; border-radius: 8px;
    selection-background-color: #EAF0FE; selection-color: #2F6FED;
    outline: none;
}

/* ===== 复选框 ===== */
QCheckBox { spacing: 8px; background: transparent; }
QCheckBox::indicator {
    width: 18px; height: 18px; border: 1.5px solid #C3CEE8; border-radius: 5px;
    background: white;
}
QCheckBox::indicator:hover { border-color: #2F6FED; }
QCheckBox::indicator:checked {
    background: #2F6FED; border-color: #2F6FED;
    image: url(:/qt-project.org/styles/commonstyle/images/standardbutton-apply-16.png);
}

/* ===== 列表/树/表 ===== */
QListWidget, QTreeWidget, QTableWidget {
    background: #FFFFFF; border: 1px solid #ECF0F7; border-radius: 10px;
    padding: 6px; outline: none; alternate-background-color: #FAFBFE;
}
QListWidget::item, QTreeWidget::item { padding: 6px 8px; border-radius: 6px; }
QListWidget::item:selected, QTreeWidget::item:selected {
    background: #EAF0FE; color: #1D4FD8;
}
QListWidget::item:hover, QTreeWidget::item:hover { background: #F0F4FD; }
QTableWidget {
    gridline-color: transparent;
}
QTableWidget::item { padding: 4px 8px; border-radius: 4px; }
QTableWidget::item:selected { background: #EAF0FE; color: #1D4FD8; }

/* ===== 表头 ===== */
QHeaderView::section {
    background: #FAFBFE; color: #8A93A8; border: none;
    border-bottom: 1px solid #ECF0F7;
    padding: 9px 10px; font-weight: 600; font-size: 12px;
}
QTableCornerButton::section { background: #FAFBFE; border: none; }

/* ===== 进度条 ===== */
QProgressBar {
    background: #E9EDF7; border: none; border-radius: 5px; height: 10px;
    text-align: center; color: transparent;
}
QProgressBar::chunk { background: #2F6FED; border-radius: 5px; }

/* ===== 菜单/状态栏 ===== */
QStatusBar { background: transparent; color: #9AA3B5; }
QMenuBar { background: #FFFFFF; border-bottom: 1px solid #ECF0F7; }
QMenuBar::item { padding: 8px 14px; border-radius: 6px; }
QMenuBar::item:selected { background: #F0F4FD; color: #2F6FED; }
QMenu {
    background: #FFFFFF; border: 1px solid #ECF0F7; border-radius: 10px; padding: 6px;
}
QMenu::item { padding: 8px 24px; border-radius: 6px; }
QMenu::item:selected { background: #EAF0FE; color: #2F6FED; }
QMenu::separator { height: 1px; background: #ECF0F7; margin: 4px 8px; }

/* ===== 滚动条 ===== */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #D3DBEC; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #B4C0DC; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #D3DBEC; border-radius: 4px; min-width: 30px; }
QScrollBar::handle:horizontal:hover { background: #B4C0DC; }

/* ===== 分割器 ===== */
QSplitter::handle { background: #ECF0F7; width: 2px; }

/* ===== 消息框 ===== */
QMessageBox { background: #FFFFFF; }
QMessageBox QLabel { background: transparent; }
)";
}

} // namespace DiskOrganizer
