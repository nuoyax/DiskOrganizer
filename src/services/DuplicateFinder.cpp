#include "services/DuplicateFinder.h"
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QtConcurrent>
#include <QDateTime>
#include <algorithm>

namespace {

QByteArray partialHash(const QString& path, qint64 headBytes = 4096) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(f.read(headBytes));
    return h.result();
}

QByteArray fullHash(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    char buf[64 * 1024];
    qint64 n;
    while ((n = f.read(buf, sizeof(buf))) > 0) h.addData(buf, int(n));
    return h.result();
}

} // namespace

DuplicateFinder::DuplicateFinder(QObject* parent) : QObject(parent) {}

void DuplicateFinder::find(const QList<FileInfo>& allFiles, bool useContentHash) {
    m_cancelRequested = false;

    (void)QtConcurrent::run([this, allFiles, useContentHash]() {
        // 第 1 级：按大小分组，只保留 size>1 的组
        QHash<qint64, QList<FileInfo>> bySize;
        for (const auto& f : allFiles) {
            if (m_cancelRequested) break;
            if (f.isDir || f.size == 0) continue;
            bySize[f.size].append(f);
        }

        // 第 2 级：部分哈希粗筛
        QHash<QPair<qint64, QByteArray>, QList<FileInfo>> byHead;
        int processed = 0;
        const int candidates = bySize.size();
        for (auto it = bySize.constBegin(); it != bySize.constEnd(); ++it) {
            if (m_cancelRequested) break;
            if (it.value().size() < 2) { ++processed; continue; }
            for (const auto& f : it.value()) {
                const QByteArray h = partialHash(f.absolutePath);
                byHead[{f.size, h}].append(f);
            }
            ++processed;
            emit progress(processed * 100 / qMax(1, candidates));
        }

        // 第 3 级：全量哈希确认
        int groups = 0;
        qint64 wasted = 0;
        for (auto it = byHead.constBegin(); it != byHead.constEnd(); ++it) {
            if (m_cancelRequested) break;
            if (it.value().size() < 2) continue;
            if (!useContentHash) {
                // 不校验内容时按粗筛结果直接分组
                DuplicateGroup g;
                g.files = it.value();
                g.wastedBytes = (g.files.size() - 1) * it.key().first;
                wasted += g.wastedBytes;
                ++groups;
                emit groupFound(g);
                continue;
            }
            QHash<QByteArray, QList<FileInfo>> byFull;
            for (const auto& f : it.value())
                byFull[fullHash(f.absolutePath)].append(f);
            for (auto git = byFull.constBegin(); git != byFull.constEnd(); ++git) {
                if (git.value().size() < 2) continue;
                DuplicateGroup g;
                g.files = git.value();
                std::sort(g.files.begin(), g.files.end(),
                          [](auto& a, auto& b) { return a.lastModified < b.lastModified; });
                g.wastedBytes = (g.files.size() - 1) * g.files.first().size;
                wasted += g.wastedBytes;
                ++groups;
                emit groupFound(g);
            }
        }
        emit finished(groups, wasted);
    });
}

void DuplicateFinder::cancel() { m_cancelRequested = true; }
