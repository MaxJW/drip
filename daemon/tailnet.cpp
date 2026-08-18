/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tailnet.h"
#include "localapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace
{
constexpr int MaxBackoffMs = 15000;
constexpr int RefreshDebounceMs = 150;
/** Peer liveness re-ask; the event bus does not report sleep or wake. */
constexpr int LivenessPollMs = 15000;
constexpr int StaleAfterDays = 30;

/** "desktop.tailnet-name.ts.net." -> "desktop" */
QString shortNameFromDnsName(const QString &dnsName)
{
    const qsizetype dot = dnsName.indexOf(QLatin1Char('.'));
    return dot > 0 ? dnsName.left(dot) : dnsName;
}

/** Product names that title-casing would otherwise mangle ("Iphone", "Ipad"). */
QString recase(const QString &word)
{
    static const QHash<QString, QString> known = {
        {QStringLiteral("iphone"), QStringLiteral("iPhone")},
        {QStringLiteral("ipad"), QStringLiteral("iPad")},
        {QStringLiteral("ipod"), QStringLiteral("iPod")},
        {QStringLiteral("imac"), QStringLiteral("iMac")},
        {QStringLiteral("macbook"), QStringLiteral("MacBook")},
        {QStringLiteral("appletv"), QStringLiteral("Apple TV")},
        {QStringLiteral("tv"), QStringLiteral("TV")},
        {QStringLiteral("pc"), QStringLiteral("PC")},
        {QStringLiteral("nas"), QStringLiteral("NAS")},
        {QStringLiteral("vm"), QStringLiteral("VM")},
        {QStringLiteral("pi"), QStringLiteral("Pi")},
    };
    const auto it = known.constFind(word.toLower());
    if (it != known.constEnd()) {
        return it.value();
    }
    return word.at(0).toUpper() + word.mid(1);
}

/** "work-laptop" -> "Work Laptop". */
QString prettify(const QString &hostName)
{
    QString out = hostName;
    out.replace(QLatin1Char('-'), QLatin1Char(' '));
    out.replace(QLatin1Char('_'), QLatin1Char(' '));
    const QStringList words = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList result;
    result.reserve(words.size());
    for (const QString &word : words) {
        result += recase(word);
    }
    return result.join(QLatin1Char(' '));
}

/** "Desktop" and "desktop" collapse to the same key; "home" and "pi" do not. */
QString nameKey(const QString &name)
{
    QString key;
    key.reserve(name.size());
    for (const QChar ch : name) {
        if (ch.isLetterOrNumber()) {
            key += ch.toLower();
        }
    }
    return key;
}

/**
 * The tailnet DNS label is the name you address a device by, and the one the
 * admin console and `tailscale status` show -- so it wins. The machine's own
 * hostname is used only to recover casing the DNS label flattened away
 * ("desktop" -> "Desktop"), never to substitute a different name: this pi
 * reports hostname "home" but is addressed as "pi", and showing "Home" in a
 * send target would be actively misleading.
 */
QString displayNameFor(const QString &hostName, const QString &dnsLabel)
{
    if (dnsLabel.isEmpty()) {
        return prettify(hostName);
    }
    if (!hostName.isEmpty() && nameKey(hostName) == nameKey(dnsLabel) && hostName != hostName.toLower()) {
        return hostName;
    }
    return prettify(dnsLabel);
}
}

QString Device::unavailableReason() const
{
    switch (target) {
    case TaildropTarget::Available:
        return {};
    case TaildropTarget::Offline:
        // Mobile suspends the Tailscale extension in the background, dropping
        // the peerapi listener while the node still looks connected elsewhere.
        if (os == QLatin1String("iOS") || os == QLatin1String("android")) {
            return QStringLiteral("Asleep — open Tailscale on it");
        }
        return QStringLiteral("Offline");
    case TaildropTarget::OwnedByOtherUser:
        return QStringLiteral("Owned by someone else");
    case TaildropTarget::UnsupportedOS:
        return QStringLiteral("Taildrop not supported");
    case TaildropTarget::MissingCap:
        return QStringLiteral("Taildrop not enabled");
    case TaildropTarget::IpnStateNotRunning:
        return QStringLiteral("Tailscale not running");
    case TaildropTarget::NoPeerAPI:
    case TaildropTarget::NoPeerInfo:
    case TaildropTarget::NoNetmapAvailable:
        return QStringLiteral("Not reachable");
    case TaildropTarget::Unknown:
        break;
    }
    return QStringLiteral("Unavailable");
}

