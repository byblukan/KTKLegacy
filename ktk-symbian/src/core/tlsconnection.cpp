// KTK for Symbian - one TLS 1.2 connection, BearSSL over a blocking QTcpSocket.
#include "tlsconnection.h"
#include "entropy.h"
#include "trustanchors.h"

#include <QDateTime>
#include <QTcpSocket>

#include <stdlib.h>
#include <string.h>

namespace
{
    QString socketErrorText(QTcpSocket *socket)
    {
        switch (socket->error()) {
        case QAbstractSocket::HostNotFoundError:
            return QString::fromUtf8("сайт не найден - есть ли интернет?");
        case QAbstractSocket::SocketTimeoutError:
            return QString::fromUtf8("сайт не ответил вовремя");
        case QAbstractSocket::ConnectionRefusedError:
            return QString::fromUtf8("соединение отклонено");
        case QAbstractSocket::RemoteHostClosedError:
            return QString::fromUtf8("сайт закрыл соединение");
        case QAbstractSocket::NetworkError:
            return QString::fromUtf8("ошибка сети");
        default:
            return socket->errorString();
        }
    }

    QString tlsErrorText(int err)
    {
        switch (err) {
        case BR_ERR_X509_EXPIRED:
        case BR_ERR_X509_TIME_UNKNOWN:
            return QString::fromUtf8("сертификат сайта не действует на дату телефона - проверьте дату и время");
        case BR_ERR_X509_NOT_TRUSTED:
        case BR_ERR_X509_NOT_CA:
            return QString::fromUtf8("сайт предъявил сертификат, которому приложение не доверяет");
        case BR_ERR_X509_BAD_SERVER_NAME:
            return QString::fromUtf8("сертификат выдан другому сайту");
        case BR_ERR_BAD_VERSION:
            return QString::fromUtf8("сайт не согласился на TLS 1.2");
        default:
            if (err >= BR_ERR_RECV_FATAL_ALERT && err < BR_ERR_SEND_FATAL_ALERT)
                return QString::fromUtf8("сайт прервал TLS (alert %1)").arg(err - BR_ERR_RECV_FATAL_ALERT);
            if (err >= BR_ERR_SEND_FATAL_ALERT)
                return QString::fromUtf8("ошибка TLS (alert %1)").arg(err - BR_ERR_SEND_FATAL_ALERT);
            return QString::fromUtf8("ошибка TLS %1").arg(err);
        }
    }
}

TlsConnection::TlsConnection()
    : m_socket(0), m_buffer(0), m_port(0), m_timeout(60000), m_open(false), m_timedOut(false), m_eof(false)
{
    memset(&m_cc, 0, sizeof m_cc);
    memset(&m_xc, 0, sizeof m_xc);
    memset(&m_io, 0, sizeof m_io);
}

TlsConnection::~TlsConnection()
{
    close();
    free(m_buffer);
}

void TlsConnection::close()
{
    m_open = false;
    if (m_socket) {
        m_socket->abort();
        delete m_socket;
        m_socket = 0;
    }
}

