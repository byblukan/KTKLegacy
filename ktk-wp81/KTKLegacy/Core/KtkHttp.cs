// KTK for Windows Phone 8.1 - HTTPS request handling.
// Port of ktk-android Http.java / Tls.java.
//
// WP81 build: requests go through the WinRT stack (Windows.Web.Http over WinHTTP).
// The Silverlight/IE browser stack silently drops the `sec-fetch-*` headers the
// site's WAF requires: without `sec-fetch-site: same-origin` the server answers
// 403 Forbidden to every call (verified against the live site 2026-11-18), and
// Silverlight's WebHeaderCollection swallows the assignment (restricted header).
// WinRT's TryAppendWithoutValidation sends arbitrary headers verbatim.
//
// WP8.0 build (KTKLegacy_WP80.csproj, no WP81 define): legacy HttpWebRequest path.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.IsolatedStorage;
using System.Net;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
#if WP81
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Storage.Streams;
using Windows.Web.Http;
using Windows.Web.Http.Filters;
using IAsyncInfo = Windows.Foundation.IAsyncInfo;
#endif

namespace KTKLegacy
{
    static class KtkHttp
    {
        public const int JsonLimit = 4 * 1024 * 1024;
        public const int FileLimit = 50 * 1024 * 1024;

        public sealed class Request
        {
            public string Method  = "GET";
            public string Url;
            public readonly Dictionary<string, string> Headers = new Dictionary<string, string>();
            public byte[] Body;             // POST only
            public int    Limit = JsonLimit;
            public int    TimeoutMs = 60000;
            public string OutputPath;       // stream body into IsolatedStorage at this path, or null for memory
        }

        public sealed class Response
        {
            public int               Status;
            public byte[]            Body;       // null when written to OutputPath
            public List<string>      SetCookies  = new List<string>();
            public string            Location;
        }

        // ---------------------------------------------------------------- URL escaping

        public static string Escape(string value)
        {
            if (value == null) return "";
            var sb = new StringBuilder();
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            foreach (byte b in bytes)
            {
                char c = (char)b;
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '_' || c == '.' || c == '~')
                    sb.Append(c);
                else
                    sb.AppendFormat("%{0:X2}", b);
            }
            return sb.ToString();
        }

        // ---------------------------------------------------------------- perform

        public static async Task<Response> Perform(Request request)
        {
            Uri uri;
            if (!Uri.TryCreate(request.Url, UriKind.Absolute, out uri)
                || !string.Equals(uri.Scheme, "https", StringComparison.OrdinalIgnoreCase))
                throw new KtkException(KtkErrorCode.Network, "Разрешён только HTTPS.");
#if WP81
            return await PerformWinRt(request, uri);
#else
            return await PerformLegacy(request, uri);
#endif
        }

#if WP81
        // ---------------------------------------------------------------- WP8.1: WinRT / WinHTTP

