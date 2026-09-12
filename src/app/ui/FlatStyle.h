#pragma once
#include <QWidget>
#include <QString>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>

// 浅色风格 QSS —— 参照 stitch_out 浅色稿（STYLE_B）：
// 暖白画布 #FAFAF7 + 深靛蓝侧栏 #1E1B4B + 白卡软阴影 + 12px 圆角
// + teal/indigo 双强调（#006B5F / #4B41E1）+ pill 按钮
namespace DiskOrganizer {

namespace {

enum class IndicatorKind { Check, Partial };

// 写出勾选/半选 PNG（QSS image 用，静态 Qt 无内置勾选图）
inline QString ensureIndicatorIcon(const char* name, IndicatorKind kind) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + QLatin1String(name);
    {
        QPixmap pm(28, 28); // 2x @14px
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        if (kind == IndicatorKind::Check) {
            QPainterPath mark;
            mark.moveTo(5.0, 14.5);
            mark.lineTo(11.0, 20.5);
            mark.lineTo(23.0, 7.5);
            mark.lineTo(20.5, 5.5);
            mark.lineTo(11.0, 16.0);
            mark.lineTo(7.5, 12.5);
            mark.closeSubpath();
            painter.drawPath(mark);
        } else {
            painter.drawRoundedRect(QRectF(6, 12, 16, 4), 2, 2);
        }
        painter.end();
        pm.save(path, "PNG");
    }
    QString url = path;
    url.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return url;
}

inline QString checkMarkUrl() {
    static const QString u = ensureIndicatorIcon("cb_check.png", IndicatorKind::Check);
    return u;
}

inline QString partialMarkUrl() {
    static const QString u = ensureIndicatorIcon("cb_partial.png", IndicatorKind::Partial);
    return u;
}

} // namespace

// 表头全选等局部 QSS 复用
inline QString checkBoxIndicatorStyle() {
    return QStringLiteral(
        "QCheckBox{background:transparent;spacing:0;margin:0;padding:0;}"
        "QCheckBox::indicator{width:14px;height:14px;border:1px solid #6C7A77;"
        "border-radius:3px;background:#FFFFFF;}"
        "QCheckBox::indicator:hover{border-color:#006B5F;}"
        "QCheckBox::indicator:checked{background:#006B5F;border-color:#006B5F;"
        "image:url(%1);}"
        "QCheckBox::indicator:indeterminate{background:#006B5F;border-color:#006B5F;"
        "image:url(%2);}").arg(checkMarkUrl(), partialMarkUrl());
}

