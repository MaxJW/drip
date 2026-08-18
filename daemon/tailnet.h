/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tailnet state: the device list, kept live off the tailscaled event bus.
 */

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class LocalApi;
class LocalApiReply;
class QTimer;

/** Mirrors ipnstate.TaildropTargetStatus. */
enum class TaildropTarget {
    Unknown = 0,
    Available = 1,
    NoNetmapAvailable = 2,
    IpnStateNotRunning = 3,
    MissingCap = 4,
    Offline = 5,
    NoPeerInfo = 6,
    UnsupportedOS = 7,
    NoPeerAPI = 8,
    OwnedByOtherUser = 9,
};

struct Device {
    QString stableId; ///< StableNodeID, the address for file-put
    QString publicKey; ///< "nodekey:..." -- how the IPN bus identifies peers
    QString hostName; ///< short name, e.g. "work-laptop"
    QString displayName; ///< prettified for the UI
    QString os; ///< linux / iOS / android / windows / macOS
    QString ownerName; ///< Tailscale account display name
    QString avatarUrl; ///< from the account profile; may be empty
    QString primaryIp;
    QDateTime lastSeen; ///< invalid for peers that have never been away
    bool online = false;
    bool isSelf = false;
    TaildropTarget target = TaildropTarget::Unknown;

    bool canReceive() const
    {
        return target == TaildropTarget::Available;
    }
    /** User-facing explanation when canReceive() is false. */
    QString unavailableReason() const;

    /** Compares what the UI draws. Excludes lastSeen, which ticks constantly. */
    bool operator==(const Device &other) const
    {
        return stableId == other.stableId && displayName == other.displayName && os == other.os && ownerName == other.ownerName
            && avatarUrl == other.avatarUrl && primaryIp == other.primaryIp && online == other.online && target == other.target;
    }
};

/**
 * Owns the tailscaled connection and republishes the device list.
 *
 * Driven by the IPN event bus, a long-lived streaming request whose
 * notifications trigger a coalesced status refresh, plus a slow liveness poll
 * for peer sleep/wake, which the bus does not report.
 */
class Tailnet : public QObject
{
    Q_OBJECT

public:
    explicit Tailnet(LocalApi *api, QObject *parent = nullptr);

    QList<Device> devices() const
    {
        return m_devices;
    }
    Device deviceById(const QString &stableId) const;
    Device deviceByPublicKey(const QString &publicKey) const;
    Device self() const
    {
        return m_self;
    }

    /**
     * Best guess at who just sent us something, inferred from recent wire
     * activity. Invalid Device when nothing plausible is in the window.
     */
    Device mostRecentlyActivePeer(int withinSeconds = 120) const;
    bool connected() const
    {
        return m_connected;
    }
    QString backendState() const
    {
        return m_backendState;
    }

    /** Connect to the bus and load the first snapshot. */
    void start();
    /** Force a status refresh; coalesced against in-flight requests. */
    void refresh();

Q_SIGNALS:
    void devicesChanged();
    void connectedChanged(bool connected);
    /** A peer showed signs of life; used to attribute inbound files. */
    void peerActivity(const QString &stableId);

private:
    void connectBus();
    void scheduleReconnect();
    void setConnected(bool connected);
    void handleStatus(const QByteArray &json);

    LocalApi *m_api;
    LocalApiReply *m_bus = nullptr;
    LocalApiReply *m_statusReply = nullptr;
    QTimer *m_reconnectTimer;
    QTimer *m_refreshDebounce;
    /** The event bus is silent on peer sleep/wake, so liveness is polled. */
    QTimer *m_livenessTimer;

    QList<Device> m_devices;
    Device m_self;
    QString m_backendState;
    QHash<QString, QDateTime> m_lastActivity; ///< publicKey -> last seen on the bus
    bool m_connected = false;
    int m_backoffMs = 500;
};
