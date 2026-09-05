#include "services/DuplicateFinder.h"

DuplicateFinder::DuplicateFinder(QObject* parent) : QObject(parent) {}

void DuplicateFinder::find(const QList<FileInfo>& allFiles, bool useContentHash) {
    m_cancelRequested = false;
    Q_UNUSED(allFiles); Q_UNUSED(useContentHash)
}

void DuplicateFinder::cancel() { m_cancelRequested = true; }
