/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Packs a directory into a zip, because Taildrop transfers single files.
 *
 * Compression happens on a worker thread: a large folder takes seconds, and the
 * daemon's event loop is also holding the inbox long-poll and the event bus.
 */

#pragma once

#include <QObject>
#include <QString>

class Archiver : public QObject
{
    Q_OBJECT

public:
    explicit Archiver(QObject *parent = nullptr);

    /**
     * Zip @p directory into the cache. Emits packed() or failed() with @p token
     * so the caller can match the result to the transfer it queued.
     */
    void pack(const QString &token, const QString &directory);

Q_SIGNALS:
    void packed(const QString &token, const QString &archivePath, qint64 size);
    void failed(const QString &token, const QString &message);
};
