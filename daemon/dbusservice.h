/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dev.drip.Daemon -- the applet's window onto the engine.
 *
 * Payloads are JSON strings rather than marshalled structs. D-Bus custom-type
 * registration would need matching boilerplate on both sides for no benefit;
 * QML consumes JSON natively.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class Tailnet;
class TransferManager;
class InboxWatcher;
class Settings;
struct Device;
struct Transfer;
struct PendingArrival;

class DBusService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "dev.drip.Daemon")

public:
    DBusService(Tailnet *tailnet, TransferManager *transfers, InboxWatcher *inbox, Settings *settings, QObject *parent = nullptr);

    /** Claim the well-known name. False if another dripd already holds it. */
    bool registerService();

    static QString serviceName();
    static QString objectPath();

    static QString toJson(const Device &device);
    static QString toJson(const Transfer &transfer);
    static QString toJson(const PendingArrival &arrival);

public Q_SLOTS:
    /** JSON array of devices. */
    Q_SCRIPTABLE QString devices() const;
    /** JSON array of transfers, newest first. */
    Q_SCRIPTABLE QString transfers() const;
    /** True when tailscaled is reachable and running. */
    Q_SCRIPTABLE bool connected() const;
    /** JSON object describing this machine. */
    Q_SCRIPTABLE QString self() const;

    /** Queue files (plain paths or file:// URLs). Returns transfer ids. */
    Q_SCRIPTABLE QStringList send(const QString &deviceId, const QStringList &paths);
    Q_SCRIPTABLE void cancel(const QString &transferId);
    Q_SCRIPTABLE void clearHistory();
    Q_SCRIPTABLE void refresh();
    Q_SCRIPTABLE QString destinationRoot() const;

    /** JSON object: destinationRoot, autoAccept, groupBySender. */
    Q_SCRIPTABLE QString settings() const;
    /** Merge a JSON object of settings; unknown keys are ignored. */
    Q_SCRIPTABLE void updateSettings(const QString &json);

    /** JSON array of arrivals awaiting a decision. */
    Q_SCRIPTABLE QString pendingArrivals() const;
    Q_SCRIPTABLE void acceptArrival(const QString &name);
    Q_SCRIPTABLE void declineArrival(const QString &name);

Q_SIGNALS:
    Q_SCRIPTABLE void devicesChanged(const QString &json);
    Q_SCRIPTABLE void transferAdded(const QString &json);
    Q_SCRIPTABLE void transferUpdated(const QString &json);
    Q_SCRIPTABLE void historyCleared();
    Q_SCRIPTABLE void connectedChanged(bool connected);
    Q_SCRIPTABLE void settingsChanged(const QString &json);
    Q_SCRIPTABLE void pendingArrivalsChanged(const QString &json);
    /** One arrival, so the applet can raise itself for a decision. */
    Q_SCRIPTABLE void arrivalPending(const QString &json);

private:
    Tailnet *m_tailnet;
    TransferManager *m_transfers;
    InboxWatcher *m_inbox;
    Settings *m_settings;
};
