/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings.h"

#include <KConfigGroup>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

namespace
{
constexpr auto GroupName = "General";
constexpr auto KeyDestination = "DestinationRoot";
constexpr auto KeyAutoAccept = "AutoAccept";
constexpr auto KeyGroupBySender = "GroupBySender";
constexpr auto KeyKeepHistory = "KeepHistory";
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("driprc")))
{
    load();
}

QString Settings::defaultDestinationRoot()
{
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    // QStandardPaths can come back empty on a bare account with no XDG dirs.
    if (downloads.isEmpty()) {
        return QDir(QDir::homePath()).filePath(QStringLiteral("Drip"));
    }
    return QDir(downloads).filePath(QStringLiteral("Drip"));
}

QString Settings::normalisePath(const QString &path)
{
    QString result = path.trimmed();
    if (result.isEmpty()) {
        return result;
    }

    if (result.startsWith(QLatin1String("file://"))) {
        result = QUrl(result).toLocalFile();
    }
    if (result == QLatin1String("~")) {
        result = QDir::homePath();
    } else if (result.startsWith(QLatin1String("~/"))) {
        result = QDir::homePath() + result.mid(1);
    }

    result = QDir::cleanPath(result);
    return result;
}

void Settings::load()
{
    KConfigGroup group = m_config->group(QLatin1String(GroupName));
    m_destinationRoot = normalisePath(group.readEntry(KeyDestination, defaultDestinationRoot()));
    if (m_destinationRoot.isEmpty()) {
        m_destinationRoot = defaultDestinationRoot();
    }
    m_autoAccept = group.readEntry(KeyAutoAccept, true);
    m_groupBySender = group.readEntry(KeyGroupBySender, true);
    m_keepHistory = group.readEntry(KeyKeepHistory, true);
}

void Settings::save()
{
    KConfigGroup group = m_config->group(QLatin1String(GroupName));
    group.writeEntry(KeyDestination, m_destinationRoot);
    group.writeEntry(KeyAutoAccept, m_autoAccept);
    group.writeEntry(KeyGroupBySender, m_groupBySender);
    group.writeEntry(KeyKeepHistory, m_keepHistory);
    m_config->sync();
}

void Settings::setDestinationRoot(const QString &path)
{
    const QString normalised = normalisePath(path);
    if (normalised.isEmpty() || normalised == m_destinationRoot) {
        return;
    }
    m_destinationRoot = normalised;
    save();
    Q_EMIT destinationRootChanged(m_destinationRoot);
    Q_EMIT changed();
}

void Settings::setAutoAccept(bool on)
{
    if (on == m_autoAccept) {
        return;
    }
    m_autoAccept = on;
    save();
    Q_EMIT autoAcceptChanged(on);
    Q_EMIT changed();
}

void Settings::setGroupBySender(bool on)
{
    if (on == m_groupBySender) {
        return;
    }
    m_groupBySender = on;
    save();
    Q_EMIT changed();
}

void Settings::setKeepHistory(bool on)
{
    if (on == m_keepHistory) {
        return;
    }
    m_keepHistory = on;
    save();
    Q_EMIT changed();
}

void Settings::applyJson(const QString &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    if (object.contains(QStringLiteral("destinationRoot"))) {
        setDestinationRoot(object.value(QStringLiteral("destinationRoot")).toString());
    }
    if (object.contains(QStringLiteral("autoAccept"))) {
        setAutoAccept(object.value(QStringLiteral("autoAccept")).toBool());
    }
    if (object.contains(QStringLiteral("groupBySender"))) {
        setGroupBySender(object.value(QStringLiteral("groupBySender")).toBool());
    }
    if (object.contains(QStringLiteral("keepHistory"))) {
        setKeepHistory(object.value(QStringLiteral("keepHistory")).toBool());
    }
}

QString Settings::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("destinationRoot"), m_destinationRoot);
    object.insert(QStringLiteral("autoAccept"), m_autoAccept);
    object.insert(QStringLiteral("groupBySender"), m_groupBySender);
    object.insert(QStringLiteral("keepHistory"), m_keepHistory);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

#include "moc_settings.cpp"
