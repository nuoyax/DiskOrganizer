#include "Icons.h"
#include "DuplicatePage.h"
#include "FlatStyle.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <QtConcurrent>
#include "services/CleanerService.h"
#include "services/DuplicateFinder.h"
#include "services/ScannerService.h"
#include "SearchableComboBox.h"
#include "util/FileSystemUtil.h"
#include "util/SizeFormatter.h"

namespace DiskOrganizer {

namespace {
QFrame* metricCard(const char* iconPath, const QColor& iconTint, const QColor& iconBg,
                   const QString& title, QLabel** valueOut, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setProperty("class", "card");
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(10);
    auto* chip = new QLabel(card);
    const int px = 18;
    chip->setPixmap(Icons::tinted(QString::fromUtf8(iconPath), iconTint, px).pixmap(px, px));
    chip->setFixedSize(34, 34);
    chip->setAlignment(Qt::AlignCenter);
    chip->setStyleSheet(QString("background:%1; border-radius:8px;").arg(iconBg.name()));
    auto* col = new QVBoxLayout;
    col->setSpacing(0);
    auto* cap = new QLabel(title, card);
    cap->setStyleSheet("font-size:11px; color:#6C7A77; background:transparent;");
    auto* val = new QLabel("--", card);
    val->setStyleSheet("font-size:15px; font-weight:800; color:#181445; background:transparent;");
    col->addWidget(cap);
    col->addWidget(val);
    h->addWidget(chip);
    h->addLayout(col, 1);
    *valueOut = val;
    return card;
}

QString extCategory(const QString& path) {
    const QString e = QFileInfo(path).suffix().toLower();
    if (QStringList{"mp4","mkv","avi","mov","wmv","flv","webm","m4v","mpg","ts"}.contains(e))
        return QStringLiteral("video");
    if (QStringList{"pdf","doc","docx","xls","xlsx","ppt","pptx","txt","md","csv","zip","7z","rar","tar","gz"}.contains(e))
        return QStringLiteral("doc");
    if (QStringList{"iso","img","msi","exe","dmg","wim","vhd","vhdx"}.contains(e))
        return QStringLiteral("iso");
    if (QStringList{"mp3","wav","flac","aac","ogg","wma","m4a"}.contains(e))
        return QStringLiteral("audio");
    return QStringLiteral("other");
}
} // namespace

DuplicatePage::DuplicatePage(QWidget* parent) : PageBase(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* head = new QHBoxLayout;
    auto* headCol = new QVBoxLayout;
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto* title = new QLabel(tr("重复文件"), this);
    title->setStyleSheet("font-size:24px; font-weight:800; color:#181445; background:transparent;");
    m_headStatus = new QLabel(tr("SHA-1 哈希匹配引擎已就绪"), this);
    m_headStatus->setStyleSheet(
        "padding:3px 10px; border-radius:12px; background-color:#ECFDF5; color:#047857;"
        "font-size:12px; font-weight:600;");
    titleRow->addWidget(title);
    titleRow->addWidget(m_headStatus);
    titleRow->addStretch();
    auto* subtitle = new QLabel(
        tr("自动识别二进制完全相同的文件副本，默认为您安全保留每组最早创建的原作版本。"), this);
    subtitle->setStyleSheet("color:#6C7A77; background:transparent;");
    headCol->addLayout(titleRow);
    headCol->addWidget(subtitle);
    head->addLayout(headCol, 1);
    head->addWidget(metricCard(Icons::P::duplicate, QColor(0x4B, 0x41, 0xE1), QColor(0xEE, 0xF2, 0xFF),
                               tr("发现重复组"), &m_groupsMetric, this));
    head->addWidget(metricCard(Icons::P::trash, QColor(0x0D, 0x94, 0x88), QColor(0xF0, 0xFD, 0xFA),
                               tr("可释放空间"), &m_wastedMetric, this));
    layout->addLayout(head);

    auto* filterRow = new QHBoxLayout;
    auto* driveLabel = new QLabel(tr("目标磁盘:"), this);
    driveLabel->setStyleSheet("color:#6C7A77; font-size:12px; background:transparent;");
    m_driveCombo = new SearchableComboBox;
    const QIcon driveIcon = Icons::tinted(QString::fromUtf8(Icons::P::drive), QColor(0x4B, 0x41, 0xE1), 18);
    for (const auto& d : enumerateDisks())
        m_driveCombo->addItem(driveIcon, QString("%1 (%2)").arg(d.driveLetter, d.volumeLabel.isEmpty()
            ? QStringLiteral("本地磁盘") : d.volumeLabel), d.driveLetter);
    if (m_driveCombo->count() > 0) m_driveCombo->setCurrentIndex(0);
    m_driveCombo->setFixedWidth(240);

    m_typeGroup = new QButtonGroup(this);
    m_typeGroup->setExclusive(true);
    const struct { const char* text; const char* id; } types[] = {
        {"全部", "all"}, {"视频", "video"}, {"文档与归档", "doc"},
        {"安装包/镜像", "iso"}, {"音频", "audio"},
    };
    for (const auto& t : types) {
        auto* b = new QPushButton(QString::fromUtf8(t.text), this);
        b->setCheckable(true);
        b->setProperty("class", "secondary");
        b->setProperty("typeId", t.id);
        m_typeGroup->addButton(b);
        filterRow->addWidget(b);
        if (QString(t.id) == "all") b->setChecked(true);
    }

    m_sizeFilter = new QComboBox(this);
    m_sizeFilter->addItem(tr("大小: 全部"), 0);
    m_sizeFilter->addItem(tr("大小: > 1MB"), 1);
    m_sizeFilter->addItem(tr("大小: > 10MB"), 10);
    m_sizeFilter->addItem(tr("大小: > 100MB"), 100);
    m_sizeFilter->setCurrentIndex(0);

    m_findBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::scan), QColor("white")),
                                tr("查找重复"), this);
    filterRow->insertWidget(0, driveLabel);
    filterRow->insertWidget(1, m_driveCombo);
    filterRow->addWidget(m_sizeFilter);
    filterRow->addStretch();
    filterRow->addWidget(m_findBtn);
    layout->addLayout(filterRow);

    m_progress = new QProgressBar(this);
    m_progress->hide();
    layout->addWidget(m_progress);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_cardHost = new QWidget;
    auto* cardsLay = new QVBoxLayout(m_cardHost);
    cardsLay->setContentsMargins(0, 0, 0, 0);
    cardsLay->setSpacing(12);
    cardsLay->addStretch();
    m_scroll->setWidget(m_cardHost);
    layout->addWidget(m_scroll, 1);

    auto* bottom = new QHBoxLayout;
    m_summary = new QLabel(tr("尚未扫描"), this);
    m_summary->setStyleSheet("color:#6C7A77; background:transparent;");
    m_keepOldestBtn = new QPushButton(tr("保留每组最早修改的"), this);
    m_keepOldestBtn->setProperty("class", "secondary");
    m_keepNewestBtn = new QPushButton(tr("保留每组最新修改的"), this);
    m_keepNewestBtn->setProperty("class", "secondary");
    m_keepShortBtn = new QPushButton(tr("保留最短路径"), this);
    m_keepShortBtn->setProperty("class", "secondary");
    m_deleteBtn = new QPushButton(Icons::tinted(QString::fromUtf8(Icons::P::trash), QColor("white")),
                                  tr("删除选中（回收站）"), this);
    m_deleteBtn->setProperty("class", "danger");
    for (auto* b : {m_keepOldestBtn, m_keepNewestBtn, m_keepShortBtn, m_deleteBtn})
        b->setEnabled(false);
    bottom->addWidget(m_summary, 1);
    bottom->addWidget(m_keepOldestBtn);
    bottom->addWidget(m_keepNewestBtn);
    bottom->addWidget(m_keepShortBtn);
    bottom->addWidget(m_deleteBtn);
    layout->addLayout(bottom);

    connect(m_findBtn, &QPushButton::clicked, this, &DuplicatePage::doFind);
    connect(m_keepOldestBtn, &QPushButton::clicked, this, &DuplicatePage::keepOldest);
    connect(m_keepNewestBtn, &QPushButton::clicked, this, &DuplicatePage::keepNewest);
    connect(m_keepShortBtn, &QPushButton::clicked, this, &DuplicatePage::keepShortestPath);
    connect(m_deleteBtn, &QPushButton::clicked, this, &DuplicatePage::deleteSelected);
    connect(m_typeGroup, &QButtonGroup::buttonClicked, this, [this] { rebuildGroupCards(); });
    connect(m_sizeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { rebuildGroupCards(); });

    DiskOrganizer::applyCardShadows(this);
}

