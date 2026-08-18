/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * image://dripavatar/<percent-encoded profile pic URL>
 *
 * Tailscale hands us each account's avatar URL for free, but a tray popup must
 * not stall on the network. Fetches are asynchronous and cached on disk, so the
 * second open is instant and an offline session still shows faces.
 */

#pragma once

#include <QHash>
#include <QQuickAsyncImageProvider>
#include <QString>

class QNetworkAccessManager;

class AvatarProvider : public QQuickAsyncImageProvider
{
public:
    AvatarProvider();
    ~AvatarProvider() override;

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    static QString cacheDirectory();
    /** Stable filename for a remote URL. */
    static QString cachePathFor(const QString &url);

private:
    QNetworkAccessManager *m_network;
};
