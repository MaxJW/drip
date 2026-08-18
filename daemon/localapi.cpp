/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "localapi.h"

#include <QIODevice>
#include <QLocalSocket>
#include <QUrl>

namespace
{
// tailscaled rejects requests whose Host is not this sentinel; it is the
// anti-DNS-rebinding check. No other header is required, not even for writes.
constexpr auto HostHeader = "local-tailscaled.sock";
constexpr qint64 UploadSliceSize = 256 * 1024;
constexpr int MaxHeaderBytes = 64 * 1024;
}

// ---------------------------------------------------------------- LocalApiReply

LocalApiReply::LocalApiReply(Mode mode, QObject *parent)
    : QObject(parent)
    , m_mode(mode)
{
}

LocalApiReply::~LocalApiReply() = default;

void LocalApiReply::start(const QString &socketPath, const QByteArray &requestHead, QIODevice *uploadBody, qint64 uploadLength)
{
    m_uploadBody = uploadBody;
    m_uploadLength = uploadLength;
    m_uploadComplete = (uploadBody == nullptr);
    m_pendingHead = requestHead;

    if (uploadBody) {
        uploadBody->setParent(this);
    }

    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, &LocalApiReply::onConnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &LocalApiReply::onReadyRead);
    connect(m_socket, &QLocalSocket::bytesWritten, this, &LocalApiReply::onBytesWritten);
    connect(m_socket, &QLocalSocket::disconnected, this, &LocalApiReply::onDisconnected);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &LocalApiReply::onSocketError);

    m_socket->connectToServer(socketPath);
}

void LocalApiReply::abort()
{
    m_finished = true;
    m_state = State::Done;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void LocalApiReply::onConnected()
{
    m_socket->write(m_pendingHead);
    m_pendingHead.clear();
    pumpUpload();
}

void LocalApiReply::onBytesWritten(qint64)
{
    pumpUpload();
}

void LocalApiReply::pumpUpload()
{
    if (m_uploadComplete || !m_socket) {
        return;
    }
    if (!m_uploadBody) {
        // Source vanished mid-transfer.
        fail(QStringLiteral("upload source closed"));
        return;
    }

    // Keep at most one slice in flight so bytesWritten tracks the socket
    // draining, not us filling a buffer. That is what makes progress honest.
    while (m_socket->bytesToWrite() < UploadSliceSize) {
        const QByteArray slice = m_uploadBody->read(UploadSliceSize);
        if (slice.isEmpty()) {
            m_uploadComplete = true;
            if (m_uploadLength >= 0 && m_uploadSent != m_uploadLength) {
                fail(QStringLiteral("upload truncated at %1 of %2 bytes").arg(m_uploadSent).arg(m_uploadLength));
            }
            return;
        }
        m_socket->write(slice);
        m_uploadSent += slice.size();
        Q_EMIT uploadProgress(m_uploadSent, m_uploadLength);
    }
}

void LocalApiReply::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    consume();
}

void LocalApiReply::consume()
{
    if (m_state == State::Headers) {
        if (!parseHeaders()) {
            return;
        }
    }

    for (;;) {
        switch (m_state) {
        case State::Headers:
        case State::Done:
            return;

        case State::ChunkSize: {
            const int eol = m_buffer.indexOf("\r\n");
            if (eol < 0) {
                return;
            }
            // Chunk size is hex, optionally followed by ";extension".
            QByteArray sizeToken = m_buffer.left(eol);
            const int semi = sizeToken.indexOf(';');
            if (semi >= 0) {
                sizeToken = sizeToken.left(semi);
            }
            bool ok = false;
            m_chunkRemaining = sizeToken.trimmed().toLongLong(&ok, 16);
            if (!ok || m_chunkRemaining < 0) {
                fail(QStringLiteral("malformed chunk size: %1").arg(QString::fromLatin1(sizeToken)));
                return;
            }
            m_buffer.remove(0, eol + 2);
            if (m_chunkRemaining == 0) {
                // Terminal chunk; trailers ignored.
                complete();
                return;
            }
            m_state = State::ChunkData;
            break;
        }

        case State::ChunkData: {
            if (m_buffer.isEmpty()) {
                return;
            }
            const qint64 take = qMin<qint64>(m_chunkRemaining, m_buffer.size());
            deliver(m_buffer.left(take));
            m_buffer.remove(0, take);
            m_chunkRemaining -= take;
            if (m_chunkRemaining == 0) {
                m_state = State::ChunkCrlf;
            }
            break;
        }

        case State::ChunkCrlf: {
            if (m_buffer.size() < 2) {
                return;
            }
            m_buffer.remove(0, 2);
            m_state = State::ChunkSize;
            break;
        }

        case State::Length: {
            if (m_buffer.isEmpty()) {
                return;
            }
            const qint64 take = qMin<qint64>(m_bodyRemaining, m_buffer.size());
            deliver(m_buffer.left(take));
            m_buffer.remove(0, take);
            m_bodyRemaining -= take;
            if (m_bodyRemaining <= 0) {
                complete();
                return;
            }
            break;
        }

        case State::UntilClose: {
            if (m_buffer.isEmpty()) {
                return;
            }
            deliver(m_buffer);
            m_buffer.clear();
            return;
        }
        }
    }
}