Tailnet::Tailnet(LocalApi *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_reconnectTimer(new QTimer(this))
    , m_refreshDebounce(new QTimer(this))
    , m_livenessTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &Tailnet::connectBus);

    // Bus notifications arrive in bursts; one status fetch per burst is plenty.
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(RefreshDebounceMs);
    connect(m_refreshDebounce, &QTimer::timeout, this, &Tailnet::refresh);

    m_livenessTimer->setInterval(LivenessPollMs);
    connect(m_livenessTimer, &QTimer::timeout, this, &Tailnet::refresh);
}

Device Tailnet::deviceById(const QString &stableId) const
{
    for (const Device &device : m_devices) {
        if (device.stableId == stableId) {
            return device;
        }
    }
    return {};
}

Device Tailnet::deviceByPublicKey(const QString &publicKey) const
{
    for (const Device &device : m_devices) {
        if (device.publicKey == publicKey) {
            return device;
        }
    }
    return {};
}

Device Tailnet::mostRecentlyActivePeer(int withinSeconds) const
{
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-withinSeconds);
    QString bestKey;
    QDateTime bestAt;
    for (auto it = m_lastActivity.constBegin(); it != m_lastActivity.constEnd(); ++it) {
        if (it.value() < cutoff) {
            continue;
        }
        if (!bestAt.isValid() || it.value() > bestAt) {
            bestAt = it.value();
            bestKey = it.key();
        }
    }
    return bestKey.isEmpty() ? Device{} : deviceByPublicKey(bestKey);
}

void Tailnet::start()
{
    connectBus();
    refresh();
}

void Tailnet::connectBus()
{
    if (m_bus) {
        m_bus->abort();
        m_bus->deleteLater();
        m_bus = nullptr;
    }

    m_bus = m_api->stream(QStringLiteral("/localapi/v0/watch-ipn-bus?mask=1"));

    connect(m_bus, &LocalApiReply::headers, this, [this](int status) {
        if (status == 200) {
            m_backoffMs = 500;
            setConnected(true);
        }
    });

    connect(m_bus, &LocalApiReply::line, this, [this](const QByteArray &record) {
        const QJsonObject notify = QJsonDocument::fromJson(record).object();

        // Engine status carries per-peer handshake activity, which is the only
        // hint available for attributing an inbound file to a sender.
        const QJsonObject engine = notify.value(QStringLiteral("Engine")).toObject();
        const QJsonObject livePeers = engine.value(QStringLiteral("LivePeers")).toObject();
        for (auto it = livePeers.begin(); it != livePeers.end(); ++it) {
            // Keyed by node key, which is why Device carries publicKey.
            m_lastActivity.insert(it.key(), QDateTime::currentDateTimeUtc());
            Q_EMIT peerActivity(it.key());
        }

        // contains(), not isNull(): an absent key reads as Undefined, for
        // which isNull() is false.
        if (notify.contains(QStringLiteral("NetMap")) || notify.contains(QStringLiteral("State"))
            || notify.contains(QStringLiteral("Prefs"))) {
            m_refreshDebounce->start();
        }
    });

    connect(m_bus, &LocalApiReply::errored, this, [this](const QString &message) {
        Q_UNUSED(message)
        setConnected(false);
        scheduleReconnect();
    });
    connect(m_bus, &LocalApiReply::finished, this, [this] {
        setConnected(false);
        scheduleReconnect();
    });
}

void Tailnet::scheduleReconnect()
{
    if (m_reconnectTimer->isActive()) {
        return;
    }
    m_reconnectTimer->start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, MaxBackoffMs);
}

void Tailnet::setConnected(bool connected)
{
    if (m_connected == connected) {
        return;
    }
    m_connected = connected;
    if (!connected) {
        m_livenessTimer->stop();
        // Keep the last known device list, but nothing is reachable now.
        for (Device &device : m_devices) {
            device.online = false;
            device.target = TaildropTarget::IpnStateNotRunning;
        }
        Q_EMIT devicesChanged();
    } else {
        m_livenessTimer->start();
        refresh();
    }
    Q_EMIT connectedChanged(connected);
}

