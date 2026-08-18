/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Desktop notifications for arrivals and failures.
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class KNotification;

class Notifier : public QObject
{
    Q_OBJECT

public:
    explicit Notifier(QObject *parent = nullptr);

    void fileReceived(const QString &path, const QString &senderName);
    void receiveFailed(const QString &fileName, const QString &error);
    void sendFailed(const QString &fileName, const QString &deviceName, const QString &error);

    /** Raised only when auto-accept is off. */
    void arrivalPending(const QString &fileName, qint64 size, const QString &senderName);
    /** Withdraws the prompt, however it was answered. */
    void arrivalResolved(const QString &fileName);

Q_SIGNALS:
    /** The user chose "Move to..." and picked a destination. */
    void fileMoved(const QString &fromPath, const QString &toPath);

    void arrivalAccepted(const QString &fileName);
    void arrivalDeclined(const QString &fileName);

private:
    void openFile(const QString &path);
    void showInFolder(const QString &path);
    void moveElsewhere(const QString &path);

    QHash<QString, QPointer<KNotification>> m_arrivalPrompts;
};
