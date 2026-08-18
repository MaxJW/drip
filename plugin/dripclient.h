/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * QML-facing client for dev.drip.Daemon. Watches the bus name and resyncs when
 * the daemon appears, so the panel survives it restarting.
 */

#pragma once

#include <QJsonValue>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

#include "jsonlistmodel.h"

class QDBusInterface;
class QDBusServiceWatcher;

class DripClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(JsonListModel *devices READ devicesModel CONSTANT)
    Q_PROPERTY(JsonListModel *transfers READ transfersModel CONSTANT)
    /** dripd is on the bus. */
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /** dripd says tailscaled is up and running. */
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
    /** Arrivals waiting for accept or decline; empty unless auto-accept is off. */
    Q_PROPERTY(JsonListModel *pendingArrivals READ pendingArrivalsModel CONSTANT)

    Q_PROPERTY(QString destinationRoot READ destinationRoot WRITE setDestinationRoot NOTIFY settingsChanged)
    Q_PROPERTY(bool autoAccept READ autoAccept WRITE setAutoAccept NOTIFY settingsChanged)
    Q_PROPERTY(bool groupBySender READ groupBySender WRITE setGroupBySender NOTIFY settingsChanged)
    Q_PROPERTY(bool keepHistory READ keepHistory WRITE setKeepHistory NOTIFY settingsChanged)
    /** Devices that could accept a file right now. */
    Q_PROPERTY(QStringList reachableIds READ reachableIds NOTIFY reachableIdsChanged)

public:
    explicit DripClient(QObject *parent = nullptr);

    JsonListModel *devicesModel() const;
    JsonListModel *transfersModel() const;
    bool available() const
    {
        return m_available;
    }
    bool connected() const
    {
        return m_connected;
    }
    int activeCount() const
    {
        return m_activeCount;
    }
    JsonListModel *pendingArrivalsModel() const;
    QString destinationRoot() const
    {
        return m_destinationRoot;
    }
    bool autoAccept() const
    {
        return m_autoAccept;
    }
    bool groupBySender() const
    {
        return m_groupBySender;
    }
    bool keepHistory() const
    {
        return m_keepHistory;
    }
    void setDestinationRoot(const QString &path);
    void setAutoAccept(bool on);
    void setGroupBySender(bool on);
    void setKeepHistory(bool on);

    Q_INVOKABLE void acceptArrival(const QString &name);
    Q_INVOKABLE void declineArrival(const QString &name);

    QStringList reachableIds() const;
    Q_INVOKABLE QString reachableName(const QString &deviceId) const;

    /** @p paths may be file:// URLs (from a drop) or plain paths. */
    Q_INVOKABLE void send(const QString &deviceId, const QStringList &paths);
    Q_INVOKABLE void cancel(const QString &transferId);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void showInFolder(const QString &path);

    /** Pretty byte counts for the UI, so QML never hand-rolls them. */
    Q_INVOKABLE QString formatSize(double bytes) const;
    Q_INVOKABLE QString formatRelativeTime(const QString &isoTimestamp) const;
    /** Absolute path with the home directory shown as "~". */
    Q_INVOKABLE QString displayPath(const QString &path) const;

Q_SIGNALS:
    void availableChanged();
    void connectedChanged();
    void activeCountChanged();
    void reachableIdsChanged();
    void settingsChanged();
    /** Something arrived; the tray icon uses this to raise its dot. */
    void fileReceived(const QString &fileName, const QString &deviceName);
    /** Something needs a decision; the applet opens itself on this. */
    void arrivalPending(const QString &fileName, const QString &senderName);

private Q_SLOTS:
    // Connected by name via QDBusConnection::connect, so these must be slots.
    void onDevicesChanged(const QString &json);
    void onTransferAdded(const QString &json);
    void onTransferUpdated(const QString &json);
    void onHistoryCleared();
    void onSettingsChanged(const QString &json);
    void onPendingArrivalsChanged(const QString &json);
    void onArrivalPending(const QString &json);

private:
    void connectToDaemon();
    void setAvailable(bool available);
    void syncAll();
    void recountActive();
    /** Push one changed setting to dripd; the daemon is the source of truth. */
    void pushSetting(const QString &key, const QJsonValue &value);

    QDBusInterface *m_daemon = nullptr;
    QDBusServiceWatcher *m_watcher = nullptr;
    JsonListModel *m_devices;
    JsonListModel *m_transfers;
    JsonListModel *m_pendingArrivals;

    bool m_autoAccept = true;
    bool m_groupBySender = true;
    bool m_keepHistory = true;

    bool m_available = false;
    bool m_connected = false;
    int m_activeCount = 0;
    QString m_destinationRoot;
};
