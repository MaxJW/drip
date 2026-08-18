/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "notifier.h"
#include "util.h"

#include <KIO/OpenFileManagerWindowJob>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KNotification>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>

namespace
{
constexpr auto ComponentName = "drip";

QString elide(const QString &text, int limit = 48)
{
    return text.size() <= limit ? text : text.left(limit - 1) + QStringLiteral("…");
}

}

Notifier::Notifier(QObject *parent)
    : QObject(parent)
{
}

void Notifier::openFile(const QString &path)
{
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(path));
    job->setRunExecutables(false); // an arriving file must never be executed
    job->start();
}

void Notifier::showInFolder(const QString &path)
{
    KIO::highlightInFileManager({QUrl::fromLocalFile(path)});
}

void Notifier::moveElsewhere(const QString &path)
{
    const QFileInfo info(path);
    const QString destination = QFileDialog::getExistingDirectory(nullptr,
                                                                  i18n("Move “%1” to…", info.fileName()),
                                                                  info.absolutePath());
    if (destination.isEmpty()) {
        return;
    }

    const QString target = drip::uniquePath(destination, info.fileName());
    if (QFile::rename(path, target)) {
        Q_EMIT fileMoved(path, target);
    }
}

void Notifier::fileReceived(const QString &path, const QString &senderName)
{
    const QFileInfo info(path);

    auto *notification = new KNotification(QStringLiteral("fileReceived"));
    notification->setComponentName(QLatin1String(ComponentName));
    notification->setTitle(i18n("File received"));
    notification->setText(i18nc("%1 is a filename, %2 a device name", "%1 — from %2", elide(info.fileName()), senderName));
    notification->setIconName(QStringLiteral("drip"));
    // No setUrls(): Plasma's file-preview area is oversized and overdraws the
    // action buttons, with or without actions present.
    notification->setAutoDelete(true);

    auto *openAction = notification->addAction(i18n("Open"));
    connect(openAction, &KNotificationAction::activated, this, [this, path] {
        openFile(path);
    });

    auto *showAction = notification->addAction(i18n("Show in Folder"));
    connect(showAction, &KNotificationAction::activated, this, [this, path] {
        showInFolder(path);
    });

    auto *moveAction = notification->addAction(i18n("Move to…"));
    connect(moveAction, &KNotificationAction::activated, this, [this, path] {
        moveElsewhere(path);
    });

    auto *defaultAction = notification->addDefaultAction(i18n("Open"));
    connect(defaultAction, &KNotificationAction::activated, this, [this, path] {
        openFile(path);
    });

    notification->sendEvent();
}

void Notifier::arrivalPending(const QString &fileName, qint64 size, const QString &senderName)
{
    auto *notification = new KNotification(QStringLiteral("arrivalPending"));
    notification->setComponentName(QLatin1String(ComponentName));
    notification->setTitle(i18nc("%1 is a device name", "%1 sent you a file", senderName));
    notification->setText(i18nc("%1 is a filename, %2 a file size", "%1 — %2. Accept?", elide(fileName), drip::humanSize(size)));
    notification->setIconName(QStringLiteral("drip"));
    // Persistent: an unanswered prompt leaves the file waiting indefinitely.
    notification->setFlags(KNotification::Persistent);
    notification->setAutoDelete(true);

    auto *acceptAction = notification->addAction(i18n("Accept"));
    connect(acceptAction, &KNotificationAction::activated, this, [this, fileName] {
        Q_EMIT arrivalAccepted(fileName);
    });

    auto *declineAction = notification->addAction(i18n("Decline"));
    connect(declineAction, &KNotificationAction::activated, this, [this, fileName] {
        Q_EMIT arrivalDeclined(fileName);
    });

    // Withdrawal is driven by arrivalResolved, so answering here or in the
    // panel takes the prompt down the same way.
    m_arrivalPrompts.insert(fileName, notification);
    connect(notification, &KNotification::closed, this, [this, fileName] {
        m_arrivalPrompts.remove(fileName);
    });

    notification->sendEvent();
}

void Notifier::arrivalResolved(const QString &fileName)
{
    const QPointer<KNotification> prompt = m_arrivalPrompts.take(fileName);
    if (prompt) {
        prompt->close();
    }
}

void Notifier::receiveFailed(const QString &fileName, const QString &error)
{
    auto *notification = new KNotification(QStringLiteral("transferFailed"));
    notification->setComponentName(QLatin1String(ComponentName));
    notification->setTitle(i18n("Could not receive file"));
    notification->setText(i18nc("%1 is a filename, %2 an error message", "%1 — %2", elide(fileName), error));
    notification->setIconName(QStringLiteral("dialog-error"));
    notification->setAutoDelete(true);
    notification->sendEvent();
}

void Notifier::sendFailed(const QString &fileName, const QString &deviceName, const QString &error)
{
    auto *notification = new KNotification(QStringLiteral("transferFailed"));
    notification->setComponentName(QLatin1String(ComponentName));
    notification->setTitle(i18nc("%1 is a device name", "Could not send to %1", deviceName));
    notification->setText(i18nc("%1 is a filename, %2 an error message", "%1 — %2", elide(fileName), error));
    notification->setIconName(QStringLiteral("dialog-error"));
    notification->setAutoDelete(true);
    notification->sendEvent();
}

#include "moc_notifier.cpp"