bool DuplicatePage::passTypeFilter(const QString& path) const {
    auto* btn = m_typeGroup->checkedButton();
    if (!btn) return true;
    const QString id = btn->property("typeId").toString();
    if (id == "all") return true;
    return extCategory(path) == id;
}

bool DuplicatePage::passSizeFilter(qint64 size) const {
    const qint64 mb = m_sizeFilter->currentData().toLongLong();
    if (mb <= 0) return true;
    return size >= mb * 1024LL * 1024;
}

void DuplicatePage::doFind() {
    const QString drive = m_driveCombo->currentData().toString();
    if (drive.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请选择要扫描的磁盘"));
        return;
    }
    const QString root = drive + "/";
    if (m_finding) {
        m_cancelled.store(true);
        m_headStatus->setText(tr("正在取消……"));
        m_summary->setText(tr("正在取消……"));
        return;
    }
    m_finding = true;
    m_cancelled.store(false);
    m_findBtn->setText(tr("取消"));
    m_headStatus->setText(tr("SHA-1 哈希匹配中……"));
    m_groups.clear();
    rebuildGroupCards();
    m_progress->setRange(0, 0);
    m_progress->show();
    m_summary->setText(tr("正在扫描磁盘……"));
    for (auto* b : {m_keepOldestBtn, m_keepNewestBtn, m_keepShortBtn, m_deleteBtn})
        b->setEnabled(false);

    (void)QtConcurrent::run([this, root]() {
        ScannerService scanner;
        auto cancelled = [this]() { return m_cancelled.load(); };
        std::function<bool(qint64, const QString&)> onProgress =
            [this, cancelled](qint64, const QString& path) -> bool {
            if (cancelled()) return false;
            QMetaObject::invokeMethod(this, [this, path]() {
                m_summary->setText(tr("扫描中：%1").arg(
                    fontMetrics().elidedText(path, Qt::ElideMiddle, 400)));
            }, Qt::QueuedConnection);
            return true;
        };
        QList<FileInfo> files = scanner.scanBlocking({root}, onProgress, 0, 0, cancelled);
        if (cancelled()) {
            QMetaObject::invokeMethod(this, [this]() { resetUiAfterCancel(); }, Qt::QueuedConnection);
            return;
        }

        QList<DupGroupUi> groups;
        QMutex mutex;
        QAtomicInt done{0};
        auto* finder = new DuplicateFinder;
        QObject::connect(finder, &DuplicateFinder::groupFound, finder,
                         [&groups, &mutex](const DuplicateGroup& g) {
            if (g.files.size() < 2) return;
            DupGroupUi ui;
            ui.name = QFileInfo(g.files.first().absolutePath).fileName();
            ui.hashPrefix = QString::number(qHash(g.files.first().absolutePath + QString::number(g.files.first().size)), 16).left(12);
            ui.fileSize = g.files.first().size;
            ui.wasted = g.wastedBytes;
            qint64 oldest = g.files.first().lastModified;
            for (const auto& f : g.files)
                oldest = qMin(oldest, f.lastModified);
            for (const auto& f : g.files) {
                DupRow row;
                row.path = f.absolutePath;
                row.size = f.size;
                row.modified = f.lastModified;
                row.keepSuggested = (f.lastModified == oldest);
                row.checked = !row.keepSuggested;
                ui.rows.append(row);
            }
            QMutexLocker lock(&mutex);
            groups.append(ui);
        }, Qt::DirectConnection);
        QObject::connect(finder, &DuplicateFinder::finished, finder,
                         [&done](int, qint64) {
            done.storeRelease(1);
        }, Qt::DirectConnection);
        finder->find(files, true);
        while (!done.loadAcquire()) {
            if (cancelled()) {
                finder->cancel();
            }
            QThread::msleep(40);
        }
        delete finder;

        if (cancelled()) {
            QMetaObject::invokeMethod(this, [this]() { resetUiAfterCancel(); }, Qt::QueuedConnection);
            return;
        }
        QList<DupGroupUi> groupsCopy;
        {
            QMutexLocker lock(&mutex);
            groupsCopy = groups;
        }
        QMetaObject::invokeMethod(this, [this, groupsCopy]() {
            m_groups = groupsCopy;
            m_finding = false;
            m_progress->hide();
            qint64 wasted = 0;
            int copies = 0;
            for (const auto& g : m_groups) {
                wasted += g.wasted;
                copies += g.rows.size();
            }
            m_groupsMetric->setText(tr("%1 组").arg(m_groups.size()));
            m_wastedMetric->setText(formatSize(wasted));
            m_headStatus->setText(tr("SHA-1 哈希匹配完成"));
            m_summary->setText(tr("发现 %1 组重复（%2 个副本），浪费 %3")
                                   .arg(m_groups.size()).arg(copies).arg(formatSize(wasted)));
            m_findBtn->setText(tr("查找重复"));
            const bool ok = !m_groups.isEmpty();
            for (auto* b : {m_keepOldestBtn, m_keepNewestBtn, m_keepShortBtn, m_deleteBtn})
                b->setEnabled(ok);
            rebuildGroupCards();
            updateFooter();
            DiskOrganizer::applyCardShadows(this);
        }, Qt::QueuedConnection);
    });
}

