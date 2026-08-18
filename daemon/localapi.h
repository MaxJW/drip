/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal HTTP/1.1 client for the tailscaled LocalAPI.
 *
 * QNetworkAccessManager cannot speak to a unix socket, so we drive QLocalSocket
 * directly. tailscaled answers with Transfer-Encoding: chunked even for small
 * buffered responses, so chunked decoding is mandatory rather than optional.
 *
 * Access is unprivileged: tailscaled checks the peer credentials of the socket
 * against the configured operator user.
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class QIODevice;
class QLocalSocket;

class LocalApiReply : public QObject
{
    Q_OBJECT

public:
    ~LocalApiReply() override;

    /** HTTP status code, valid once headers() has fired. */
    int statusCode() const
    {
        return m_statusCode;
    }

    /** Accumulated body. Empty in streaming mode, where chunk()/line() carry it. */
    QByteArray body() const
    {
        return m_body;
    }

    QString errorString() const
    {
        return m_error;
    }

    bool isError() const
    {
        return !m_error.isEmpty();
    }

    QByteArray header(const QByteArray &name) const
    {
        return m_headers.value(name.toLower());
    }

    /** Tear down the connection. Emits neither finished() nor errored(). */
    void abort();

Q_SIGNALS:
    void headers(int statusCode);
    /** Decoded body bytes. Streaming mode only. */
    void chunk(const QByteArray &data);
    /** One newline-delimited record. Streaming mode only. */
    void line(const QByteArray &data);
    void uploadProgress(qint64 sent, qint64 total);
    void finished();
    void errored(const QString &message);

private:
    friend class LocalApi;

    enum class Mode {
        Buffered, ///< accumulate into m_body, emit finished() at end
        Streaming, ///< emit chunk()/line() as bytes arrive
    };

    enum class State {
        Headers,
        ChunkSize,
        ChunkData,
        ChunkCrlf,
        Length,
        UntilClose,
        Done,
    };

    explicit LocalApiReply(Mode mode, QObject *parent = nullptr);

    void start(const QString &socketPath, const QByteArray &requestHead, QIODevice *uploadBody, qint64 uploadLength);

    void onConnected();
    void onReadyRead();
    void onBytesWritten(qint64 written);
    void onDisconnected();
    void onSocketError();

    bool parseHeaders();
    void consume();
    void deliver(const QByteArray &data);
    void complete();
    void fail(const QString &message);
    void pumpUpload();

    QLocalSocket *m_socket = nullptr;
    Mode m_mode;
    State m_state = State::Headers;

    QByteArray m_buffer; ///< unparsed bytes from the socket
    QByteArray m_body; ///< buffered mode accumulator
    QByteArray m_lineBuffer; ///< streaming mode partial line
    QHash<QByteArray, QByteArray> m_headers;

    int m_statusCode = 0;
    QString m_error;
    qint64 m_chunkRemaining = 0;
    qint64 m_bodyRemaining = -1;
    bool m_finished = false;

    // Upload state
    QPointer<QIODevice> m_uploadBody;
    qint64 m_uploadLength = -1;
    qint64 m_uploadSent = 0;
    bool m_uploadComplete = false;
    QByteArray m_pendingHead;
};

class LocalApi : public QObject
{
    Q_OBJECT

public:
    explicit LocalApi(QObject *parent = nullptr);

    /** Defaults to /var/run/tailscale/tailscaled.sock, overridable for tests. */
    void setSocketPath(const QString &path);
    QString socketPath() const
    {
        return m_socketPath;
    }

    /** Buffered GET. */
    LocalApiReply *get(const QString &path);

    /** Streaming GET: emits line() per newline-delimited record until closed. */
    LocalApiReply *stream(const QString &path);

    /** Buffered DELETE. */
    LocalApiReply *remove(const QString &path);

    /**
     * Streaming PUT. @p body is read incrementally and reparented to the reply,
     * so a file handle stays open exactly as long as the transfer does.
     */
    LocalApiReply *put(const QString &path, QIODevice *body, qint64 length);

    static QString defaultSocketPath();
    /** Percent-encode one path segment (filenames may contain spaces, #, ?). */
    static QString encodeSegment(const QString &segment);

private:
    LocalApiReply *request(const QByteArray &method,
                           const QString &path,
                           LocalApiReply::Mode mode,
                           QIODevice *body = nullptr,
                           qint64 length = -1);

    QString m_socketPath;
};