        private static async Task<Response> PerformWinRt(Request request, Uri uri)
        {
            var filter = new HttpBaseProtocolFilter();
            filter.AllowAutoRedirect = false;

            // Cookies are handled by the filter's cookie manager (the manual
            // Cookie header is a restricted header in the browser stack).
            string cookieHeader = null;
            foreach (var kv in request.Headers)
                if (string.Equals(kv.Key, "Cookie", StringComparison.OrdinalIgnoreCase))
                {
                    cookieHeader = kv.Value;
                    break;
                }
            ApplyCookies(filter.CookieManager, uri, cookieHeader);

            using (var client = new HttpClient(filter))
            using (var msg = new HttpRequestMessage(new HttpMethod(request.Method), uri))
            {
                if (request.Method == "POST" && request.Body != null)
                    msg.Content = new HttpBufferContent(request.Body.AsBuffer());

                foreach (var kv in request.Headers)
                {
                    string lower = kv.Key.ToLowerInvariant();
                    if (lower == "cookie")          continue; // applied through the cookie manager
                    if (lower == "content-length")  continue; // computed from the body
                    if (lower == "content-type")
                    {
                        if (msg.Content != null)
                            msg.Content.Headers.TryAppendWithoutValidation("Content-Type", kv.Value);
                        continue;
                    }
                    // Everything else (sec-fetch-site, Origin, Referer, User-Agent,
                    // x-tsr-serverfn, ...) must reach the wire unvalidated.
                    msg.Headers.TryAppendWithoutValidation(kv.Key, kv.Value);
                }
                msg.Headers.TryAppendWithoutValidation("Accept-Encoding", "identity");

                int timeoutMs = request.TimeoutMs > 0 ? request.TimeoutMs : 600000;
                long started = DateTime.UtcNow.Ticks;

                HttpResponseMessage resp;
                try
                {
                    // No CancellationToken overload on WP8.1: race the operation against
                    // a delay and cancel it through IAsyncInfo when the time runs out.
                    var sendOp  = client.SendRequestAsync(msg, HttpCompletionOption.ResponseHeadersRead);
                    var sendTsk = sendOp.AsTask();
                    Task first  = await Task.WhenAny(sendTsk, Task.Delay(timeoutMs));
                    if (first != sendTsk)
                    {
                        Observe(sendTsk);
                        try { ((IAsyncInfo)sendOp).Cancel(); } catch { }
                        throw new KtkException(KtkErrorCode.Network, "Сайт не ответил вовремя.");
                    }
                    resp = await sendTsk;
                }
                catch (KtkException) { throw; }
                catch (Exception ex) { throw RequestFailure(ex); }

                using (resp)
                {
                    var response = new Response { Status = (int)resp.StatusCode };
                    string location;
                    if (resp.Headers.TryGetValue("Location", out location))
                        response.Location = location;

                    int elapsedMs = (int)((DateTime.UtcNow.Ticks - started) / TimeSpan.TicksPerMillisecond);
                    int remaining = timeoutMs - elapsedMs;
                    if (remaining < 1) remaining = 1;

                    try
                    {
                        Task body = ReadBody(response, request, resp);
                        Task first = await Task.WhenAny(body, Task.Delay(remaining));
                        if (first != body)
                        {
                            Observe(body);
                            throw new KtkException(KtkErrorCode.Network, "Сайт не ответил вовремя.");
                        }
                        await body;
                    }
                    catch (KtkException) { throw; }
                    catch (Exception ex) { throw RequestFailure(ex); }

                    CollectCookies(filter.CookieManager, uri, response);
                    return response;
                }
            }
        }

        private static async Task ReadBody(Response response, Request request, HttpResponseMessage resp)
        {
            IInputStream input = await resp.Content.ReadAsInputStreamAsync();
            using (Stream stream = input.AsStreamForRead())
            {
                if (request.OutputPath != null && response.Status == 200)
                    await CopyToFile(stream, request.OutputPath, request.Limit);
                else
                    response.Body = await ReadAll(stream, request.Limit);
            }
        }

        // A timed-out operation is disposed with the HttpClient below; just make
        // sure its eventual fault does not surface as an unobserved task error.
        private static void Observe(Task task)
        {
            try
            {
                if (task != null)
                    task.ContinueWith(t => { var ignored = t.Exception; },
                                      TaskContinuationOptions.OnlyOnFaulted);
            }
            catch { }
        }

        // The jar the client sends is the source of truth: cookies it names are
        // (re)applied, anything else is dropped so no stale session survives.
        private static void ApplyCookies(Windows.Web.Http.HttpCookieManager jar, Uri uri, string cookieHeader)
        {
            var desired = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (!string.IsNullOrEmpty(cookieHeader))
            {
                foreach (string part in cookieHeader.Split(';'))
                {
                    string pair = part.Trim();
                    int eq = pair.IndexOf('=');
                    if (eq > 0)
                        desired[pair.Substring(0, eq).Trim()] = pair.Substring(eq + 1).Trim();
                }
            }
            try
            {
                foreach (var existing in jar.GetCookies(uri))
                    if (!desired.ContainsKey(existing.Name))
                        jar.DeleteCookie(existing);
                foreach (var kv in desired)
                {
                    var cookie = new HttpCookie(kv.Key, uri.Host, "/") { Value = kv.Value, Secure = true };
                    jar.SetCookie(cookie);
                }
            }
            catch { /* cookies are best effort; auth failures surface as 401 */ }
        }

        private static void CollectCookies(Windows.Web.Http.HttpCookieManager jar, Uri uri, Response response)
        {
            try
            {
                foreach (var cookie in jar.GetCookies(uri))
                    response.SetCookies.Add(cookie.Name + "=" + cookie.Value);
            }
            catch { }
        }