void DuplicatePage::resetUiAfterCancel() {
    m_finding = false;
    m_progress->hide();
    m_headStatus->setText(tr("SHA-1 哈希匹配引擎已就绪"));
    m_summary->setText(tr("扫描已取消"));
    m_findBtn->setText(tr("查找重复"));
}

void DuplicatePage::rebuildGroupCards() {
    QLayout* lay = m_cardHost->layout();
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }

    int shownGroups = 0;
    for (int gi = 0; gi < m_groups.size(); ++gi) {
        const DupGroupUi& g = m_groups[gi];
        // 过滤：组内至少有一个通过类型/大小过滤的文件才显示
        QList<int> visibleRows;
        for (int ri = 0; ri < g.rows.size(); ++ri) {
            if (passTypeFilter(g.rows[ri].path) && passSizeFilter(g.rows[ri].size))
                visibleRows.append(ri);
        }
        if (visibleRows.size() < 2) continue;
        ++shownGroups;

        auto* card = new QFrame(m_cardHost);
        card->setProperty("class", "card");
        auto* cv = new QVBoxLayout(card);
        cv->setContentsMargins(14, 12, 14, 12);
        cv->setSpacing(8);

        auto* header = new QHBoxLayout;
        auto* nameCol = new QVBoxLayout;
        auto* name = new QLabel(g.name, card);
        name->setStyleSheet("font-size:14px; font-weight:700; color:#181445; background:transparent;");
        auto* meta = new QLabel(
            tr("共 %1 个副本 · 单文件 %2 · SHA-1 %3…")
                .arg(visibleRows.size()).arg(formatSize(g.fileSize)).arg(g.hashPrefix), card);
        meta->setStyleSheet("font-size:11px; color:#6C7A77; background:transparent;");
        nameCol->addWidget(name);
        nameCol->addWidget(meta);
        header->addLayout(nameCol, 1);
        auto* waste = new QLabel(tr("可释放 %1").arg(formatSize(g.wasted)), card);
        waste->setStyleSheet("font-weight:700; color:#E11D48; background:transparent;");
        auto* smartBtn = new QPushButton(tr("智能勾选"), card);
        smartBtn->setProperty("class", "secondary");
        connect(smartBtn, &QPushButton::clicked, this, [this, gi] {
            if (gi < 0 || gi >= m_groups.size()) return;
            qint64 oldest = m_groups[gi].rows.first().modified;
            for (const auto& r : m_groups[gi].rows) oldest = qMin(oldest, r.modified);
            for (auto& r : m_groups[gi].rows) {
                r.keepSuggested = (r.modified == oldest);
                r.checked = !r.keepSuggested;
            }
            rebuildGroupCards();
            updateFooter();
        });
        header->addWidget(waste);
        header->addWidget(smartBtn);
        cv->addLayout(header);

        for (int ri : visibleRows) {
            DupRow& row = m_groups[gi].rows[ri];
            auto* rowW = new QWidget(card);
            auto* rh = new QHBoxLayout(rowW);
            rh->setContentsMargins(4, 2, 4, 2);
            auto* cb = new QCheckBox(rowW);
            cb->setChecked(row.checked);
            cb->setEnabled(!row.keepSuggested);
            connect(cb, &QCheckBox::toggled, this, [this, gi, ri](bool on) {
                if (gi >= 0 && gi < m_groups.size() && ri >= 0 && ri < m_groups[gi].rows.size())
                    m_groups[gi].rows[ri].checked = on;
                updateFooter();
            });
            auto* path = new QLabel(row.path, rowW);
            path->setStyleSheet("font-family:Consolas,'Cascadia Mono',monospace; font-size:12px; background:transparent;");
            path->setTextInteractionFlags(Qt::TextSelectableByMouse);
            auto* size = new QLabel(formatSize(row.size), rowW);
            size->setStyleSheet("font-family:Consolas; background:transparent;");
            auto* date = new QLabel(QDateTime::fromMSecsSinceEpoch(row.modified).toString("yyyy-MM-dd"), rowW);
            date->setStyleSheet("color:#6C7A77; background:transparent;");
            auto* policy = new QLabel(row.keepSuggested ? tr("建议保留") : tr("冗余副本"), rowW);
            policy->setStyleSheet(row.keepSuggested
                ? "padding:2px 8px; border-radius:8px; background:#ECFDF5; color:#047857; font-size:11px; font-weight:600;"
                : "padding:2px 8px; border-radius:8px; background:#FFF1F2; color:#BE123C; font-size:11px; font-weight:600;");
            auto* openBtn = new QPushButton(tr("打开"), rowW);
            openBtn->setProperty("class", "ghost");
            connect(openBtn, &QPushButton::clicked, this, [path = row.path] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
            });
            rh->addWidget(cb);
            rh->addWidget(path, 1);
            rh->addWidget(size);
            rh->addWidget(date);
            rh->addWidget(policy);
            rh->addWidget(openBtn);
            if (!row.keepSuggested)
                rowW->setStyleSheet("background:rgba(148,163,184,0.08); border-radius:6px;");
            cv->addWidget(rowW);
        }
        lay->addWidget(card);
    }
    static_cast<QVBoxLayout*>(lay)->addStretch();
    if (shownGroups == 0 && !m_groups.isEmpty())
        m_summary->setText(tr("当前筛选条件下无匹配组"));
}

