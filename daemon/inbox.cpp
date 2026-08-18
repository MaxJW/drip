/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "inbox.h"
#include "localapi.h"
#include "tailnet.h"
#include "transfers.h"
#include "util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

namespace
{
/** tailscaled blocks for this long, so an idle poll costs nothing. */
constexpr int WaitSeconds = 25;
constexpr int MaxBackoffMs = 30000;
/** Re-ask interval while an arrival is undecided and waitsec cannot block. */
constexpr int IdlePollMs = 3000;

bool containsName(const QList<PendingArrival> &list, const QString &name)
{
    for (const PendingArrival &item : list) {
        if (item.name == name) {
            return true;
        }
    }
    return false;
}
}

InboxWatcher::InboxWatcher(LocalApi *api, Tailnet *tailnet, TransferManager *transfers, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_tailnet(tailnet)
    , m_transfers(transfers)
    , m_retryTimer(new QTimer(this))
    , m_idleTimer(new QTimer(this))
{
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &InboxWatcher::poll);

    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(IdlePollMs);
    connect(m_idleTimer, &QTimer::timeout, this, &InboxWatcher::poll);

    m_root = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(QStringLiteral("Drip"));
}

void InboxWatcher::setDestinationRoot(const QString &path)
{
    m_root = path;
}

void InboxWatcher::setGroupBySender(bool group)
{
    m_groupBySender = group;
}

void InboxWatcher::setAutoAccept(bool autoAccept)
{
    if (autoAccept == m_autoAccept) {
        return;
    }
    m_autoAccept = autoAccept;

    if (!m_autoAccept || m_pending.isEmpty()) {
        return;
    }

    // Adopt anything already waiting, so no prompt is stranded.
    for (const PendingArrival &arrival : std::as_const(m_pending)) {
        Q_EMIT arrivalResolved(arrival.name);
    }
    m_queue += m_pending;
    m_pending.clear();
    Q_EMIT pendingArrivalsChanged();
    if (!m_fetch) {
        m_idleTimer->stop();
        processNext();
    }
}

void InboxWatcher::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    poll();
}

void InboxWatcher::stop()
{
    m_running = false;
    m_retryTimer->stop();
    m_idleTimer->stop();
    if (m_poll) {
        m_poll->abort();
        m_poll->deleteLater();
        m_poll = nullptr;
    }
    if (m_fetch) {
        m_fetch->abort();
        m_fetch->deleteLater();
        m_fetch = nullptr;
    }
}

void InboxWatcher::scheduleRetry()
{
    if (!m_running || m_retryTimer->isActive()) {
        return;
    }
    m_retryTimer->start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, MaxBackoffMs);
}

void InboxWatcher::poll()
{
    if (!m_running || m_poll || m_fetch) {
        return;
    }

    m_poll = m_api->get(QStringLiteral("/localapi/v0/files/?waitsec=%1").arg(WaitSeconds));

    connect(m_poll, &LocalApiReply::finished, this, [this] {
        const QByteArray body = m_poll->body();
        m_poll->deleteLater();
        m_poll = nullptr;
        m_backoffMs = 1000;

        // "null" is the idle answer: the long poll expired with an empty inbox.
        const QJsonArray files = QJsonDocument::fromJson(body).array();
        QList<PendingArrival> waiting;
        waiting.reserve(files.size());
        for (const QJsonValue &value : files) {
            const QJsonObject file = value.toObject();
            const QString name = file.value(QStringLiteral("Name")).toString();
            if (name.isEmpty()) {
                continue;
            }
            PendingArrival arrival;
            arrival.name = name;
            arrival.size = static_cast<qint64>(file.value(QStringLiteral("Size")).toDouble());
            waiting.append(arrival);
        }

        reconcile(waiting);
    });

    connect(m_poll, &LocalApiReply::errored, this, [this] {
        m_poll->deleteLater();
        m_poll = nullptr;
        scheduleRetry();
    });
}

void InboxWatcher::reconcile(const QList<PendingArrival> &waiting)
{
    QStringList waitingNames;
    waitingNames.reserve(waiting.size());
    for (const PendingArrival &item : waiting) {
        waitingNames.append(item.name);
    }

    // Gone from the listing means another client took it.
    bool dropped = false;
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        if (waitingNames.contains(m_pending.at(i).name)) {
            continue;
        }
        Q_EMIT arrivalResolved(m_pending.at(i).name);
        m_pending.removeAt(i);
        dropped = true;
    }
    if (dropped) {
        Q_EMIT pendingArrivalsChanged();
    }

    bool announced = false;
    for (const PendingArrival &item : waiting) {
        if (item.name == m_fetching || containsName(m_queue, item.name) || containsName(m_pending, item.name)) {
            continue;
        }

        // WaitingFile has no sender field, so infer it from recent wire
        // activity -- at arrival, before the peer set can change.
        const Device sender = m_tailnet->mostRecentlyActivePeer();
        PendingArrival arrival = item;
        arrival.senderId = sender.stableId;
        arrival.senderName = sender.displayName.isEmpty() ? QStringLiteral("Unknown device") : sender.displayName;
        arrival.arrivedAt = QDateTime::currentDateTimeUtc();

        if (m_autoAccept) {
            m_queue.append(arrival);
        } else {
            m_pending.append(arrival);
            announced = true;
            Q_EMIT arrivalPending(arrival);
        }
    }
    if (announced) {
        Q_EMIT pendingArrivalsChanged();
    }

    pumpOrWait();
}

void InboxWatcher::pumpOrWait()
{
    if (!m_running) {
        return;
    }
    if (m_fetch) {
        return; // the fetch will call back round
    }
    if (!m_queue.isEmpty()) {
        processNext();
        return;
    }
    if (!m_pending.isEmpty()) {
        m_idleTimer->start();
        return;
    }
    poll();
}

