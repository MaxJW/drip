/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The transfer ledger: everything drip is sending, receiving, or recently did.
 */

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>

class LocalApi;
class LocalApiReply;
class Tailnet;

enum class TransferDirection {
    Outgoing,
    Incoming,
};

enum class TransferState {
    Queued,
    Active,
    Completed,
    Failed,
    Cancelled,
};

struct Transfer {
    QString id;
    QString deviceId; ///< empty for inbound files we could not attribute
    QString deviceName;
    QString fileName;
    QString localPath; ///< source when outgoing, destination when incoming
    qint64 size = -1; ///< -1 when unknown
    qint64 transferred = 0;
    TransferDirection direction = TransferDirection::Outgoing;
    TransferState state = TransferState::Queued;
    QString error;
    QDateTime queuedAt;
    QDateTime finishedAt;

    bool isFinished() const
    {
        return state == TransferState::Completed || state == TransferState::Failed || state == TransferState::Cancelled;
    }
    /** 0.0 - 1.0, or -1 when the total is unknown. */
    double progress() const
    {
        if (state == TransferState::Completed) {
            return 1.0;
        }
        return size > 0 ? double(transferred) / double(size) : -1.0;
    }
};

/**
 * Owns outbound sends and the ledger that inbound receives also write into.
 *
 * Sends are serialised per device: two files to the same phone go one after the
 * other so a progress ring means something, while two different devices proceed
 * independently.
 */
class TransferManager : public QObject
{
    Q_OBJECT

public:
    TransferManager(LocalApi *api, Tailnet *tailnet, QObject *parent = nullptr);

    /** Queue one file. Returns the transfer id, or empty if the path is unusable. */
    QString send(const QString &deviceId, const QString &filePath);
    /** Queue several files to one device, preserving order. */
    QStringList sendAll(const QString &deviceId, const QStringList &filePaths);

    void cancel(const QString &transferId);
    /** Drop finished entries from the ledger. */
    void clearHistory();
    /** Off discards the record and drops entries as they finish. Running
     *  transfers are still tracked, so progress remains visible. */
    void setKeepHistory(bool keep);
    bool keepHistory() const
    {
        return m_keepHistory;
    }

    QList<Transfer> transfers() const
    {
        return m_ledger;
    }
    Transfer transfer(const QString &id) const;
    int activeCount() const;

    // --- Inbound, driven by InboxWatcher ---
    QString beginIncoming(const QString &fileName, qint64 size, const QString &deviceId, const QString &deviceName);
    void updateIncoming(const QString &id, qint64 received);
    void finishIncoming(const QString &id, const QString &localPath, const QString &error = {});

Q_SIGNALS:
    void transferAdded(const Transfer &transfer);
    void transferUpdated(const Transfer &transfer);
    void historyCleared();

private:
    void pumpDevice(const QString &deviceId);
    void startTransfer(const QString &transferId);
    Transfer *find(const QString &id);
    void publish(const Transfer &transfer);
    void trimHistory();

    LocalApi *m_api;
    Tailnet *m_tailnet;

    QList<Transfer> m_ledger;
    bool m_keepHistory = true;
    QHash<QString, QQueue<QString>> m_queues; ///< deviceId -> pending transfer ids
    QHash<QString, QString> m_activeByDevice; ///< deviceId -> active transfer id
    QHash<QString, LocalApiReply *> m_replies; ///< transferId -> in-flight request
};
