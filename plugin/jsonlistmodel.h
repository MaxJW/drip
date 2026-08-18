/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A list model backed by a JSON array, with roles derived from the object keys.
 *
 * Devices and transfers have different shapes but identical plumbing, so both
 * use this rather than two near-identical QAbstractListModel subclasses.
 */

#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QQmlEngine>
#include <functional>

class JsonListModel : public QAbstractListModel
{
    Q_OBJECT
    // Never constructed from QML, but registering it lets the QML tooling
    // resolve count and get on the models DripClient exposes.
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit JsonListModel(const QList<QByteArray> &roleNames, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** Replace everything. Used for full refreshes. */
    void reset(const QJsonArray &items);

    /**
     * Insert or update one item, matched on "id". Updates emit dataChanged for
     * just that row so a progress ring animates instead of the list flickering.
     */
    void upsert(const QJsonObject &item);

    void removeById(const QString &id);
    void clear();

    /** Drop every row for which the predicate is true. */
    void removeIf(const std::function<bool(const QJsonObject &)> &predicate);

    Q_INVOKABLE QVariantMap get(int row) const;
    int indexOfId(const QString &id) const;

    /** The rows as they came off the wire, for C++-side filtering. */
    const QList<QJsonObject> &items() const
    {
        return m_items;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<QJsonObject> m_items;
    QHash<int, QByteArray> m_roles;
};