inline QString flatStyleSheet() {
    const QString check = checkMarkUrl();
    const QString partial = partialMarkUrl();
    return QStringLiteral(R"(
/* ===== 全局 ===== */
QMainWindow, QDialog { background: #FAFAF7; }
QWidget {
    font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
    font-size: 13px; color: #181445;
}

/* ===== 侧边导航（深靛蓝）===== */
#sidebar {
    background: #1E1B4B;
    border-right: none;
}
#appTitle {
    font-size: 17px; font-weight: 700; color: #FFFFFF;
    padding: 18px 20px 4px 20px; background: transparent;
}
#appSubtitle {
    font-size: 11px; color: #A5A3C9; padding: 0 20px 14px 20px;
    background: transparent;
}
QPushButton.navBtn {
    background: transparent; color: #C5C2E8; border: none;
    border-radius: 10px; padding: 11px 16px; margin: 2px 10px;
    font-size: 13.5px; font-weight: 600; text-align: left;
}
QPushButton.navBtn:hover { background: #2E2A63; color: #FFFFFF; }
QPushButton.navBtn:checked {
    background: #37326E; color: #FFFFFF;
}

/* ===== 内容卡片 ===== */
#contentArea { background: #FAFAF7; }
QFrame.card {
    background: #FFFFFF; border: 1px solid #EAE6F4; border-radius: 12px;
}
QLabel.cardTitle { font-size: 14px; font-weight: 700; color: #181445; background: transparent; }
QLabel.cardSub   { font-size: 12px; color: #6C7A77; background: transparent; }
QLabel.hint      { color: #6C7A77; background: transparent; }
QLabel.metric    { font-size: 26px; font-weight: 800; color: #4B41E1; background: transparent; }

/* ===== Tab（页内小 tab）===== */
QTabWidget::pane { border: none; background: transparent; top: -1px; }
QTabBar::tab {
    background: transparent; color: #6C7A77; padding: 8px 18px;
    border: none; border-bottom: 2.5px solid transparent; font-weight: 600;
    margin-right: 2px;
}
QTabBar::tab:selected { color: #006B5F; border-bottom: 2.5px solid #006B5F; }
QTabBar::tab:hover:!selected { color: #006B5F; background: #E6F2EF; }

/* ===== 按钮（pill）===== */
QPushButton {
    background: #006B5F; color: white; border: none; border-radius: 18px;
    padding: 9px 22px; font-weight: 600;
}
QPushButton:hover { background: #005A50; }
QPushButton:pressed { background: #00493F; }
QPushButton:disabled { background: #CDE4E0; color: #F2F7F6; }
QPushButton[class="secondary"] {
    background: #E8E6FB; color: #4B41E1; border: none;
}
QPushButton[class="secondary"]:hover { background: #DCD9F8; }
QPushButton[class="danger"]  { background: #BA1A1A; }
QPushButton[class="danger"]:hover  { background: #9C1414; }
QPushButton[class="ghost"] {
    background: transparent; color: #6C7A77; border: none; padding: 8px 12px;
}
QPushButton[class="ghost"]:hover { background: #EFEDE6; color: #181445; }
QPushButton:flat { icon-size: 18px 18px; }

/* ===== 输入 ===== */
QLineEdit, QComboBox {
    background: #FFFFFF; border: 1.5px solid #D8D5E8; border-radius: 18px;
    padding: 8px 14px; selection-background-color: #006B5F;
}
QLineEdit:focus, QComboBox:focus { border-color: #006B5F; }
QComboBox::drop-down {
    border: none; width: 22px; subcontrol-origin: padding; subcontrol-position: center right;
}
QComboBox[editable="true"]::drop-down { border: none; width: 0; }
QComboBox[editable="true"]::down-arrow { image: none; width: 0; height: 0; border: none; }
QComboBox QAbstractItemView {
    background: #FFFFFF; border: 1px solid #EAE6F4; border-radius: 10px;
    selection-background-color: #E6F2EF; selection-color: #006B5F;
    outline: none;
}

/* ===== 复选框（小号 + 白勾）===== */
QCheckBox { spacing: 6px; background: transparent; }
QCheckBox::indicator {
    width: 14px; height: 14px;
    border: 1px solid #BBCAC6; border-radius: 3px;
    background: #FFFFFF;
}
QCheckBox::indicator:hover { border-color: #006B5F; }
QCheckBox::indicator:checked {
    background: #006B5F; border-color: #006B5F;
    image: url(%1);
}
QCheckBox::indicator:indeterminate {
    background: #006B5F; border-color: #006B5F;
    image: url(%2);
}

/* 树/表内复选框（与 QCheckBox 同款，避免默认大框无勾） */
QTreeWidget::indicator, QTableWidget::indicator, QListWidget::indicator {
    width: 14px; height: 14px;
    border: 1px solid #BBCAC6; border-radius: 3px;
    background: #FFFFFF;
}
QTreeWidget::indicator:hover, QTableWidget::indicator:hover, QListWidget::indicator:hover {
    border-color: #006B5F;
}
QTreeWidget::indicator:checked, QTableWidget::indicator:checked, QListWidget::indicator:checked {
    background: #006B5F; border-color: #006B5F;
    image: url(%1);
}
QTreeWidget::indicator:indeterminate, QTableWidget::indicator:indeterminate {
    background: #006B5F; border-color: #006B5F;
    image: url(%2);
}

/* ===== 列表/树/表（斑马纹）===== */
QListWidget, QTreeWidget, QTableWidget {
    background: #FFFFFF; border: 1px solid #EAE6F4; border-radius: 12px;
    padding: 6px; outline: none; alternate-background-color: #F7F5FC;
}
QListWidget::item, QTreeWidget::item { padding: 6px 8px; border-radius: 6px; }
QListWidget::item:selected, QTreeWidget::item:selected {
    background: #E6F2EF; color: #00544A;
}
QListWidget::item:hover, QTreeWidget::item:hover { background: #F1EEE8; }
QTableWidget {
    gridline-color: transparent;
}
QTableWidget::item { padding: 4px 8px; border-radius: 4px; }
QTableWidget::item:selected { background: #E6F2EF; color: #00544A; }

/* ===== 表头 ===== */
QHeaderView::section {
    background: #F7F5FC; color: #6C7A77; border: none;
    border-bottom: 1px solid #EAE6F4;
    padding: 9px 10px; font-weight: 600; font-size: 12px;
}
QHeaderView::down-arrow {
    width: 0; height: 0;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 6px solid #3C4947;
    margin-left: 4px;
}
QHeaderView::up-arrow {
    width: 0; height: 0;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-bottom: 6px solid #3C4947;
    margin-left: 4px;
}
QTableCornerButton::section { background: #F7F5FC; border: none; }

/* ===== 进度条 ===== */
QProgressBar {
    background: #E4E1F2; border: none; border-radius: 5px; height: 10px;
    text-align: center; color: transparent;
}
QProgressBar::chunk { background: #14B8A6; border-radius: 5px; }

/* ===== 菜单/状态栏 ===== */
QStatusBar { background: transparent; color: #6C7A77; }
QMenuBar { background: #FAFAF7; border-bottom: 1px solid #EAE6F4; }
QMenuBar::item { padding: 8px 14px; border-radius: 6px; }
QMenuBar::item:selected { background: #E6F2EF; color: #006B5F; }
QMenu {
    background: #FFFFFF; border: 1px solid #EAE6F4; border-radius: 10px; padding: 6px;
}
QMenu::item { padding: 8px 24px; border-radius: 6px; }
QMenu::item:selected { background: #E6F2EF; color: #006B5F; }
QMenu::separator { height: 1px; background: #EAE6F4; margin: 4px 8px; }

/* ===== 滚动条 ===== */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #D4D0E4; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #B9B4D2; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #D4D0E4; border-radius: 4px; min-width: 30px; }
QScrollBar::handle:horizontal:hover { background: #B9B4D2; }

/* ===== 分割器 ===== */
QSplitter::handle { background: #EAE6F4; width: 2px; }

/* ===== 消息框 ===== */
QMessageBox { background: #FFFFFF; }
QMessageBox QLabel { background: transparent; }
)").arg(check, partial);
}

} // namespace DiskOrganizer
