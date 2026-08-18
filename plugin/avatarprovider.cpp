/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "avatarprovider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQuickTextureFactory>
#include <QStandardPaths>
#include <QUrl>

namespace
{
constexpr int MaxAvatarBytes = 2 * 1024 * 1024;

class AvatarResponse : public QQuickImageResponse
{
public:
    AvatarResponse(QNetworkAccessManager *network, const QString &url, const QSize &requestedSize)
        : m_requestedSize(requestedSize)
    {
        const QString cached = AvatarProvider::cachePathFor(url);
        if (QFile::exists(cached)) {
            QImage image;
            if (image.load(cached)) {
                finishWith(image);
                return;
            }
        }

        if (url.isEmpty() || !url.startsWith(QLatin1String("https://"))) {
            fail(QStringLiteral("no avatar"));
            return;
        }

        QNetworkRequest request{QUrl(url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(8000);

        m_reply = network->get(request);
        QObject::connect(m_reply, &QNetworkReply::finished, m_reply, [this, cached] {
            const QByteArray payload = m_reply->readAll();
            const bool ok = m_reply->error() == QNetworkReply::NoError && !payload.isEmpty()
                && payload.size() <= MaxAvatarBytes;

            m_reply->deleteLater();
            m_reply = nullptr;

            QImage image;
            if (!ok || !image.loadFromData(payload)) {
                fail(QStringLiteral("avatar fetch failed"));
                return;
            }

            QDir().mkpath(AvatarProvider::cacheDirectory());
            image.save(cached, "PNG");
            finishWith(image);
        });
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_error;
    }

    void cancel() override
    {
        if (m_reply) {
            m_reply->abort();
        }
    }

private:
    void finishWith(const QImage &image)
    {
        m_image = m_requestedSize.isValid()
            ? image.scaled(m_requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : image;
        Q_EMIT finished();
    }

    void fail(const QString &error)
    {
        m_error = error;
        Q_EMIT finished();
    }

    QImage m_image;
    QString m_error;
    QSize m_requestedSize;
    QNetworkReply *m_reply = nullptr;
};
}

AvatarProvider::AvatarProvider()
    : m_network(new QNetworkAccessManager)
{
}

AvatarProvider::~AvatarProvider()
{
    delete m_network;
}

QString AvatarProvider::cacheDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("drip/avatars"));
}

QString AvatarProvider::cachePathFor(const QString &url)
{
    const QByteArray digest = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
    return QDir(cacheDirectory()).filePath(QString::fromLatin1(digest) + QStringLiteral(".png"));
}

QQuickImageResponse *AvatarProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // QML percent-encodes the URL into the image source path.
    const QString url = QUrl::fromPercentEncoding(id.toUtf8());
    return new AvatarResponse(m_network, url, requestedSize);
}
