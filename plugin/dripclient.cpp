/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dripclient.h"
#include "jsonlistmodel.h"
#include "util.h"

#include <KIO/OpenFileManagerWindowJob>

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUrl>

namespace
{
constexpr auto ServiceName = "dev.drip.Daemon";
constexpr auto ObjectPath = "/Daemon";
constexpr auto InterfaceName = "dev.drip.Daemon";

const QList<QByteArray> DeviceRoles = {
    "id", "name", "hostName", "os", "owner", "avatarUrl", "ip", "online", "canReceive", "reason", "lastSeen",
};

const QList<QByteArray> TransferRoles = {
    "id", "deviceId", "deviceName", "fileName", "path", "size",
    "transferred", "progress", "incoming", "state", "error", "queuedAt", "finishedAt",
};

const QList<QByteArray> ArrivalRoles = {
    "name", "size", "senderId", "senderName", "arrivedAt",
};
}

DripClient::DripClient(QObject *parent)
    : QObject(parent)
    , m_devices(new JsonListModel(DeviceRoles, this))
    , m_transfers(new JsonListModel(TransferRoles, this))
    , m_pendingArrivals(new JsonListModel(ArrivalRoles, this))
{
    m_watcher = new QDBusServiceWatcher(QLatin1String(ServiceName),
                                        QDBusConnection::sessionBus(),
                                        QDBusServiceWatcher::WatchForOwnerChange,
                                        this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        connectToDaemon();
    });
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        setAvailable(false);
        m_devices->clear();
        m_pendingArrivals->clear();
    });

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &DripClient::clipboardChanged);

    connectToDaemon();
}

JsonListModel *DripClient::devicesModel() const
{
    return m_devices;
}

JsonListModel *DripClient::transfersModel() const
{
    return m_transfers;
}

JsonListModel *DripClient::pendingArrivalsModel() const
{
    return m_pendingArrivals;
}

void DripClient::connectToDaemon()
{
    if (m_daemon) {
        m_daemon->deleteLater();
        m_daemon = nullptr;
    }

    m_daemon = new QDBusInterface(QLatin1String(ServiceName),
                                  QLatin1String(ObjectPath),
                                  QLatin1String(InterfaceName),
                                  QDBusConnection::sessionBus(),
                                  this);
    if (!m_daemon->isValid()) {
        setAvailable(false);
        return;
    }

    // Signal name -> slot, connected by name because QDBusConnection::connect
    // takes a SLOT() signature rather than a pointer-to-member.
    static const QList<QPair<const char *, const char *>> signals = {
        {"devicesChanged", SLOT(onDevicesChanged(QString))},
        {"transferAdded", SLOT(onTransferAdded(QString))},
        {"transferUpdated", SLOT(onTransferUpdated(QString))},
        {"historyCleared", SLOT(onHistoryCleared())},
        {"settingsChanged", SLOT(onSettingsChanged(QString))},
        {"pendingArrivalsChanged", SLOT(onPendingArrivalsChanged(QString))},
        {"arrivalPending", SLOT(onArrivalPending(QString))},
    };
    for (const auto &[name, slot] : signals) {
        QDBusConnection::sessionBus().connect(QLatin1String(ServiceName),
                                              QLatin1String(ObjectPath),
                                              QLatin1String(InterfaceName),
                                              QLatin1String(name),
                                              this,
                                              slot);
    }

    setAvailable(true);
    syncAll();
}

void DripClient::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    Q_EMIT availableChanged();

    if (!available && m_connected) {
        m_connected = false;
        Q_EMIT connectedChanged();
    }
}

void DripClient::syncAll()
{
    if (!m_daemon || !m_daemon->isValid()) {
        return;
    }

    const QDBusReply<QString> devices = m_daemon->call(QStringLiteral("devices"));
    if (devices.isValid()) {
        m_devices->reset(QJsonDocument::fromJson(devices.value().toUtf8()).array());
    }

    const QDBusReply<QString> transfers = m_daemon->call(QStringLiteral("transfers"));
    if (transfers.isValid()) {
        m_transfers->reset(QJsonDocument::fromJson(transfers.value().toUtf8()).array());
        recountActive();
    }

    const QDBusReply<bool> connected = m_daemon->call(QStringLiteral("connected"));
    if (connected.isValid() && connected.value() != m_connected) {
        m_connected = connected.value();
        Q_EMIT connectedChanged();
    }

    const QDBusReply<QString> settings = m_daemon->call(QStringLiteral("settings"));
    if (settings.isValid()) {
        onSettingsChanged(settings.value());
    }

    const QDBusReply<QString> arrivals = m_daemon->call(QStringLiteral("pendingArrivals"));
    if (arrivals.isValid()) {
        m_pendingArrivals->reset(QJsonDocument::fromJson(arrivals.value().toUtf8()).array());
    }
}