void DuplicatePage::updateFooter() {
    int n = 0;
    qint64 bytes = 0;
    for (const auto& g : m_groups) {
        for (const auto& r : g.rows) {
            if (!r.checked) continue;
            if (!passTypeFilter(r.path) || !passSizeFilter(r.size)) continue;
            ++n;
            bytes += r.size;
        }
    }
    m_summary->setText(tr("已勾选 %1 个冗余文件 · 预计清理释放 %2")
                           .arg(n).arg(formatSize(bytes)));
    m_deleteBtn->setEnabled(n > 0 && !m_finding);
}

QStringList DuplicatePage::selectedPaths() const {
    QStringList paths;
    for (const auto& g : m_groups)
        for (const auto& r : g.rows)
            if (r.checked && passTypeFilter(r.path) && passSizeFilter(r.size))
                paths.append(r.path);
    return paths;
}

void DuplicatePage::keepOldest() {
    for (auto& g : m_groups) {
        if (g.rows.isEmpty()) continue;
        qint64 oldest = g.rows.first().modified;
        for (const auto& r : g.rows) oldest = qMin(oldest, r.modified);
        for (auto& r : g.rows) {
            r.keepSuggested = (r.modified == oldest);
            r.checked = !r.keepSuggested;
        }
    }
    rebuildGroupCards();
    updateFooter();
}

