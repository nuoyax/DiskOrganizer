#include "services/CleanerService.h"

QList<CleanItem> CleanerService::findCleanableItems(const QList<CleanCategory>& categories) const {
    Q_UNUSED(categories)
    return {};
}

qint64 CleanerService::clean(const QList<CleanItem>& items, bool toRecycleBin) {
    Q_UNUSED(items); Q_UNUSED(toRecycleBin)
    return 0;
}