void InboxWatcher::processNext()
{
    if (m_queue.isEmpty()) {
        pumpOrWait();
        return;
    }
    fetch(m_queue.takeFirst());
}

void InboxWatcher::accept(const QString &name)
{
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).name != name) {
            continue;
        }
        m_queue.append(m_pending.takeAt(i));
        Q_EMIT arrivalResolved(name);
        Q_EMIT pendingArrivalsChanged();
        m_idleTimer->stop();
        if (!m_fetch) {
            processNext();
        }
        return;
    }
}

void InboxWatcher::decline(const QString &name)
{
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).name != name) {
            continue;
        }
        m_pending.removeAt(i);
        Q_EMIT arrivalResolved(name);
        Q_EMIT pendingArrivalsChanged();
        discard(name);
        return;
    }
}

void InboxWatcher::discard(const QString &name)
{
    LocalApiReply *del = m_api->remove(QStringLiteral("/localapi/v0/files/%1").arg(LocalApi::encodeSegment(name)));
    const auto done = [this, del] {
        del->deleteLater();
        m_idleTimer->stop();
        if (!m_poll && !m_fetch) {
            pumpOrWait();
        }
    };
    connect(del, &LocalApiReply::finished, this, done);
    connect(del, &LocalApiReply::errored, this, done);
}

void InboxWatcher::fetch(const PendingArrival &arrival)
{
    const QString name = arrival.name;
    const qint64 size = arrival.size;
    const QString senderName = arrival.senderName;

    QString directory = m_root;
    if (m_groupBySender) {
        directory = QDir(m_root).filePath(senderName);
    }
    if (!QDir().mkpath(directory)) {
        Q_EMIT receiveFailed(name, QStringLiteral("cannot create %1").arg(directory));
        pumpOrWait();
        return;
    }

    const QString finalPath = drip::uniquePath(directory, name);
    const QString partPath = finalPath + QStringLiteral(".part");

    auto *file = new QFile(partPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = file->errorString();
        delete file;
        Q_EMIT receiveFailed(name, error);
        pumpOrWait();
        return;
    }

    m_fetching = name;
    const QString transferId = m_transfers->beginIncoming(name, size, arrival.senderId, senderName);

    m_fetch = m_api->stream(QStringLiteral("/localapi/v0/files/%1").arg(LocalApi::encodeSegment(name)));
    file->setParent(m_fetch);

    auto *received = new qint64(0);
    connect(m_fetch, &QObject::destroyed, m_fetch, [received] {
        delete received;
    });

    // An error response must not be written to disk as though it were the file.
    auto *rejected = new bool(false);
    connect(m_fetch, &QObject::destroyed, m_fetch, [rejected] {
        delete rejected;
    });
    connect(m_fetch, &LocalApiReply::headers, this, [rejected](int statusCode) {
        *rejected = statusCode < 200 || statusCode >= 300;
    });

    connect(m_fetch, &LocalApiReply::chunk, this, [this, file, received, rejected, transferId](const QByteArray &data) {
        if (*rejected) {
            return; // the body is an error message, not our file
        }
        if (file->write(data) != data.size()) {
            return; // surfaced by the size check at completion
        }
        *received += data.size();
        m_transfers->updateIncoming(transferId, *received);
    });

    // Releases the reply only; m_fetching is held until the DELETE lands.
    const auto cleanup = [this] {
        if (m_fetch) {
            m_fetch->deleteLater();
            m_fetch = nullptr;
        }
    };

    connect(m_fetch, &LocalApiReply::finished, this, [this, file, finalPath, partPath, name, size, received, rejected, transferId, senderName, cleanup] {
        file->close();

        QString error;
        if (*rejected) {
            error = QStringLiteral("tailscaled would not hand it over (HTTP %1)").arg(m_fetch ? m_fetch->statusCode() : 0);
        } else if (size >= 0 && *received != size) {
            error = QStringLiteral("truncated: got %1 of %2 bytes").arg(*received).arg(size);
        } else if (!QFile::rename(partPath, finalPath)) {
            error = QStringLiteral("could not move into place");
        }

        if (error.isEmpty()) {
            // Owned by us because we wrote it; make the mode explicit anyway.
            QFile::setPermissions(finalPath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                                      | QFileDevice::ReadOther);
            m_transfers->finishIncoming(transferId, finalPath);
            Q_EMIT fileReceived(finalPath, senderName);
            cleanup();

            // Delete only once the file is safely on disk, and do not poll
            // again until the DELETE lands -- an earlier poll would still see
            // the file listed and fetch it a second time.
            LocalApiReply *del = m_api->remove(QStringLiteral("/localapi/v0/files/%1").arg(LocalApi::encodeSegment(name)));
            const auto deleted = [this, del] {
                del->deleteLater();
                m_fetching.clear();
                pumpOrWait();
            };
            connect(del, &LocalApiReply::finished, this, deleted);
            connect(del, &LocalApiReply::errored, this, deleted);
            return;
        }

        QFile::remove(partPath);
        m_transfers->finishIncoming(transferId, {}, error);
        Q_EMIT receiveFailed(name, error);
        cleanup();
        m_fetching.clear();
        pumpOrWait();
    });

    connect(m_fetch, &LocalApiReply::errored, this, [this, file, partPath, name, transferId, cleanup](const QString &message) {
        file->close();
        QFile::remove(partPath);
        m_transfers->finishIncoming(transferId, {}, message);
        Q_EMIT receiveFailed(name, message);
        cleanup();
        m_fetching.clear();
        pumpOrWait();
    });
}

#include "moc_inbox.cpp"
