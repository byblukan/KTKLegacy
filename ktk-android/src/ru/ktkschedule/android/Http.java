package ru.ktkschedule.android;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLException;

/** HTTPS only, TLS 1.2 (see {@link Tls}), no automatic redirects, size limits. Blocking: never on the main thread. */
final class Http {
    static final int DEFAULT_LIMIT = 4 * 1024 * 1024;

    static final class Request {
        String method = "GET";
        String url;
        final Map<String, String> headers = new LinkedHashMap<String, String>();
        byte[] body;              // POST only
        int limit = DEFAULT_LIMIT; // max body bytes
        int timeoutMs = 60000;    // read timeout
        File output;              // stream the body into this file instead of memory
    }

    static final class Response {
        int status;
        byte[] body; // null when written to the output file
        final List<String> setCookies = new ArrayList<String>();
        String location;
    }

    private Http() {
    }

    static Response perform(Context context, Request request) throws KtkError {
        HttpsURLConnection connection = null;
        boolean reusable = false;
        try {
            URL url = new URL(request.url);
            if (!"https".equalsIgnoreCase(url.getProtocol())) {
                throw new KtkError(KtkError.Code.NETWORK, "Разрешён только HTTPS.");
            }
            connection = (HttpsURLConnection) url.openConnection();
            connection.setSSLSocketFactory(Tls.socketFactory(context));
            connection.setInstanceFollowRedirects(false);
            connection.setUseCaches(false);
            connection.setConnectTimeout(20000);
            connection.setReadTimeout(request.timeoutMs);
            connection.setRequestMethod(request.method);
            for (Map.Entry<String, String> header : request.headers.entrySet()) {
                connection.setRequestProperty(header.getKey(), header.getValue());
            }
            if ("POST".equals(request.method)) {
                byte[] body = request.body != null ? request.body : new byte[0];
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(body.length);
                OutputStream out = connection.getOutputStream();
                try {
                    out.write(body);
                } finally {
                    out.close();
                }
            }

            Response response = new Response();
            response.status = connection.getResponseCode();
            for (Map.Entry<String, List<String>> header : connection.getHeaderFields().entrySet()) {
                String name = header.getKey();
                if (name == null || header.getValue() == null) {
                    continue;
                }
                if (name.equalsIgnoreCase("set-cookie")) {
                    response.setCookies.addAll(header.getValue());
                } else if (name.equalsIgnoreCase("location") && !header.getValue().isEmpty()) {
                    response.location = header.getValue().get(0);
                }
            }
            InputStream in = response.status >= 400 ? connection.getErrorStream() : connection.getInputStream();
            if (request.output != null && response.status == 200) {
                copyToFile(in, request.output, request.limit);
            } else {
                response.body = readAll(in, request.limit);
            }
            reusable = true;
            return response;
        } catch (KtkError e) {
            throw e;
        } catch (SocketTimeoutException e) {
            throw new KtkError(KtkError.Code.NETWORK, "Сайт не ответил вовремя.");
        } catch (SSLException e) {
            throw new KtkError(KtkError.Code.NETWORK, "Ошибка TLS: " + e.getMessage());
        } catch (IOException e) {
            String message = e.getMessage();
            throw new KtkError(KtkError.Code.NETWORK, message != null ? message : e.toString());
        } catch (RuntimeException e) {
            // Old HttpURLConnection throws unchecked exceptions on some broken connections.
            throw new KtkError(KtkError.Code.NETWORK, e.toString());
        } finally {
            if (connection != null && !reusable) {
                connection.disconnect();
            }
        }
    }

    private static byte[] readAll(InputStream in, int limit) throws IOException, KtkError {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        if (in == null) {
            return out.toByteArray();
        }
        try {
            byte[] buffer = new byte[16384];
            int n;
            while ((n = in.read(buffer)) > 0) {
                if (out.size() + n > limit) {
                    throw new KtkError(KtkError.Code.TOO_LARGE, "Ответ слишком большой.");
                }
                out.write(buffer, 0, n);
            }
        } finally {
            in.close();
        }
        return out.toByteArray();
    }

    private static void copyToFile(InputStream in, File file, int limit) throws IOException, KtkError {
        if (in == null) {
            throw new KtkError(KtkError.Code.BAD_RESPONSE, null);
        }
        boolean ok = false;
        FileOutputStream out;
        try {
            out = new FileOutputStream(file);
        } catch (IOException e) {
            in.close();
            throw new KtkError(KtkError.Code.FILE, "Не удалось создать файл.");
        }
        try {
            byte[] buffer = new byte[65536];
            long size = 0;
            int n;
            while ((n = in.read(buffer)) > 0) {
                size += n;
                if (size > limit) {
                    throw new KtkError(KtkError.Code.TOO_LARGE, "Файл слишком большой.");
                }
                try {
                    out.write(buffer, 0, n);
                } catch (IOException e) {
                    throw new KtkError(KtkError.Code.FILE, "Не удалось записать файл.");
                }
            }
            out.getFD().sync();
            ok = true;
        } finally {
            in.close();
            try {
                out.close();
            } catch (IOException e) {
                ok = false;
            }
            if (!ok) {
                //noinspection ResultOfMethodCallIgnored
                file.delete();
            }
        }
    }

    /** Percent-encode everything except RFC 3986 unreserved characters. */
    static String escape(String value) {
        try {
            return URLEncoder.encode(value, "UTF-8").replace("+", "%20").replace("*", "%2A").replace("%7E", "~");
        } catch (UnsupportedEncodingException e) {
            throw new AssertionError(e);
        }
    }
}