void DripClient::onSettingsChanged(const QString &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    if (object.isEmpty()) {
        return;
    }

    const QString root = object.value(QStringLiteral("destinationRoot")).toString();
    const bool autoAccept = object.value(QStringLiteral("autoAccept")).toBool();
    const bool groupBySender = object.value(QStringLiteral("groupBySender")).toBool();
    const bool keepHistory = object.value(QStringLiteral("keepHistory")).toBool();

    if (root == m_destinationRoot && autoAccept == m_autoAccept && groupBySender == m_groupBySender
        && keepHistory == m_keepHistory) {
        return;
    }
    m_destinationRoot = root;
    m_autoAccept = autoAccept;
    m_groupBySender = groupBySender;
    m_keepHistory = keepHistory;
    Q_EMIT settingsChanged();
}

void DripClient::onPendingArrivalsChanged(const QString &json)
{
    m_pendingArrivals->reset(QJsonDocument::fromJson(json.toUtf8()).array());
}

void DripClient::onArrivalPending(const QString &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    Q_EMIT arrivalPending(object.value(QStringLiteral("name")).toString(),
                          object.value(QStringLiteral("senderName")).toString());
}

void DripClient::pushSetting(const QString &key, const QJsonValue &value)
{
    if (!m_daemon || !m_daemon->isValid()) {
        return;
    }
    // Local state is not updated here: dripd applies the change and signals
    // back, so a rejected value never sticks in the UI.
    QJsonObject patch;
    patch.insert(key, value);
    m_daemon->asyncCall(QStringLiteral("updateSettings"),
                        QString::fromUtf8(QJsonDocument(patch).toJson(QJsonDocument::Compact)));
}

void DripClient::setDestinationRoot(const QString &path)
{
    pushSetting(QStringLiteral("destinationRoot"), path);
}

void DripClient::setAutoAccept(bool on)
{
    pushSetting(QStringLiteral("autoAccept"), on);
}

void DripClient::setGroupBySender(bool on)
{
    pushSetting(QStringLiteral("groupBySender"), on);
}

void DripClient::setKeepHistory(bool on)
{
    pushSetting(QStringLiteral("keepHistory"), on);
}

void DripClient::acceptArrival(const QString &name)
{
    if (m_daemon && m_daemon->isValid()) {
        m_daemon->asyncCall(QStringLiteral("acceptArrival"), name);
    }
}

void DripClient::declineArrival(const QString &name)
{
    if (m_daemon && m_daemon->isValid()) {
        m_daemon->asyncCall(QStringLiteral("declineArrival"), name);
    }
}

void DripClient::recountActive()
{
    int active = 0;
    for (int i = 0; i < m_transfers->rowCount(); ++i) {
        const QString state = m_transfers->get(i).value(QStringLiteral("state")).toString();
        if (state == QLatin1String("active") || state == QLatin1String("queued")) {
            ++active;
        }
    }
    if (active != m_activeCount) {
        m_activeCount = active;
        Q_EMIT activeCountChanged();
    }
}

void DripClient::onDevicesChanged(const QString &json)
{
    m_devices->reset(QJsonDocument::fromJson(json.toUtf8()).array());
    Q_EMIT reachableIdsChanged();
}

QStringList DripClient::reachableIds() const
{
    QStringList ids;
    for (const QJsonObject &device : m_devices->items()) {
        if (device.value(QStringLiteral("canReceive")).toBool()) {
            ids.append(device.value(QStringLiteral("id")).toString());
        }
    }
    return ids;
}

QString DripClient::reachableName(const QString &deviceId) const
{
    for (const QJsonObject &device : m_devices->items()) {
        if (device.value(QStringLiteral("id")).toString() == deviceId) {
            return device.value(QStringLiteral("name")).toString();
        }
    }
    return {};
}

void DripClient::onTransferAdded(const QString &json)
{
    const QJsonObject item = QJsonDocument::fromJson(json.toUtf8()).object();
    m_transfers->upsert(item);
    recountActive();
}

void DripClient::onTransferUpdated(const QString &json)
{
    const QJsonObject item = QJsonDocument::fromJson(json.toUtf8()).object();
    m_transfers->upsert(item);
    recountActive();

    if (item.value(QStringLiteral("incoming")).toBool() && item.value(QStringLiteral("state")).toString() == QLatin1String("completed")) {
        Q_EMIT fileReceived(item.value(QStringLiteral("fileName")).toString(),
                            item.value(QStringLiteral("deviceName")).toString());
    }
}

