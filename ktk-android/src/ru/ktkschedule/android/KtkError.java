package ru.ktkschedule.android;

/** Every failure of this app, with the text shown to the user. */
public final class KtkError extends Exception {
    public enum Code {
        NETWORK,        // no connection, TLS failure, timeout
        HTTP_STATUS,    // unexpected HTTP status
        TOO_LARGE,      // response above the size limit
        BAD_RESPONSE,   // not JSON / not the expected shape (often: the site was updated)
        LOGIN_REQUIRED, // the site rejected the saved session
        CREDENTIALS,    // the site rejected login and password; detail = site message
        SERVER,         // other in-band error of a server function
        FILE,           // local file system failure
    }

    public final Code code;
    public final int status;
    public final String detail;

    public KtkError(Code code, String detail) {
        this(code, detail, 0);
    }

    public KtkError(Code code, String detail, int status) {
        super(code + (detail != null ? ": " + detail : ""));
        this.code = code;
        this.detail = detail;
        this.status = status;
    }

    static KtkError status(int status) {
        return new KtkError(Code.HTTP_STATUS, "Сайт ответил ошибкой " + status + ".", status);
    }

    public static boolean isLoginRequired(Throwable error) {
        return error instanceof KtkError && ((KtkError) error).code == Code.LOGIN_REQUIRED;
    }

    /** User-facing text for any error of this app. */
    public static String text(Throwable error) {
        if (error == null) {
            return null;
        }
        if (!(error instanceof KtkError)) {
            String message = error.getMessage();
            return message != null ? message : error.toString();
        }
        KtkError e = (KtkError) error;
        switch (e.code) {
            case NETWORK:
                return e.detail != null ? "Нет связи с сайтом колледжа.\n(" + e.detail + ")" : "Нет связи с сайтом колледжа.";
            case LOGIN_REQUIRED:
                return "Сессия закончилась. Войдите снова.";
            case CREDENTIALS:
                return e.detail != null ? e.detail : "Неверный логин или пароль.";
            case BAD_RESPONSE:
                return "Сайт ответил непонятно. Возможно, он обновился.";
            default:
                return e.detail != null ? e.detail : "Ошибка сайта.";
        }
    }
}
