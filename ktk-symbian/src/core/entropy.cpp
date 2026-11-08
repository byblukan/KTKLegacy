// KTK for Symbian - secure random bytes from the platform.
#include "entropy.h"

#include <QDateTime>
#include <QFile>
#include <QTime>

#if defined(Q_OS_SYMBIAN)
#include <e32math.h>
#include <e32std.h>
#endif

#include "bearssl.h"

QByteArray Entropy::bytes(int count)
{
    QByteArray pool;
#if defined(Q_OS_SYMBIAN)
    // Math::Random draws on the kernel's random pool (Symbian^3 and later).
    for (int i = 0; i < count + 32; i += 4) {
        TUint32 r = Math::Random();
        pool.append(reinterpret_cast<const char *>(&r), 4);
        TUint32 fast = User::FastCounter();
        pool.append(reinterpret_cast<const char *>(&fast), 4);
    }
#else
    QFile f(QLatin1String("/dev/urandom"));
    if (f.open(QIODevice::ReadOnly)) pool = f.read(count + 32);
#endif
    const qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    pool.append(reinterpret_cast<const char *>(&now), sizeof now);
    const int ms = QTime::currentTime().msec();
    pool.append(reinterpret_cast<const char *>(&ms), sizeof ms);

    // Condense whatever was gathered through an HMAC-DRBG, so the output is uniform whatever
    // the quality of the individual readings.
    br_hmac_drbg_context drbg;
    br_hmac_drbg_init(&drbg, &br_sha256_vtable, pool.constData(), size_t(pool.size()));
    QByteArray out(count, '\0');
    br_hmac_drbg_generate(&drbg, out.data(), size_t(count));
    return out;
}
