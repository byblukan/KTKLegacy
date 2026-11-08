// KTK for Symbian - HTTPS only, HTTP/1.1, no automatic redirects, size limits.
//
// An Http object keeps at most one keep-alive connection, reused while requests go to the same
// host. Blocking: worker threads only, and one Http per thread (the socket belongs to the thread
// that opened it).
#ifndef HTTP_H
#define HTTP_H

#include "ktkerror.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

class TlsConnection;

struct HttpRequest
{
    HttpRequest() : method("GET"), limit(4 * 1024 * 1024), timeoutMs(60000) {}

    QByteArray method;
    QString url;                                   ///< https://host/path?query, already encoded
    QList<QPair<QByteArray, QByteArray> > headers;
    QByteArray body;                               ///< POST only
    qint64 limit;                                  ///< max body bytes
    int timeoutMs;                                 ///< idle limit while waiting for the site
    QString outputPath;                            ///< stream a 200 body into this file

    /// Replaces a header of the same name (case-insensitive), or adds it.
    void setHeader(const QByteArray &name, const QByteArray &value);
    void removeHeader(const QByteArray &name);
};

struct HttpResponse
{
    HttpResponse() : status(0) {}
    int status;
    QByteArray body;                ///< empty when written to the output file
    QList<QByteArray> setCookies;
    QString location;
};

class Http
{
public:
    Http();
    ~Http();

    /// Throws KtkError (Network, TooLarge, File).
    HttpResponse perform(const HttpRequest &request);
    void close();

    /// Percent-encodes everything except RFC 3986 unreserved characters (UTF-8).
    static QString escape(const QString &value);

private:
    HttpResponse exchange(const HttpRequest &request, const QByteArray &head, const QString &host);
    QByteArray readLine(int limit);
    void readBody(HttpResponse &response, const HttpRequest &request, qint64 contentLength,
                  bool chunked, bool untilClose);
    int fill();

    TlsConnection *m_conn;
    QByteArray m_in;   // bytes received but not yet consumed
    int m_pos;
    bool m_started;    // the current exchange has received a status line

    Http(const Http &);
    Http &operator=(const Http &);
};

#endif // HTTP_H