        private static KtkException RequestFailure(Exception ex)
        {
            if (ex is OperationCanceledException || ex is TaskCanceledException)
                return new KtkException(KtkErrorCode.Network, "Сайт не ответил вовремя.");

            string msg = ex.Message ?? "";
            string lower = msg.ToLowerInvariant();
            int hr = ex.HResult;
            // Common WinHTTP HRESULTs: 0x80072EE2 timeout, 0x80072F8F security failure,
            // 0x80072EFD connection reset, 0x80072EFB name resolution.
            if (hr == unchecked((int)0x80072EE2))
                return new KtkException(KtkErrorCode.Network, "Сайт не ответил вовремя.");
            if (hr == unchecked((int)0x80072F8F) || hr == unchecked((int)0x80072F05)
                || lower.Contains("certificate") || lower.Contains("ssl") || lower.Contains("tls")
                || lower.Contains("trust"))
                return new KtkException(KtkErrorCode.Network,
                    "Ошибка TLS (сертификат сайта не принят): 0x" + hr.ToString("X8"));
            string detail = string.IsNullOrEmpty(msg)
                ? "Сеть недоступна (0x" + hr.ToString("X8") + ")"
                : msg;
            return new KtkException(KtkErrorCode.Network, detail);
        }
#endif

        private static async Task<byte[]> ReadAll(Stream stream, int limit)
        {
            if (stream == null) return new byte[0];
            var buf = new MemoryStream();
            byte[] tmp = new byte[8192];
            int read;
            while ((read = await stream.ReadAsync(tmp, 0, tmp.Length)) > 0)
            {
                if (buf.Length + read > limit)
                    throw new KtkException(KtkErrorCode.TooLarge, "Ответ слишком большой.");
                buf.Write(tmp, 0, read);
            }
            return buf.ToArray();
        }

        private static async Task CopyToFile(Stream stream, string isoPath, int limit)
        {
            using (var store = IsolatedStorageFile.GetUserStoreForApplication())
            {
                string dir = Path.GetDirectoryName(isoPath);
                if (!string.IsNullOrEmpty(dir) && !store.DirectoryExists(dir))
                    store.CreateDirectory(dir);
                using (var file = store.OpenFile(isoPath, FileMode.Create, FileAccess.Write))
                {
                    byte[] buf = new byte[8192];
                    long written = 0;
                    int read;
                    while ((read = await stream.ReadAsync(buf, 0, buf.Length)) > 0)
                    {
                        written += read;
                        if (written > limit)
                            throw new KtkException(KtkErrorCode.TooLarge, "Файл слишком большой.");
                        await file.WriteAsync(buf, 0, read);
                    }
                }
            }
        }

#if !WP81
        // ---------------------------------------------------------------- WP8.0: HttpWebRequest (legacy)