void DripClient::onHistoryCleared()
{
    m_transfers->removeIf([](const QJsonObject &item) {
        const QString state = item.value(QStringLiteral("state")).toString();
        return state != QLatin1String("active") && state != QLatin1String("queued");
    });
    recountActive();
}

void DripClient::send(const QString &deviceId, const QStringList &paths)
{
    if (!m_daemon || paths.isEmpty()) {
        return;
    }
    m_daemon->asyncCall(QStringLiteral("send"), deviceId, paths);
}

void DripClient::cancel(const QString &transferId)
{
    if (m_daemon) {
        m_daemon->asyncCall(QStringLiteral("cancel"), transferId);
    }
}

void DripClient::clearHistory()
{
    if (m_daemon) {
        m_daemon->asyncCall(QStringLiteral("clearHistory"));
    }
}

void DripClient::refresh()
{
    if (m_daemon) {
        m_daemon->asyncCall(QStringLiteral("refresh"));
    }
}

void DripClient::openPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // A freshly chosen destination may not exist yet.
    const QFileInfo info(path);
    if (!info.exists() && !path.contains(QLatin1Char('.'))) {
        QDir().mkpath(path);
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void DripClient::showInFolder(const QString &path)
{
    KIO::highlightInFileManager({QUrl::fromLocalFile(path)});
}

QString DripClient::displayPath(const QString &path) const
{
    return drip::abbreviateHome(path);
}

QString DripClient::formatSize(double bytes) const
{
    return drip::humanSize(qint64(bytes));
}

QString DripClient::formatRelativeTime(const QString &isoTimestamp) const
{
    const QDateTime when = QDateTime::fromString(isoTimestamp, Qt::ISODate);
    if (!when.isValid()) {
        return {};
    }
    const qint64 seconds = when.secsTo(QDateTime::currentDateTime());
    if (seconds < 60) {
        return tr("just now");
    }
    if (seconds < 3600) {
        return tr("%n min ago", nullptr, int(seconds / 60));
    }
    if (seconds < 86400) {
        return tr("%n hr ago", nullptr, int(seconds / 3600));
    }
    if (seconds < 604800) {
        return tr("%n day(s) ago", nullptr, int(seconds / 86400));
    }
    return when.date().toString(QStringLiteral("d MMM"));
}

bool DripClient::clipboardSendable() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) {
        return false;
    }
    return mime->hasImage() || mime->hasUrls() || !mime->text().trimmed().isEmpty();
}

QString DripClient::clipboardSummary() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) {
        return {};
    }
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        return image.isNull() ? tr("Image")
                              : tr("Image · %1×%2").arg(image.width()).arg(image.height());
    }
    if (mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        return urls.size() == 1 ? QFileInfo(urls.first().toLocalFile()).fileName()
                                : tr("%n files", nullptr, int(urls.size()));
    }
    const QString text = mime->text().trimmed();
    if (text.isEmpty()) {
        return {};
    }
    return tr("Text · %1").arg(drip::humanSize(text.toUtf8().size()));
}

void DripClient::sendClipboard(const QString &deviceId)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || deviceId.isEmpty()) {
        return;
    }

    // Copied files are already files; send them rather than a copy.
    if (mime->hasUrls() && !mime->hasImage()) {
        QStringList paths;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile()) {
                paths += url.toLocalFile();
            }
        }
        if (!paths.isEmpty()) {
            send(deviceId, paths);
            return;
        }
    }

    const QString directory = drip::outgoingCacheDirectory();
    // A readable name, because this is what the other end sees arrive.
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));

    QString path;
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (image.isNull()) {
            return;
        }
        path = drip::uniquePath(directory, tr("Screenshot %1.png").arg(stamp));
        if (!image.save(path, "PNG")) {
            return;
        }
    } else {
        const QString text = mime->text();
        if (text.trimmed().isEmpty()) {
            return;
        }
        path = drip::uniquePath(directory, tr("Clipboard %1.txt").arg(stamp));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }
        file.write(text.toUtf8());
        file.close();
    }

    send(deviceId, {path});
}

QStringList DripClient::folderNames(const QStringList &paths) const
{
    QStringList names;
    for (const QString &entry : paths) {
        const QUrl url(entry);
        const QString local = url.isLocalFile() ? url.toLocalFile() : entry;
        const QFileInfo info(local);
        if (info.isDir()) {
            names += info.fileName();
        }
    }
    return names;
}

#include "moc_dripclient.cpp"
