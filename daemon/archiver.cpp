/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "archiver.h"

#include "util.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrent>

#include <KZip>

namespace
{

struct PackResult {
    QString archivePath;
    qint64 size = 0;
    QString error;
};

/** Runs on a worker thread; touches nothing but its arguments. */
PackResult packDirectory(const QString &directory, const QString &archivePath)
{
    PackResult result;

    const QDir source(directory);
    if (!source.exists()) {
        result.error = QStringLiteral("folder no longer exists");
        return result;
    }

    KZip zip(archivePath);
    if (!zip.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("could not create the archive");
        return result;
    }

    // Store paths relative to the folder's parent, so unpacking recreates the
    // folder itself rather than scattering its contents.
    const QString base = source.absolutePath();
    bool addedAnything = false;

    QDirIterator it(base, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QString entry = source.dirName() + QLatin1Char('/') + source.relativeFilePath(filePath);
        if (zip.addLocalFile(filePath, entry)) {
            addedAnything = true;
        }
    }

    zip.close();

    if (!addedAnything) {
        QFile::remove(archivePath);
        result.error = QStringLiteral("folder is empty");
        return result;
    }

    result.archivePath = archivePath;
    result.size = QFileInfo(archivePath).size();
    return result;
}

}

Archiver::Archiver(QObject *parent)
    : QObject(parent)
{
}

void Archiver::pack(const QString &token, const QString &directory)
{
    const QFileInfo info(directory);
    const QString archivePath = drip::uniquePath(drip::outgoingCacheDirectory(), info.fileName() + QStringLiteral(".zip"));

    auto *watcher = new QFutureWatcher<PackResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, token] {
        const PackResult result = watcher->result();
        watcher->deleteLater();
        if (result.error.isEmpty()) {
            Q_EMIT packed(token, result.archivePath, result.size);
        } else {
            Q_EMIT failed(token, result.error);
        }
    });
    watcher->setFuture(QtConcurrent::run(packDirectory, info.absoluteFilePath(), archivePath));
}
