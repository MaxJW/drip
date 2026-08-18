/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * User preferences, in ~/.config/driprc. Owned by the daemon because it
 * receives files with no UI running.
 */

#pragma once

#include <KSharedConfig>

#include <QObject>
#include <QString>

class Settings : public QObject
{
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

    QString destinationRoot() const
    {
        return m_destinationRoot;
    }
    /** Pull arrivals straight to disk, rather than asking first. */
    bool autoAccept() const
    {
        return m_autoAccept;
    }
    /** Sort arrivals into a per-sender subfolder. */
    bool groupBySender() const
    {
        return m_groupBySender;
    }
    /** Keep a record of finished transfers. */
    bool keepHistory() const
    {
        return m_keepHistory;
    }

    void setDestinationRoot(const QString &path);
    void setAutoAccept(bool on);
    void setGroupBySender(bool on);
    void setKeepHistory(bool on);

    /** Merge a JSON object; unknown keys are ignored. */
    void applyJson(const QString &json);
    QString toJson() const;

    static QString defaultDestinationRoot();

    /**
     * Expands "~", strips a file:// scheme and drops a trailing separator, so a
     * path typed by hand and one returned by a folder dialog compare equal.
     */
    static QString normalisePath(const QString &path);

Q_SIGNALS:
    void changed();
    void destinationRootChanged(const QString &path);
    void autoAcceptChanged(bool on);

private:
    void load();
    void save();

    KSharedConfig::Ptr m_config;
    QString m_destinationRoot;
    bool m_autoAccept = true;
    bool m_groupBySender = true;
    bool m_keepHistory = true;
};
