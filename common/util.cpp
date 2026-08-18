/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "util.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace drip
{

QString humanSize(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(QLatin1String(units[unit]));
}

QString uniquePath(const QString &directory, const QString &fileName)
{
    const QDir dir(directory);
    if (!dir.exists(fileName)) {
        return dir.filePath(fileName);
    }

    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int n = 2; n < 10000; ++n) {
        const QString candidate = suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(n)
                                                   : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
        if (!dir.exists(candidate)) {
            return dir.filePath(candidate);
        }
    }
    return dir.filePath(fileName);
}

QString outgoingCacheDirectory()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/drip/outgoing");
    QDir().mkpath(path);
    return path;
}

QString abbreviateHome(const QString &path)
{
    const QString home = QDir::homePath();
    if (!home.isEmpty() && path.startsWith(home)) {
        return QLatin1Char('~') + path.mid(home.size());
    }
    return path;
}

}