void Tailnet::refresh()
{
    if (m_statusReply) {
        return; // coalesce; the in-flight fetch will publish fresh data
    }

    m_statusReply = m_api->get(QStringLiteral("/localapi/v0/status"));
    connect(m_statusReply, &LocalApiReply::finished, this, [this] {
        const QByteArray body = m_statusReply->body();
        m_statusReply->deleteLater();
        m_statusReply = nullptr;
        handleStatus(body);
    });
    connect(m_statusReply, &LocalApiReply::errored, this, [this] {
        m_statusReply->deleteLater();
        m_statusReply = nullptr;
        setConnected(false);
        scheduleReconnect();
    });
}

void Tailnet::handleStatus(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.isEmpty()) {
        return;
    }

    m_backendState = root.value(QStringLiteral("BackendState")).toString();
    setConnected(m_backendState == QLatin1String("Running"));

    // User profiles carry the avatars; peers reference them by UserID.
    const QJsonObject users = root.value(QStringLiteral("User")).toObject();
    const auto profileFor = [&users](qint64 userId) {
        return users.value(QString::number(userId)).toObject();
    };

    const auto parseNode = [&](const QJsonObject &node, bool isSelf) {
        Device device;
        device.stableId = node.value(QStringLiteral("ID")).toString();
        device.publicKey = node.value(QStringLiteral("PublicKey")).toString();
        device.hostName = shortNameFromDnsName(node.value(QStringLiteral("DNSName")).toString());
        if (device.hostName.isEmpty()) {
            device.hostName = node.value(QStringLiteral("HostName")).toString();
        }
        device.displayName = displayNameFor(node.value(QStringLiteral("HostName")).toString(), device.hostName);
        device.os = node.value(QStringLiteral("OS")).toString();
        device.online = node.value(QStringLiteral("Online")).toBool();
        device.isSelf = isSelf;
        // Go marshals "never" as year 1, which parses fine but means nothing.
        const QDateTime lastSeen = QDateTime::fromString(node.value(QStringLiteral("LastSeen")).toString(), Qt::ISODateWithMs);
        device.lastSeen = lastSeen.date().year() > 2000 ? lastSeen : QDateTime();
        device.target = static_cast<TaildropTarget>(node.value(QStringLiteral("TaildropTarget")).toInt());

        const QJsonArray ips = node.value(QStringLiteral("TailscaleIPs")).toArray();
        if (!ips.isEmpty()) {
            device.primaryIp = ips.first().toString();
        }

        const QJsonObject profile = profileFor(static_cast<qint64>(node.value(QStringLiteral("UserID")).toDouble()));
        device.ownerName = profile.value(QStringLiteral("DisplayName")).toString();
        device.avatarUrl = profile.value(QStringLiteral("ProfilePicURL")).toString();

        // An appliance with no PeerAPI endpoint can never accept a file, and
        // showing it as a target just invites failed drops.
        const QJsonArray peerApi = node.value(QStringLiteral("PeerAPIURL")).toArray();
        if (peerApi.isEmpty() && !isSelf && device.target == TaildropTarget::Available) {
            device.target = TaildropTarget::NoPeerAPI;
        }
        return device;
    };

    m_self = parseNode(root.value(QStringLiteral("Self")).toObject(), true);

    QList<Device> devices;
    const QJsonObject peers = root.value(QStringLiteral("Peer")).toObject();
    const QDateTime staleBefore = QDateTime::currentDateTimeUtc().addDays(-StaleAfterDays);

    for (auto it = peers.begin(); it != peers.end(); ++it) {
        const QJsonObject node = it.value().toObject();
        Device device = parseNode(node, false);
        if (device.stableId.isEmpty()) {
            continue;
        }
        // A peer nobody has seen in a month is not a plausible drop target;
        // showing it just crowds the row with junk like an old Chromecast.
        if (!device.online && device.lastSeen.isValid() && device.lastSeen < staleBefore) {
            continue;
        }
        devices.append(device);
    }

    // Sendable first, then online, then alphabetical -- the row should put what
    // you can actually use nearest the left edge.
    std::sort(devices.begin(), devices.end(), [](const Device &a, const Device &b) {
        if (a.canReceive() != b.canReceive()) {
            return a.canReceive();
        }
        if (a.online != b.online) {
            return a.online;
        }
        return a.displayName.localeAwareCompare(b.displayName) < 0;
    });

    // Republishing unchanged data would reset the QML model on a timer.
    if (devices == m_devices) {
        return;
    }
    m_devices = devices;
    Q_EMIT devicesChanged();
}

#include "moc_tailnet.cpp"