void TlsConnection::open(const QString &host, int port, int connectTimeoutMs)
{
    close();
    m_host = host;
    m_port = port;
    m_timedOut = false;
    m_eof = false;

    if (!m_buffer) {
        m_buffer = static_cast<unsigned char *>(malloc(BR_SSL_BUFSIZE_BIDI));
        if (!m_buffer) throw KtkError(KtkError::Network, QString::fromUtf8("мало памяти"));
    }

    m_socket = new QTcpSocket;
    m_socket->connectToHost(host, quint16(port));
    if (!m_socket->waitForConnected(connectTimeoutMs)) {
        const QString text = m_socket->error() == QAbstractSocket::UnknownSocketError
            ? QString::fromUtf8("сайт не ответил вовремя") : socketErrorText(m_socket);
        close();
        throw KtkError(KtkError::Network, text);
    }

    ktk_trust_anchors_init();
    br_ssl_client_init_full(&m_cc, &m_xc, KTK_TRUST_ANCHORS, KTK_TRUST_ANCHORS_NUM);
    br_ssl_engine_set_versions(&m_cc.eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_buffer(&m_cc.eng, m_buffer, BR_SSL_BUFSIZE_BIDI, 1);
    const QByteArray seed = Entropy::bytes(48);
    br_ssl_engine_inject_entropy(&m_cc.eng, seed.constData(), size_t(seed.size()));

    // BearSSL has no clock of its own here: certificates are checked against the phone's
    // date. Days are counted from 0000-01-01 (proleptic Gregorian); 1970-01-01 is day 719528.
    const qint64 now = QDateTime::currentDateTime().toTime_t();
    br_x509_minimal_set_time(&m_xc, uint32_t(now / 86400 + 719528), uint32_t(now % 86400));

    const QByteArray serverName = host.toLatin1();
    if (!br_ssl_client_reset(&m_cc, serverName.constData(), 0)) {
        const int err = br_ssl_engine_last_error(&m_cc.eng);
        close();
        throw KtkError(KtkError::Network, tlsErrorText(err));
    }
    br_sslio_init(&m_io, &m_cc.eng, lowRead, this, lowWrite, this);
    m_open = true;

    // Runs the handshake now, so certificate problems surface here rather than in a request.
    if (br_sslio_flush(&m_io) < 0) throw failure();
}

KtkError TlsConnection::failure()
{
    const int err = br_ssl_engine_last_error(&m_cc.eng);
    QString text;
    if (m_timedOut) text = QString::fromUtf8("сайт не ответил вовремя");
    else if (err == BR_ERR_IO || err == BR_ERR_OK) text = m_socket ? socketErrorText(m_socket) : QString();
    else text = tlsErrorText(err);
    if (text.isEmpty()) text = QString::fromUtf8("соединение прервано");
    close();
    return KtkError(KtkError::Network, text);
}

void TlsConnection::write(const QByteArray &data)
{
    if (!m_open) throw KtkError(KtkError::Network, QString::fromUtf8("соединение закрыто"));
    if (br_sslio_write_all(&m_io, data.constData(), size_t(data.size())) < 0) throw failure();
    if (br_sslio_flush(&m_io) < 0) throw failure();
}

int TlsConnection::read(char *buffer, int max)
{
    if (!m_open) return 0;
    const int n = br_sslio_read(&m_io, buffer, size_t(max));
    if (n > 0) return n;
    const int err = br_ssl_engine_last_error(&m_cc.eng);
    // A close_notify (no error), or the peer simply dropping the TCP connection, which many
    // servers do at the end of a keep-alive period: both end the stream.
    if (!m_timedOut && (err == BR_ERR_OK || (err == BR_ERR_IO && m_eof))) {
        close();
        return 0;
    }
    throw failure();
}

int TlsConnection::lowRead(void *ctx, unsigned char *buf, size_t len)
{
    TlsConnection *self = static_cast<TlsConnection *>(ctx);
    QTcpSocket *socket = self->m_socket;
    if (!socket) return -1;
    for (;;) {
        if (socket->bytesAvailable() > 0) {
            const qint64 n = socket->read(reinterpret_cast<char *>(buf), qint64(len));
            if (n > 0) return int(n);
            if (n < 0) return -1;
        }
        if (socket->state() != QAbstractSocket::ConnectedState) {
            self->m_eof = true;
            return -1;
        }
        if (!socket->waitForReadyRead(self->m_timeout)) {
            if (socket->bytesAvailable() > 0) continue;   // data arrived together with the close
            if (socket->state() != QAbstractSocket::ConnectedState
                || socket->error() == QAbstractSocket::RemoteHostClosedError) {
                self->m_eof = true;
            } else {
                self->m_timedOut = true;
            }
            return -1;
        }
    }
}

int TlsConnection::lowWrite(void *ctx, const unsigned char *buf, size_t len)
{
    TlsConnection *self = static_cast<TlsConnection *>(ctx);
    QTcpSocket *socket = self->m_socket;
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return -1;
    const qint64 n = socket->write(reinterpret_cast<const char *>(buf), qint64(len));
    if (n <= 0) return -1;
    while (socket->bytesToWrite() > 0) {
        if (!socket->waitForBytesWritten(self->m_timeout)) {
            if (socket->error() == QAbstractSocket::SocketTimeoutError) self->m_timedOut = true;
            return -1;
        }
    }
    return int(n);
}