void DuplicatePage::keepNewest() {
    for (auto& g : m_groups) {
        if (g.rows.isEmpty()) continue;
        qint64 newest = g.rows.first().modified;
        for (const auto& r : g.rows) newest = qMax(newest, r.modified);
        for (auto& r : g.rows) {
            r.keepSuggested = (r.modified == newest);
            r.checked = !r.keepSuggested;
        }
    }
    rebuildGroupCards();
    updateFooter();
}

void DuplicatePage::keepShortestPath() {
    for (auto& g : m_groups) {
        if (g.rows.isEmpty()) continue;
        int best = 0;
        for (int i = 1; i < g.rows.size(); ++i)
            if (g.rows[i].path.size() < g.rows[best].path.size()) best = i;
        for (int i = 0; i < g.rows.size(); ++i) {
            g.rows[i].keepSuggested = (i == best);
            g.rows[i].checked = (i != best);
        }
    }
    rebuildGroupCards();
    updateFooter();
}

void DuplicatePage::deleteSelected() {
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return;
    if (QMessageBox::question(this, tr("确认删除"),
            tr("将 %1 个文件移入回收站，是否继续？").arg(paths.size())) != QMessageBox::Yes)
        return;
    CleanerService cleaner;
    QList<CleanItem> items;
    for (const QString& path : paths) {
        CleanItem ci;
        ci.path = path;
        ci.size = QFileInfo(path).size();
        ci.safeToDelete = true;
        items.append(ci);
    }
    const qint64 freed = cleaner.clean(items, true);
    // 从组中移除已删路径
    for (auto& g : m_groups) {
        QList<DupRow> remain;
        for (const auto& r : g.rows)
            if (!paths.contains(r.path)) remain.append(r);
        g.rows = remain;
        g.wasted = g.rows.isEmpty() ? 0 : g.fileSize * qMax(0, g.rows.size() - 1);
    }
    m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
                                  [](const DupGroupUi& g) { return g.rows.size() < 2; }),
                   m_groups.end());
    qint64 wasted = 0;
    for (const auto& g : m_groups) wasted += g.wasted;
    m_groupsMetric->setText(tr("%1 组").arg(m_groups.size()));
    m_wastedMetric->setText(formatSize(wasted));
    m_summary->setText(tr("已删除 %1，释放 %2").arg(paths.size()).arg(formatSize(freed)));
    rebuildGroupCards();
    updateFooter();
}

} // namespace DiskOrganizer
