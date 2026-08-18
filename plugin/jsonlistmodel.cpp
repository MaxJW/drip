/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jsonlistmodel.h"

JsonListModel::JsonListModel(const QList<QByteArray> &roleNames, QObject *parent)
    : QAbstractListModel(parent)
{
    int role = Qt::UserRole + 1;
    for (const QByteArray &name : roleNames) {
        m_roles.insert(role++, name);
    }
}

int JsonListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QHash<int, QByteArray> JsonListModel::roleNames() const
{
    return m_roles;
}

QVariant JsonListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const QByteArray key = m_roles.value(role);
    if (key.isEmpty()) {
        return {};
    }
    return m_items.at(index.row()).value(QString::fromUtf8(key)).toVariant();
}

void JsonListModel::reset(const QJsonArray &items)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(items.size());
    for (const QJsonValue &value : items) {
        m_items.append(value.toObject());
    }
    endResetModel();
    Q_EMIT countChanged();
}

int JsonListModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).value(QStringLiteral("id")).toString() == id) {
            return i;
        }
    }
    return -1;
}

void JsonListModel::upsert(const QJsonObject &item)
{
    const QString id = item.value(QStringLiteral("id")).toString();
    const int row = indexOfId(id);

    if (row < 0) {
        // Newest first, matching the daemon's ledger order.
        beginInsertRows({}, 0, 0);
        m_items.prepend(item);
        endInsertRows();
        Q_EMIT countChanged();
        return;
    }

    m_items[row] = item;
    // Targeted change so only the affected delegate re-evaluates.
    Q_EMIT dataChanged(index(row), index(row), m_roles.keys());
}

void JsonListModel::removeById(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void JsonListModel::removeIf(const std::function<bool(const QJsonObject &)> &predicate)
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (!predicate(m_items.at(i))) {
            continue;
        }
        beginRemoveRows({}, i, i);
        m_items.removeAt(i);
        endRemoveRows();
    }
    Q_EMIT countChanged();
}

void JsonListModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
    Q_EMIT countChanged();
}

QVariantMap JsonListModel::get(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row).toVariantMap();
}

#include "moc_jsonlistmodel.cpp"
