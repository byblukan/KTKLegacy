// KTK for Symbian - HTTPS only, HTTP/1.1, no automatic redirects, size limits.
#include "http.h"
#include "tlsconnection.h"

#include <QFile>

namespace
{
    const int ConnectTimeoutMs = 20000;
    const int MaxLine = 16 * 1024;
    const int MaxHeaders = 128;

    /// Where the body goes: memory, or the output file of a download.
    class Sink
    {
    public:
        Sink(QByteArray *memory, QFile *file, qint64 limit) : m_memory(memory), m_file(file), m_limit(limit), m_size(0) {}

        void add(const char *data, int size)
        {
            m_size += size;
            if (m_size > m_limit) {
                throw KtkError(KtkError::TooLarge, m_file ? QString::fromUtf8("Файл слишком большой.")
                                                          : QString::fromUtf8("Ответ слишком большой."));
            }
            if (m_file) {
                if (m_file->write(data, size) != size)
                    throw KtkError(KtkError::File, QString::fromUtf8("Не удалось записать файл."));
            } else {
                m_memory->append(data, size);
            }
        }

    private:
        QByteArray *m_memory;
        QFile *m_file;
        qint64 m_limit;
        qint64 m_size;
    };

    bool sameName(const QByteArray &a, const char *b)
    {
        return a.toLower() == QByteArray(b);
    }
}

void HttpRequest::setHeader(const QByteArray &name, const QByteArray &value)
{
    removeHeader(name);
    headers.append(qMakePair(name, value));
}

void HttpRequest::removeHeader(const QByteArray &name)
{
    const QByteArray lower = name.toLower();
    for (int i = headers.size() - 1; i >= 0; --i)
        if (headers.at(i).first.toLower() == lower) headers.removeAt(i);
}

Http::Http() : m_conn(0), m_pos(0), m_started(false) {}

Http::~Http()
{
    delete m_conn;
}

void Http::close()
{
    if (m_conn) m_conn->close();
    m_in.clear();
    m_pos = 0;
}

QString Http::escape(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    static const char digits[] = "0123456789ABCDEF";
    QString out;
    for (int i = 0; i < utf8.size(); ++i) {
        const uchar c = uchar(utf8.at(i));
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '.' || c == '_' || c == '~') {
            out += QLatin1Char(char(c));
        } else {
            out += QLatin1Char('%');
            out += QLatin1Char(digits[c >> 4]);
            out += QLatin1Char(digits[c & 15]);
        }
    }
    return out;
}

HttpResponse Http::perform(const HttpRequest &request)
{
    const QString url = request.url;
    if (!url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        throw KtkError(KtkError::Network, QString::fromUtf8("Разрешён только HTTPS."));
    const QString rest = url.mid(8);
    int slash = rest.indexOf(QLatin1Char('/'));
    const int question = rest.indexOf(QLatin1Char('?'));
    if (slash < 0 || (question >= 0 && question < slash)) slash = question;
    const QString authority = slash < 0 ? rest : rest.left(slash);
    QString target = slash < 0 ? QString::fromLatin1("/") : rest.mid(slash);
    if (target.startsWith(QLatin1Char('?'))) target.prepend(QLatin1Char('/'));

    QString host = authority;
    int port = 443;
    const int colon = authority.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        port = authority.mid(colon + 1).toInt(&ok);
        if (!ok || port <= 0 || port > 65535) throw KtkError(KtkError::Network, QString::fromUtf8("Неверный адрес."));
        host = authority.left(colon);
    }
    host = host.toLower();
    if (host.isEmpty() || target.contains(QLatin1Char(' ')) || target.contains(QLatin1Char('\n')))
        throw KtkError(KtkError::Network, QString::fromUtf8("Неверный адрес."));

    QByteArray head;
    head += request.method + ' ' + target.toLatin1() + " HTTP/1.1\r\n";
    head += "Host: " + authority.toLatin1() + "\r\n";
    for (int i = 0; i < request.headers.size(); ++i)
        head += request.headers.at(i).first + ": " + request.headers.at(i).second + "\r\n";
    if (request.method == "POST") head += "Content-Length: " + QByteArray::number(request.body.size()) + "\r\n";
    head += "Accept-Encoding: identity\r\n\r\n";

    // A kept-alive connection may have been closed by the site meanwhile; that shows up as a
    // failure before any answer, and then a fresh connection is tried once.
    const bool reused = m_conn && m_conn->isOpen() && m_conn->host() == host && m_conn->port() == port;
    if (reused) {
        try {
            return exchange(request, head, host);
        } catch (const KtkError &e) {
            if (e.code != KtkError::Network || m_started) throw;
        }
    }
    if (!m_conn) m_conn = new TlsConnection;
    m_in.clear();
    m_pos = 0;
    m_conn->open(host, port, ConnectTimeoutMs);
    return exchange(request, head, host);
}

int Http::fill()
{
    if (m_pos > 0 && m_pos >= m_in.size()) {
        m_in.clear();
        m_pos = 0;
    } else if (m_pos > 64 * 1024) {
        m_in.remove(0, m_pos);
        m_pos = 0;
    }
    char buffer[8192];
    const int n = m_conn->read(buffer, int(sizeof buffer));
    if (n > 0) m_in.append(buffer, n);
    return n;
}

QByteArray Http::readLine(int limit)
{
    for (;;) {
        const int newline = m_in.indexOf('\n', m_pos);
        if (newline >= 0) {
            QByteArray line = m_in.mid(m_pos, newline - m_pos);
            m_pos = newline + 1;
            if (line.endsWith('\r')) line.chop(1);
            return line;
        }
        if (m_in.size() - m_pos > limit) throw KtkError(KtkError::BadResponse);
        if (fill() == 0) throw KtkError(KtkError::Network, QString::fromUtf8("сайт закрыл соединение"));
    }
}

