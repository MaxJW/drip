/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Moves Taildrop arrivals out of tailscaled's staging area and onto disk.
 * Files are written by this process, so they are owned by the user running it.
 *
 * Taildrop has no pre-transfer handshake: by the time a file is listed, its
 * bytes have already arrived. accept() saves it; decline() deletes it.
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

class LocalApi;
class LocalApiReply;
class TransferManager;
class Tailnet;
class QTimer;

/** A file waiting in tailscaled's staging area for a yes or no. */
struct PendingArrival {
    QString name;
    qint64 size = 0;
    QString senderId;
    QString senderName;
    QDateTime arrivedAt;
};

class InboxWatcher : public QObject
{
    Q_OBJECT

public:
    InboxWatcher(LocalApi *api, Tailnet *tailnet, TransferManager *transfers, QObject *parent = nullptr);

    /** Begin long-polling. Idempotent. */
    void start();
    void stop();

    /** Where arrivals land; defaults to ~/Downloads/Drip. */
    void setDestinationRoot(const QString &path);
    QString destinationRoot() const
    {
        return m_root;
    }

    /** Sort arrivals into a per-sender subfolder. Default true. */
    void setGroupBySender(bool group);

    /** Off means arrivals wait for accept() or decline(). */
    void setAutoAccept(bool autoAccept);
    bool autoAccept() const
    {
        return m_autoAccept;
    }

    QList<PendingArrival> pendingArrivals() const
    {
        return m_pending;
    }
    void accept(const QString &name);
    void decline(const QString &name);

Q_SIGNALS:
    /** A file is fully written and owned by the user. */
    void fileReceived(const QString &path, const QString &senderName);
    void receiveFailed(const QString &fileName, const QString &error);

    void arrivalPending(const PendingArrival &arrival);
    /** Decided here, or taken by another client. Either way, withdraw the prompt. */
    void arrivalResolved(const QString &name);
    void pendingArrivalsChanged();

private:
    void poll();
    void reconcile(const QList<PendingArrival> &waiting);
    void processNext();
    void fetch(const PendingArrival &arrival);
    void scheduleRetry();
    void discard(const QString &name);
    /** Chooses the next action once a poll or fetch has settled. */
    void pumpOrWait();


    LocalApi *m_api;
    Tailnet *m_tailnet;
    TransferManager *m_transfers;
    QTimer *m_retryTimer;
    /** waitsec returns instantly while the inbox is non-empty; this paces it. */
    QTimer *m_idleTimer;

    LocalApiReply *m_poll = nullptr;
    LocalApiReply *m_fetch = nullptr;

    QString m_root;
    bool m_groupBySender = true;
    bool m_autoAccept = true;
    bool m_running = false;
    int m_backoffMs = 1000;

    /** Accepted, waiting to be pulled down. */
    QList<PendingArrival> m_queue;
    /** Awaiting a decision. */
    QList<PendingArrival> m_pending;
    /** In flight: in neither list, but still listed by tailscaled until deleted. */
    QString m_fetching;
};
