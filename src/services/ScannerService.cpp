#include "services/ScannerService.h"

ScannerService::ScannerService(QObject* parent) : QObject(parent) {}

void ScannerService::startScan(const QStringList& rootPaths) {
    m_cancelRequested = false;
    // TODO: 在 QtConcurrent 线程池中迭代目录；过滤符号链接死循环；
    // 通过 signals 汇报 progress / fileScanned / finished
    Q_UNUSED(rootPaths)
}

void ScannerService::cancel() {
    m_cancelRequested = true;
}
