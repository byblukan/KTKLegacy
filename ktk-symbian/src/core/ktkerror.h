// KTK for Symbian - every failure of the app, with the text shown to the user.
#ifndef KTKERROR_H
#define KTKERROR_H

#include <QString>

class KtkError
{
public:
    enum Code {
        None,
        Network,        // no connection, TLS failure, timeout
        HttpStatus,     // unexpected HTTP status
        TooLarge,       // response above the size limit
        BadResponse,    // not JSON / not the expected shape (often: the site was updated)
        LoginRequired,  // the site rejected the saved session
        Credentials,    // the site rejected login and password; detail = site message
        Server,         // other in-band error of a server function
        File            // local file system failure
    };

    KtkError() : code(None), status(0) {}
    KtkError(Code c, const QString &d = QString(), int s = 0) : code(c), status(s), detail(d) {}

    static KtkError fromStatus(int status);

    bool isError() const { return code != None; }
    bool isLoginRequired() const { return code == LoginRequired; }
    /// The user-facing text; empty for None.
    QString text() const;

    Code code;
    int status;
    QString detail;
};

#endif // KTKERROR_H