        private static async Task<Response> PerformLegacy(Request request, Uri uri)
        {
            var req = (HttpWebRequest)WebRequest.Create(uri);
            req.Method = request.Method;
            try { req.AllowAutoRedirect = false; } catch { }
            try { req.CookieContainer = new CookieContainer(); } catch { }

            foreach (var kv in request.Headers)
                SetHeader(req, kv.Key, kv.Value);

            if (request.Method == "POST" && request.Body != null)
            {
                try { req.ContentLength = request.Body.Length; } catch { }
                using (var bodyStream = await req.GetRequestStreamAsync())
                    await bodyStream.WriteAsync(request.Body, 0, request.Body.Length);
            }

            HttpWebResponse resp;
            try
            {
                Task<WebResponse> responseTask = req.GetResponseAsync();
                if (request.TimeoutMs > 0)
                {
                    Task timeoutTask = Task.Delay(request.TimeoutMs);
                    Task completed = await Task.WhenAny(responseTask, timeoutTask);
                    if (completed == timeoutTask)
                    {
                        try { req.Abort(); } catch { }
                        throw new KtkException(KtkErrorCode.Network, "Сайт не ответил вовремя.");
                    }
                }
                resp = (HttpWebResponse)await responseTask;
            }
            catch (KtkException)
            {
                throw;
            }
            catch (WebException ex)
            {
                if (ex.Response != null)
                {
                    resp = (HttpWebResponse)ex.Response;
                }
                else
                {
                    string msg = ex.Message ?? "";
                    if (ex.Status == WebExceptionStatus.Timeout)
                        throw new KtkException(KtkErrorCode.Network, "Сайт не ответил вовремя.");
                    // TLS errors
                    if (msg.IndexOf("SSL", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        msg.IndexOf("TLS", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        msg.IndexOf("certificate", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        msg.IndexOf("trust", StringComparison.OrdinalIgnoreCase) >= 0)
                        throw new KtkException(KtkErrorCode.Network, "Ошибка TLS: " + msg);
                    throw new KtkException(KtkErrorCode.Network, msg);
                }
            }
            catch (Exception ex)
            {
                throw new KtkException(KtkErrorCode.Network, ex.Message);
            }

            var response = new Response { Status = (int)resp.StatusCode };
            // Collect headers
            try
            {
                for (int i = 0; i < resp.Headers.Count; i++)
                {
                    string name = resp.Headers.AllKeys[i];
                    if (name == null) continue;
                    if (string.Equals(name, "Set-Cookie", StringComparison.OrdinalIgnoreCase))
                        response.SetCookies.Add(resp.Headers[name]);
                    else if (string.Equals(name, "Location", StringComparison.OrdinalIgnoreCase))
                        response.Location = resp.Headers[name];
                }
            }
            catch { }

            // Collect cookies from CookieContainer if any
            try
            {
                if (req.CookieContainer != null)
                {
                    CookieCollection cookies = req.CookieContainer.GetCookies(uri);
                    foreach (Cookie c in cookies)
                    {
                        response.SetCookies.Add(c.Name + "=" + c.Value);
                    }
                }
            }
            catch { }

            using (var respStream = resp.GetResponseStream())
            {
                if (request.OutputPath != null && response.Status == 200)
                {
                    await CopyToFile(respStream, request.OutputPath, request.Limit);
                }
                else
                {
                    response.Body = await ReadAll(respStream, request.Limit);
                }
            }
            return response;
        }

        // Some headers must be set via dedicated properties in .NET
        private static void SetHeader(HttpWebRequest req, string name, string value)
        {
            try
            {
                switch (name.ToLowerInvariant())
                {
                    case "accept":          req.Accept      = value; break;
                    case "content-type":    req.ContentType = value; break;
                    case "user-agent":      req.UserAgent   = value; break;
                    case "content-length":  /* set elsewhere */ break;
                    case "cookie":
                        SetCookieHeader(req, value);
                        break;
                    default:
                        req.Headers[name] = value;
                        break;
                }
            }
            catch
            {
                // Ignore restricted or unsupported headers in Silverlight WebHeaderCollection
            }
        }

        private static void SetCookieHeader(HttpWebRequest req, string cookieString)
        {
            try
            {
                if (req.CookieContainer == null)
                    req.CookieContainer = new CookieContainer();
                string[] parts = cookieString.Split(';');
                foreach (string part in parts)
                {
                    string trimmed = part.Trim();
                    if (string.IsNullOrEmpty(trimmed)) continue;
                    int eq = trimmed.IndexOf('=');
                    if (eq > 0)
                    {
                        string cname = trimmed.Substring(0, eq).Trim();
                        string cval  = trimmed.Substring(eq + 1).Trim();
                        req.CookieContainer.Add(req.RequestUri, new Cookie(cname, cval));
                    }
                }
            }
            catch
            {
                try { req.Headers["Cookie"] = cookieString; } catch { }
            }
        }
    }

    public static class HttpWebRequestExtensions
    {
        public static Task<Stream> GetRequestStreamAsync(this HttpWebRequest request)
        {
            var tcs = new TaskCompletionSource<Stream>();
            try
            {
                request.BeginGetRequestStream(ar =>
                {
                    try
                    {
                        var s = request.EndGetRequestStream(ar);
                        tcs.TrySetResult(s);
                    }
                    catch (Exception ex)
                    {
                        tcs.TrySetException(ex);
                    }
                }, null);
            }
            catch (Exception ex)
            {
                tcs.TrySetException(ex);
            }
            return tcs.Task;
        }

        public static Task<WebResponse> GetResponseAsync(this HttpWebRequest request)
        {
            var tcs = new TaskCompletionSource<WebResponse>();
            try
            {
                request.BeginGetResponse(ar =>
                {
                    try
                    {
                        var r = request.EndGetResponse(ar);
                        tcs.TrySetResult(r);
                    }
                    catch (Exception ex)
                    {
                        tcs.TrySetException(ex);
                    }
                }, null);
            }
            catch (Exception ex)
            {
                tcs.TrySetException(ex);
            }
            return tcs.Task;
        }
    }
#else
    }
#endif
}
