/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dbusservice.h"
#include "inbox.h"
#include "settings.h"
#include "tailnet.h"
#include "transfers.h"

#include <QDBusConnection>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace
{
QString stateName(TransferState state)
{
    switch (state) {
    case TransferState::Packing:
        return QStringLiteral("packing");
    case TransferState::Queued:
        return QStringLiteral("queued");
    case TransferState::Active:
        return QStringLiteral("active");
    case TransferState::Completed:
        return QStringLiteral("completed");
    case TransferState::Failed:
        return QStringLiteral("failed");
    case TransferState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

QJsonObject deviceObject(const Device &device)
{
    return QJsonObject{
        {QStringLiteral("id"), device.stableId},
        {QStringLiteral("name"), device.displayName},
        {QStringLiteral("hostName"), device.hostName},
        {QStringLiteral("os"), device.os},
        {QStringLiteral("owner"), device.ownerName},
        {QStringLiteral("avatarUrl"), device.avatarUrl},
        {QStringLiteral("ip"), device.primaryIp},
        {QStringLiteral("online"), device.online},
        {QStringLiteral("canReceive"), device.canReceive()},
        {QStringLiteral("reason"), device.unavailableReason()},
        {QStringLiteral("lastSeen"), device.lastSeen.isValid() ? device.lastSeen.toString(Qt::ISODate) : QString()},
    };
}

QJsonObject transferObject(const Transfer &transfer)
{
    return QJsonObject{
        {QStringLiteral("id"), transfer.id},
        {QStringLiteral("deviceId"), transfer.deviceId},
        {QStringLiteral("deviceName"), transfer.deviceName},
        {QStringLiteral("fileName"), transfer.fileName},
        {QStringLiteral("path"), transfer.localPath},
        {QStringLiteral("size"), double(transfer.size)},
        {QStringLiteral("transferred"), double(transfer.transferred)},
        {QStringLiteral("progress"), transfer.progress()},
        {QStringLiteral("incoming"), transfer.direction == TransferDirection::Incoming},
        {QStringLiteral("state"), stateName(transfer.state)},
        {QStringLiteral("error"), transfer.error},
        {QStringLiteral("queuedAt"), transfer.queuedAt.toString(Qt::ISODate)},
        {QStringLiteral("finishedAt"), transfer.finishedAt.isValid() ? transfer.finishedAt.toString(Qt::ISODate) : QString()},
    };
}

QJsonObject arrivalObject(const PendingArrival &arrival)
{
    return QJsonObject{
        {QStringLiteral("name"), arrival.name},
        {QStringLiteral("size"), double(arrival.size)},
        {QStringLiteral("senderId"), arrival.senderId},
        {QStringLiteral("senderName"), arrival.senderName},
        {QStringLiteral("arrivedAt"), arrival.arrivedAt.toString(Qt::ISODate)},
    };
}

QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
}

DBusService::DBusService(Tailnet *tailnet, TransferManager *transfers, InboxWatcher *inbox, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_tailnet(tailnet)
    , m_transfers(transfers)
    , m_inbox(inbox)
    , m_settings(settings)
{
    connect(m_settings, &Settings::changed, this, [this] {
        Q_EMIT settingsChanged(m_settings->toJson());
    });
    connect(m_inbox, &InboxWatcher::pendingArrivalsChanged, this, [this] {
        Q_EMIT pendingArrivalsChanged(pendingArrivals());
    });
    connect(m_inbox, &InboxWatcher::arrivalPending, this, [this](const PendingArrival &arrival) {
        Q_EMIT arrivalPending(toJson(arrival));
    });
    connect(m_tailnet, &Tailnet::devicesChanged, this, [this] {
        Q_EMIT devicesChanged(devices());
    });
    connect(m_tailnet, &Tailnet::connectedChanged, this, &DBusService::connectedChanged);
    connect(m_transfers, &TransferManager::transferAdded, this, [this](const Transfer &transfer) {
        Q_EMIT transferAdded(toJson(transfer));
    });
    connect(m_transfers, &TransferManager::transferUpdated, this, [this](const Transfer &transfer) {
        Q_EMIT transferUpdated(toJson(transfer));
    });
    connect(m_transfers, &TransferManager::historyCleared, this, &DBusService::historyCleared);
}

QString DBusService::serviceName()
{
    return QStringLiteral("dev.drip.Daemon");
}

QString DBusService::objectPath()
{
    return QStringLiteral("/Daemon");
}

bool DBusService::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        return false;
    }
    return bus.registerService(serviceName());
}

QString DBusService::toJson(const Device &device)
{
    return compact(deviceObject(device));
}

QString DBusService::toJson(const Transfer &transfer)
{
    return compact(transferObject(transfer));
}

QString DBusService::toJson(const PendingArrival &arrival)
{
    return compact(arrivalObject(arrival));
}

QString DBusService::devices() const
{
    QJsonArray array;
    const QList<Device> devices = m_tailnet->devices();
    for (const Device &device : devices) {
        array.append(deviceObject(device));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString DBusService::self() const
{
    return compact(deviceObject(m_tailnet->self()));
}

QString DBusService::transfers() const
{
    QJsonArray array;
    const QList<Transfer> transfers = m_transfers->transfers();
    for (const Transfer &transfer : transfers) {
        array.append(transferObject(transfer));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

bool DBusService::connected() const
{
    return m_tailnet->connected();
}

QString DBusService::destinationRoot() const
{
    return m_inbox->destinationRoot();
}

QStringList DBusService::send(const QString &deviceId, const QStringList &paths)
{
    QStringList local;
    local.reserve(paths.size());
    for (const QString &entry : paths) {
        // QML hands us file:// URLs; the CLI and tests hand us plain paths.
        const QUrl url(entry);
        local += url.isLocalFile() ? url.toLocalFile() : entry;
    }
    // Unreadable paths are passed through; the transfer manager records them
    // as failed rather than dropping them silently.
    return m_transfers->sendAll(deviceId, local);
}

void DBusService::cancel(const QString &transferId)
{
    m_transfers->cancel(transferId);
}

void DBusService::clearHistory()
{
    m_transfers->clearHistory();
}

void DBusService::refresh()
{
    m_tailnet->refresh();
}

QString DBusService::settings() const
{
    return m_settings->toJson();
}

void DBusService::updateSettings(const QString &json)
{
    m_settings->applyJson(json);
}

QString DBusService::pendingArrivals() const
{
    QJsonArray array;
    const QList<PendingArrival> arrivals = m_inbox->pendingArrivals();
    for (const PendingArrival &arrival : arrivals) {
        array.append(arrivalObject(arrival));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void DBusService::acceptArrival(const QString &name)
{
    m_inbox->accept(name);
}

void DBusService::declineArrival(const QString &name)
{
    m_inbox->decline(name);
}

#include "moc_dbusservice.cpp"