HttpResponse Http::exchange(const HttpRequest &request, const QByteArray &head, const QString &host)
{
    Q_UNUSED(host);
    m_conn->setTimeout(request.timeoutMs);
    m_in.clear();
    m_pos = 0;
    m_started = false;
    QByteArray out = head;
    if (request.method == "POST") out += request.body;
    m_conn->write(out);

    HttpResponse response;
    QByteArray statusLine;
    bool http10 = false;
    qint64 contentLength = -1;
    bool chunked = false;
    bool closeAfter = false;
    for (;;) {
        statusLine = readLine(MaxLine);
        m_started = true;
        // HTTP/1.1 200 OK
        if (!statusLine.startsWith("HTTP/1.")) throw KtkError(KtkError::BadResponse);
        http10 = statusLine.startsWith("HTTP/1.0");
        const int space = statusLine.indexOf(' ');
        bool ok = false;
        response.status = space > 0 ? statusLine.mid(space + 1, 3).toInt(&ok) : 0;
        if (!ok) throw KtkError(KtkError::BadResponse);

        response.setCookies.clear();
        response.location.clear();
        contentLength = -1;
        chunked = false;
        closeAfter = http10;
        for (int count = 0;; ++count) {
            if (count > MaxHeaders) throw KtkError(KtkError::BadResponse);
            const QByteArray line = readLine(MaxLine);
            if (line.isEmpty()) break;
            const int colon = line.indexOf(':');
            if (colon <= 0) continue;
            const QByteArray name = line.left(colon).trimmed();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (sameName(name, "set-cookie")) {
                response.setCookies.append(value);
            } else if (sameName(name, "location")) {
                if (response.location.isEmpty()) response.location = QString::fromUtf8(value);
            } else if (sameName(name, "content-length")) {
                bool okLength = false;
                contentLength = value.toLongLong(&okLength);
                if (!okLength || contentLength < 0) throw KtkError(KtkError::BadResponse);
            } else if (sameName(name, "transfer-encoding")) {
                chunked = value.toLower().contains("chunked");
            } else if (sameName(name, "connection")) {
                const QByteArray v = value.toLower();
                if (v.contains("close")) closeAfter = true;
                else if (v.contains("keep-alive")) closeAfter = false;
            }
        }
        if (response.status >= 200 || response.status == 101) break;   // skip 1xx interim answers
    }

    const bool noBody = request.method == "HEAD" || response.status == 204 || response.status == 304
        || (response.status >= 100 && response.status < 200);
    if (!noBody) {
        const bool untilClose = !chunked && contentLength < 0;
        readBody(response, request, chunked ? -1 : contentLength, chunked, untilClose);
        if (untilClose) closeAfter = true;
    }
    if (closeAfter || m_pos < m_in.size()) close();
    else {
        m_in.clear();
        m_pos = 0;
    }
    return response;
}

void Http::readBody(HttpResponse &response, const HttpRequest &request, qint64 contentLength,
                    bool chunked, bool untilClose)
{
    QFile *file = 0;
    const bool toFile = !request.outputPath.isEmpty() && response.status == 200;
    if (toFile) {
        file = new QFile(request.outputPath);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            delete file;
            throw KtkError(KtkError::File, QString::fromUtf8("Не удалось создать файл."));
        }
    }
    Sink sink(&response.body, file, request.limit);
    try {
        if (chunked) {
            for (;;) {
                QByteArray sizeLine = readLine(MaxLine);
                const int semicolon = sizeLine.indexOf(';');
                if (semicolon >= 0) sizeLine.truncate(semicolon);
                bool ok = false;
                const qint64 size = sizeLine.trimmed().toLongLong(&ok, 16);
                if (!ok || size < 0) throw KtkError(KtkError::BadResponse);
                if (size == 0) {
                    while (!readLine(MaxLine).isEmpty()) {}   // trailers
                    break;
                }
                qint64 left = size;
                while (left > 0) {
                    if (m_pos >= m_in.size() && fill() == 0)
                        throw KtkError(KtkError::Network, QString::fromUtf8("ответ оборвался"));
                    const int take = int(qMin<qint64>(left, m_in.size() - m_pos));
                    sink.add(m_in.constData() + m_pos, take);
                    m_pos += take;
                    left -= take;
                }
                if (!readLine(MaxLine).isEmpty()) throw KtkError(KtkError::BadResponse);
            }
        } else if (untilClose) {
            for (;;) {
                if (m_pos < m_in.size()) {
                    sink.add(m_in.constData() + m_pos, m_in.size() - m_pos);
                    m_pos = m_in.size();
                }
                if (fill() == 0) break;
            }
        } else {
            qint64 left = contentLength;
            while (left > 0) {
                if (m_pos >= m_in.size() && fill() == 0)
                    throw KtkError(KtkError::Network, QString::fromUtf8("ответ оборвался"));
                const int take = int(qMin<qint64>(left, m_in.size() - m_pos));
                sink.add(m_in.constData() + m_pos, take);
                m_pos += take;
                left -= take;
            }
        }
        if (file) {
            if (!file->flush()) throw KtkError(KtkError::File, QString::fromUtf8("Не удалось записать файл."));
            file->close();
            delete file;
            file = 0;
        }
    } catch (...) {
        if (file) {
            file->close();
            file->remove();
            delete file;
        }
        close();
        throw;
    }
}
