// KTK for Symbian - secure random bytes from the platform, used to seed BearSSL.
#ifndef ENTROPY_H
#define ENTROPY_H

#include <QByteArray>

namespace Entropy
{
    /// `count` random bytes: Math::Random (the kernel pool) on Symbian, /dev/urandom elsewhere,
    /// both mixed with high-resolution timer readings. Never empty.
    QByteArray bytes(int count);
}

#endif // ENTROPY_H
