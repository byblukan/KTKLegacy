// KTK for Symbian - one TLS 1.2 connection, BearSSL over a blocking QTcpSocket.
//
// Why not QSslSocket: Qt for Symbian sits on the phone's OpenSSL 0.9.8, which stops at TLS 1.0,
// and the college site accepts TLS 1.2+ with an ECDSA certificate only. BearSSL is small, plain
// C, needs no OS services beyond a socket, and validates the chain against the anchors compiled
// into trustanchors.c.
//
// Blocking by design: it is used from worker threads only (see AppController), and the socket
// is created and used in the thread that calls open().
#ifndef TLSCONNECTION_H
#define TLSCONNECTION_H

#include "ktkerror.h"

#include <QByteArray>
#include <QString>

#include "bearssl.h"

class QTcpSocket;

class TlsConnection
{
public:
    TlsConnection();
    ~TlsConnection();

    /// Connects and completes the handshake. Throws KtkError(Network) on failure.
    void open(const QString &host, int port, int connectTimeoutMs);
    bool isOpen() const { return m_open; }
    QString host() const { return m_host; }
    int port() const { return m_port; }
    /// Idle limit for every later wait on the socket.
    void setTimeout(int ms) { m_timeout = ms; }

    /// Sends everything (the record is flushed). Throws on failure.
    void write(const QByteArray &data);
    /// Up to `max` bytes, at least one; 0 when the peer closed the connection. Throws on failure.
    int read(char *buffer, int max);
    void close();

private:
    static int lowRead(void *ctx, unsigned char *buf, size_t len);
    static int lowWrite(void *ctx, const unsigned char *buf, size_t len);
    /// The error for a failed br_sslio call; closes the connection.
    KtkError failure();

    QTcpSocket *m_socket;
    br_ssl_client_context m_cc;
    br_x509_minimal_context m_xc;
    br_sslio_context m_io;
    unsigned char *m_buffer;
    QString m_host;
    int m_port;
    int m_timeout;
    bool m_open;
    bool m_timedOut;
    bool m_eof;

    TlsConnection(const TlsConnection &);
    TlsConnection &operator=(const TlsConnection &);
};

#endif // TLSCONNECTION_H
