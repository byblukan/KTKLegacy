// KTK for Windows Phone 8.1 - every failure of this app, with the text shown to the user.
// Port of ktk-android KtkError.java.
using System;

namespace KTKLegacy
{
    public enum KtkErrorCode
    {
        Network,        // no connection, TLS failure, timeout
        HttpStatus,     // unexpected HTTP status
        TooLarge,       // response above the size limit
        BadResponse,    // not JSON / not the expected shape (often: the site was updated)
        LoginRequired,  // the site rejected the saved session
        Credentials,    // the site rejected login and password; detail = site message
        Server,         // other in-band error of a server function
        File,           // local file system failure
    }

    public class KtkException : Exception
    {
        public readonly KtkErrorCode Code;
        public readonly string Detail;
        public readonly int Status;

        public KtkException(KtkErrorCode code, string detail, int status = 0)
            : base(code + (detail != null ? ": " + detail : ""))
        {
            Code   = code;
            Detail = detail;
            Status = status;
        }

        public static KtkException ForStatus(int status)
        {
            return new KtkException(KtkErrorCode.HttpStatus,
                "Сайт ответил ошибкой " + status + ".", status);
        }

        public static bool IsLoginRequired(Exception e)
        {
            var ke = e as KtkException;
            return ke != null && ke.Code == KtkErrorCode.LoginRequired;
        }

        public static string Text(Exception error)
        {
            if (error == null) return null;
            var e = error as KtkException;
            if (e == null) return error.Message ?? error.ToString();
            switch (e.Code)
            {
                case KtkErrorCode.Network:
                    return e.Detail != null
                        ? "Нет связи с сайтом колледжа.\n(" + e.Detail + ")"
                        : "Нет связи с сайтом колледжа.";
                case KtkErrorCode.LoginRequired:
                    return "Сессия закончилась. Войдите снова.";
                case KtkErrorCode.Credentials:
                    return e.Detail ?? "Неверный логин или пароль.";
                case KtkErrorCode.BadResponse:
                    return "Сайт ответил непонятно. Возможно, он обновился.";
                default:
                    return e.Detail ?? "Ошибка сайта.";
            }
        }
    }
}
