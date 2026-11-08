package ru.ktkschedule.android;

import android.content.Context;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

/**
 * TLS for Android 4.1–4.4. The site accepts TLS 1.2+ with an ECDSA certificate only, while
 * Android before 5.0 supports TLS 1.2 but leaves it off. Its chain (Let's Encrypt YE2 → Root YE →
 * ISRG Root X2 → X1) is checked against the ISRG roots bundled in res/raw/roots.pem, which old
 * system stores lack; the system store stays as a fallback for a future change of CA.
 */
final class Tls {
    private static SSLSocketFactory factory;

    private Tls() {
    }

    static synchronized SSLSocketFactory socketFactory(Context context) throws KtkError {
        if (factory != null) {
            return factory;
        }
        try {
            KeyStore store = KeyStore.getInstance(KeyStore.getDefaultType());
            store.load(null, null);
            CertificateFactory certificates = CertificateFactory.getInstance("X.509");
            int index = 0;
            for (byte[] der : pemBlocks(context.getResources().openRawResource(R.raw.roots))) {
                store.setCertificateEntry("root" + index++, certificates.generateCertificate(new ByteArrayInputStream(der)));
            }
            X509TrustManager own = trustManager(store);
            X509TrustManager system = null;
            try {
                system = trustManager(null);
            } catch (Exception ignored) {
                // The bundled roots are enough.
            }
            SSLContext tls = SSLContext.getInstance("TLS");
            tls.init(null, new TrustManager[]{new BundledTrust(own, system)}, null);
            factory = new Tls12SocketFactory(tls.getSocketFactory());
            return factory;
        } catch (Exception e) {
            throw new KtkError(KtkError.Code.NETWORK, "Не удалось настроить TLS: " + e);
        }
    }

    private static X509TrustManager trustManager(KeyStore store) throws Exception {
        TrustManagerFactory tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        tmf.init(store);
        for (TrustManager tm : tmf.getTrustManagers()) {
            if (tm instanceof X509TrustManager) {
                return (X509TrustManager) tm;
            }
        }
        throw new IllegalStateException("no X509TrustManager");
    }

    /** DER bodies of the PEM blocks; comments between blocks are skipped. */
    private static List<byte[]> pemBlocks(InputStream in) throws IOException {
        ByteArrayOutputStream raw = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        try {
            int n;
            while ((n = in.read(buffer)) > 0) {
                raw.write(buffer, 0, n);
            }
        } finally {
            in.close();
        }
        String text = new String(raw.toByteArray(), "US-ASCII");
        List<byte[]> blocks = new ArrayList<byte[]>();
        String begin = "-----BEGIN CERTIFICATE-----";
        String end = "-----END CERTIFICATE-----";
        int position = 0;
        while (true) {
            int start = text.indexOf(begin, position);
            if (start < 0) {
                break;
            }
            int stop = text.indexOf(end, start);
            if (stop < 0) {
                break;
            }
            String body = text.substring(start + begin.length(), stop).replaceAll("\\s", "");
            blocks.add(android.util.Base64.decode(body, android.util.Base64.DEFAULT));
            position = stop + end.length();
        }
        return blocks;
    }

    private static final class BundledTrust implements X509TrustManager {
        private final X509TrustManager own;
        private final X509TrustManager system;

        BundledTrust(X509TrustManager own, X509TrustManager system) {
            this.own = own;
            this.system = system;
        }

        @Override
        public void checkClientTrusted(X509Certificate[] chain, String authType) throws CertificateException {
            throw new CertificateException("client certificates are not used");
        }

        @Override
        public void checkServerTrusted(X509Certificate[] chain, String authType) throws CertificateException {
            try {
                own.checkServerTrusted(chain, authType);
            } catch (CertificateException e) {
                if (system == null) {
                    throw e;
                }
                system.checkServerTrusted(chain, authType);
            }
        }

        @Override
        public X509Certificate[] getAcceptedIssuers() {
            List<X509Certificate> all = new ArrayList<X509Certificate>(Arrays.asList(own.getAcceptedIssuers()));
            if (system != null) {
                all.addAll(Arrays.asList(system.getAcceptedIssuers()));
            }
            return all.toArray(new X509Certificate[all.size()]);
        }
    }

    /** Enables TLS 1.2 (and 1.3 where present) and the ECDSA suites the site needs; nothing older. */
    private static final class Tls12SocketFactory extends SSLSocketFactory {
        private final SSLSocketFactory delegate;

        Tls12SocketFactory(SSLSocketFactory delegate) {
            this.delegate = delegate;
        }

        private Socket configure(Socket socket) throws IOException {
            if (!(socket instanceof SSLSocket)) {
                return socket;
            }
            SSLSocket ssl = (SSLSocket) socket;
            List<String> protocols = new ArrayList<String>();
            for (String protocol : ssl.getSupportedProtocols()) {
                if (protocol.equals("TLSv1.2") || protocol.equals("TLSv1.3")) {
                    protocols.add(protocol);
                }
            }
            if (protocols.isEmpty()) {
                socket.close();
                throw new IOException("устройство не поддерживает TLS 1.2");
            }
            ssl.setEnabledProtocols(protocols.toArray(new String[protocols.size()]));

            Set<String> suites = new LinkedHashSet<String>();
            for (String suite : ssl.getSupportedCipherSuites()) {
                // Forward-secret AEAD/CBC suites with an ECDSA key: the only kind the site serves.
                if (suite.startsWith("TLS_ECDHE_ECDSA_WITH_AES_") || suite.startsWith("TLS_ECDHE_ECDSA_WITH_CHACHA20")) {
                    suites.add(suite);
                }
            }
            suites.addAll(Arrays.asList(ssl.getEnabledCipherSuites()));
            ssl.setEnabledCipherSuites(suites.toArray(new String[suites.size()]));
            return ssl;
        }

        @Override
        public String[] getDefaultCipherSuites() {
            return delegate.getDefaultCipherSuites();
        }

        @Override
        public String[] getSupportedCipherSuites() {
            return delegate.getSupportedCipherSuites();
        }

        @Override
        public Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException {
            return configure(delegate.createSocket(s, host, port, autoClose));
        }

        @Override
        public Socket createSocket() throws IOException {
            return configure(delegate.createSocket());
        }

        @Override
        public Socket createSocket(String host, int port) throws IOException {
            return configure(delegate.createSocket(host, port));
        }

        @Override
        public Socket createSocket(String host, int port, InetAddress localHost, int localPort) throws IOException {
            return configure(delegate.createSocket(host, port, localHost, localPort));
        }

        @Override
        public Socket createSocket(InetAddress host, int port) throws IOException {
            return configure(delegate.createSocket(host, port));
        }

        @Override
        public Socket createSocket(InetAddress address, int port, InetAddress localAddress, int localPort) throws IOException {
            return configure(delegate.createSocket(address, port, localAddress, localPort));
        }
    }
}
