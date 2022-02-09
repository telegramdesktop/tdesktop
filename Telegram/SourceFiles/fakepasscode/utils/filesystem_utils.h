#ifndef TELEGRAM_FILESYSTEM_UTILS_H
#define TELEGRAM_FILESYSTEM_UTILS_H

#include <QDir>
#include <QFile>
#include <QString>

void RenameAndRemoveRecursively(const QString& path);
void RenameAndRemove(const QString& path);

#endif //TELEGRAM_FILESYSTEM_UTILS_H