bool LocalApiReply::parseHeaders()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (m_buffer.size() > MaxHeaderBytes) {
            fail(QStringLiteral("response headers exceed %1 bytes").arg(MaxHeaderBytes));
        }
        return false;
    }

    const QByteArray head = m_buffer.left(end);
    m_buffer.remove(0, end + 4);

    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) {
        fail(QStringLiteral("empty response"));
        return false;
    }

    // "HTTP/1.1 200 OK"
    const QList<QByteArray> statusParts = lines.first().trimmed().split(' ');
    if (statusParts.size() < 2) {
        fail(QStringLiteral("malformed status line"));
        return false;
    }
    m_statusCode = statusParts.at(1).toInt();

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray &raw = lines.at(i);
        const int colon = raw.indexOf(':');
        if (colon < 0) {
            continue;
        }
        m_headers.insert(raw.left(colon).trimmed().toLower(), raw.mid(colon + 1).trimmed());
    }

    Q_EMIT headers(m_statusCode);

    if (m_headers.value("transfer-encoding").toLower().contains("chunked")) {
        m_state = State::ChunkSize;
    } else if (m_headers.contains("content-length")) {
        m_bodyRemaining = m_headers.value("content-length").toLongLong();
        if (m_bodyRemaining <= 0) {
            complete();
            return false;
        }
        m_state = State::Length;
    } else {
        m_state = State::UntilClose;
    }
    return true;
}

void LocalApiReply::deliver(const QByteArray &data)
{
    if (m_mode == Mode::Buffered) {
        m_body.append(data);
        return;
    }

    Q_EMIT chunk(data);

    // Split into newline-delimited records for NDJSON consumers.
    m_lineBuffer.append(data);
    int nl;
    while ((nl = m_lineBuffer.indexOf('\n')) >= 0) {
        QByteArray record = m_lineBuffer.left(nl);
        m_lineBuffer.remove(0, nl + 1);
        if (record.endsWith('\r')) {
            record.chop(1);
        }
        if (!record.isEmpty()) {
            Q_EMIT line(record);
        }
    }
}

void LocalApiReply::complete()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_state = State::Done;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
    Q_EMIT finished();
}

void LocalApiReply::fail(const QString &message)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_state = State::Done;
    m_error = message;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
    Q_EMIT errored(message);
}

void LocalApiReply::onDisconnected()
{
    if (m_finished) {
        return;
    }
    // A close is a legitimate end-of-body when the server never framed one.
    if (m_state == State::UntilClose) {
        complete();
    } else if (m_state == State::Headers && m_buffer.isEmpty()) {
        fail(QStringLiteral("connection closed before response"));
    } else {
        fail(QStringLiteral("connection closed mid-response"));
    }
}

void LocalApiReply::onSocketError()
{
    if (m_finished) {
        return;
    }
    if (m_socket->error() == QLocalSocket::PeerClosedError) {
        onDisconnected();
        return;
    }
    fail(m_socket->errorString());
}

// -------------------------------------------------------------------- LocalApi

LocalApi::LocalApi(QObject *parent)
    : QObject(parent)
    , m_socketPath(defaultSocketPath())
{
}

QString LocalApi::defaultSocketPath()
{
    return QStringLiteral("/var/run/tailscale/tailscaled.sock");
}

void LocalApi::setSocketPath(const QString &path)
{
    m_socketPath = path;
}

QString LocalApi::encodeSegment(const QString &segment)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(segment));
}

LocalApiReply *LocalApi::request(const QByteArray &method, const QString &path, LocalApiReply::Mode mode, QIODevice *body, qint64 length)
{
    auto *reply = new LocalApiReply(mode, this);

    QByteArray head;
    head += method + ' ' + path.toUtf8() + " HTTP/1.1\r\n";
    head += QByteArray("Host: ") + HostHeader + "\r\n";
    if (body) {
        head += "Content-Type: application/octet-stream\r\n";
        if (length >= 0) {
            head += "Content-Length: " + QByteArray::number(length) + "\r\n";
        }
    }
    // One request per connection: no pipelining, no keep-alive bookkeeping.
    head += "Connection: close\r\n\r\n";

    reply->start(m_socketPath, head, body, length);
    return reply;
}

LocalApiReply *LocalApi::get(const QString &path)
{
    return request("GET", path, LocalApiReply::Mode::Buffered);
}

LocalApiReply *LocalApi::stream(const QString &path)
{
    return request("GET", path, LocalApiReply::Mode::Streaming);
}

LocalApiReply *LocalApi::remove(const QString &path)
{
    return request("DELETE", path, LocalApiReply::Mode::Buffered);
}

LocalApiReply *LocalApi::put(const QString &path, QIODevice *body, qint64 length)
{
    return request("PUT", path, LocalApiReply::Mode::Buffered, body, length);
}

#include "moc_localapi.cpp"
