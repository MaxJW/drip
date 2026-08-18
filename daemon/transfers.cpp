/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "transfers.h"
#include "localapi.h"
#include "tailnet.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace
{
constexpr int MaxHistory = 60;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

TransferManager::TransferManager(LocalApi *api, Tailnet *tailnet, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_tailnet(tailnet)
{
}

Transfer *TransferManager::find(const QString &id)
{
    for (Transfer &transfer : m_ledger) {
        if (transfer.id == id) {
            return &transfer;
        }
    }
    return nullptr;
}

Transfer TransferManager::transfer(const QString &id) const
{
    for (const Transfer &transfer : m_ledger) {
        if (transfer.id == id) {
            return transfer;
        }
    }
    return {};
}

int TransferManager::activeCount() const
{
    int count = 0;
    for (const Transfer &transfer : m_ledger) {
        if (!transfer.isFinished()) {
            ++count;
        }
    }
    return count;
}

void TransferManager::publish(const Transfer &transfer)
{
    Q_EMIT transferUpdated(transfer);

    // History off: the ledger is a live view. Pruned after publishing so the
    // final state, including any error, still reaches the UI.
    if (!m_keepHistory && transfer.isFinished()) {
        m_ledger.removeIf([](const Transfer &item) {
            return item.isFinished();
        });
        Q_EMIT historyCleared();
    }
}

void TransferManager::setKeepHistory(bool keep)
{
    if (keep == m_keepHistory) {
        return;
    }
    m_keepHistory = keep;
    if (!m_keepHistory) {
        clearHistory();
    }
}

QStringList TransferManager::sendAll(const QString &deviceId, const QStringList &filePaths)
{
    QStringList ids;
    ids.reserve(filePaths.size());
    for (const QString &path : filePaths) {
        const QString id = send(deviceId, path);
        if (!id.isEmpty()) {
            ids += id;
        }
    }
    return ids;
}

QString TransferManager::send(const QString &deviceId, const QString &filePath)
{
    const QFileInfo info(filePath);
    const Device device = m_tailnet->deviceById(deviceId);

    Transfer transfer;
    transfer.id = newId();
    transfer.deviceId = deviceId;
    transfer.deviceName = device.displayName.isEmpty() ? deviceId : device.displayName;
    transfer.fileName = info.fileName();
    transfer.localPath = info.absoluteFilePath();
    transfer.size = info.size();
    transfer.direction = TransferDirection::Outgoing;
    transfer.queuedAt = QDateTime::currentDateTime();

    // An unreadable path still gets a ledger entry, so the drop reports itself
    // rather than silently amounting to nothing.
    if (!info.exists() || !info.isFile()) {
        transfer.state = TransferState::Failed;
        transfer.error = info.exists() ? QStringLiteral("not a file")
                                       : QStringLiteral("cannot read %1").arg(filePath);
        transfer.finishedAt = QDateTime::currentDateTime();
        m_ledger.prepend(transfer);
        trimHistory();
        Q_EMIT transferAdded(transfer);
        publish(transfer);
        return transfer.id;
    }

    transfer.state = TransferState::Queued;
    m_ledger.prepend(transfer);
    trimHistory();
    Q_EMIT transferAdded(transfer);

    m_queues[deviceId].enqueue(transfer.id);
    pumpDevice(deviceId);
    return transfer.id;
}

void TransferManager::pumpDevice(const QString &deviceId)
{
    // One in flight per device: concurrent uploads to the same peer would make
    // both progress rings meaningless.
    if (m_activeByDevice.contains(deviceId)) {
        return;
    }
    QQueue<QString> &queue = m_queues[deviceId];
    if (queue.isEmpty()) {
        m_queues.remove(deviceId);
        return;
    }
    const QString next = queue.dequeue();
    m_activeByDevice.insert(deviceId, next);
    startTransfer(next);
}

void TransferManager::startTransfer(const QString &transferId)
{
    Transfer *transfer = find(transferId);
    if (!transfer) {
        return;
    }

    auto *file = new QFile(transfer->localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        const QString error = file->errorString();
        delete file;
        transfer->state = TransferState::Failed;
        transfer->error = error;
        transfer->finishedAt = QDateTime::currentDateTime();
        const QString deviceId = transfer->deviceId;
        publish(*transfer);
        m_activeByDevice.remove(deviceId);
        pumpDevice(deviceId);
        return;
    }

    transfer->state = TransferState::Active;
    publish(*transfer);

    const QString path = QStringLiteral("/localapi/v0/file-put/%1/%2")
                             .arg(transfer->deviceId, LocalApi::encodeSegment(transfer->fileName));

    // The reply adopts the file handle, so it stays open exactly as long as the
    // transfer does.
    LocalApiReply *reply = m_api->put(path, file, transfer->size);
    m_replies.insert(transferId, reply);

    connect(reply, &LocalApiReply::uploadProgress, this, [this, transferId](qint64 sent, qint64) {
        Transfer *transfer = find(transferId);
        if (!transfer || transfer->state != TransferState::Active) {
            return;
        }
        transfer->transferred = sent;
        publish(*transfer);
    });

    const auto settle = [this, transferId](bool ok, const QString &error) {
        Transfer *transfer = find(transferId);
        if (!transfer) {
            return;
        }
        if (auto *reply = m_replies.take(transferId)) {
            reply->deleteLater();
        }
        if (transfer->state == TransferState::Cancelled) {
            // Already reported; just free the slot.
        } else if (ok) {
            transfer->state = TransferState::Completed;
            transfer->transferred = transfer->size;
        } else {
            transfer->state = TransferState::Failed;
            transfer->error = error;
        }
        transfer->finishedAt = QDateTime::currentDateTime();
        publish(*transfer);

        const QString deviceId = transfer->deviceId;
        m_activeByDevice.remove(deviceId);
        pumpDevice(deviceId);
    };

    connect(reply, &LocalApiReply::finished, this, [reply, settle] {
        const int status = reply->statusCode();
        if (status >= 200 && status < 300) {
            settle(true, {});
        } else {
            QString detail = QString::fromUtf8(reply->body()).trimmed();
            if (detail.isEmpty()) {
                detail = QStringLiteral("HTTP %1").arg(status);
            }
            settle(false, detail);
        }
    });
    connect(reply, &LocalApiReply::errored, this, [settle](const QString &message) {
        settle(false, message);
    });
}

void TransferManager::cancel(const QString &transferId)
{
    Transfer *transfer = find(transferId);
    if (!transfer || transfer->isFinished()) {
        return;
    }

    const QString deviceId = transfer->deviceId;
    const bool wasActive = m_activeByDevice.value(deviceId) == transferId;

    transfer->state = TransferState::Cancelled;
    transfer->finishedAt = QDateTime::currentDateTime();
    publish(*transfer);

    if (auto *reply = m_replies.take(transferId)) {
        reply->abort();
        reply->deleteLater();
    }

    if (wasActive) {
        m_activeByDevice.remove(deviceId);
        pumpDevice(deviceId);
    } else {
        m_queues[deviceId].removeAll(transferId);
    }
}

void TransferManager::clearHistory()
{
    const auto isStale = [](const Transfer &transfer) {
        return transfer.isFinished();
    };
    m_ledger.removeIf(isStale);
    Q_EMIT historyCleared();
}

void TransferManager::trimHistory()
{
    if (m_ledger.size() <= MaxHistory) {
        return;
    }
    // Drop the oldest finished entries; never evict something still running.
    for (int i = m_ledger.size() - 1; i >= 0 && m_ledger.size() > MaxHistory; --i) {
        if (m_ledger.at(i).isFinished()) {
            m_ledger.removeAt(i);
        }
    }
}

// ------------------------------------------------------------------- inbound

QString TransferManager::beginIncoming(const QString &fileName, qint64 size, const QString &deviceId, const QString &deviceName)
{
    Transfer transfer;
    transfer.id = newId();
    transfer.deviceId = deviceId;
    transfer.deviceName = deviceName;
    transfer.fileName = fileName;
    transfer.size = size;
    transfer.direction = TransferDirection::Incoming;
    transfer.state = TransferState::Active;
    transfer.queuedAt = QDateTime::currentDateTime();

    m_ledger.prepend(transfer);
    trimHistory();
    Q_EMIT transferAdded(transfer);
    return transfer.id;
}

void TransferManager::updateIncoming(const QString &id, qint64 received)
{
    Transfer *transfer = find(id);
    if (!transfer || transfer->state != TransferState::Active) {
        return;
    }
    transfer->transferred = received;
    publish(*transfer);
}

void TransferManager::finishIncoming(const QString &id, const QString &localPath, const QString &error)
{
    Transfer *transfer = find(id);
    if (!transfer) {
        return;
    }
    transfer->localPath = localPath;
    transfer->finishedAt = QDateTime::currentDateTime();
    if (error.isEmpty()) {
        transfer->state = TransferState::Completed;
        if (transfer->size > 0) {
            transfer->transferred = transfer->size;
        }
    } else {
        transfer->state = TransferState::Failed;
        transfer->error = error;
    }
    publish(*transfer);
}

#include "moc_transfers.cpp"
