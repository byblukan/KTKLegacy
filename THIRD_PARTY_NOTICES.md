# Third-party notices

KTKLegacy itself is MIT-licensed (see [LICENSE](LICENSE)). It includes or is derived from the
following third-party work, each under its own license.

## ktk-schedule (protocol)

The site protocol (TanStack Start server functions, seroval encoding, week parsing rules) used by
all three clients was ported from <https://github.com/kurumihere/ktk-schedule>.

```
Copyright (c) 2026 kurumi.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## BearSSL — `ktk-symbian/third_party/bearssl`

MIT license, Copyright (c) 2016 Thomas Pornin. Full text:
[ktk-symbian/third_party/bearssl/LICENSE.txt](ktk-symbian/third_party/bearssl/LICENSE.txt).
`inc/config.h` is patched for `__SYMBIAN32__`.

## libcurl 8.0.1 — `ktk-ios/Vendor/curl`

curl license (MIT/X derivative), Copyright (c) 1996 – 2023, Daniel Stenberg, <daniel@haxx.se>, and many
contributors. <https://curl.se/docs/copyright.html>

## OpenSSL 3.4.6 — `ktk-ios/Vendor/curl/lib/libssl.a`, `libcrypto.a`

Apache License 2.0, Copyright (c) 1998-2025 The OpenSSL Project Authors.
<https://www.openssl.org/source/license.html>

## Mozilla CA certificate bundle — `ktk-ios/Resources/cacert.pem`

Mozilla Public License 2.0, distributed as extracted by the curl project.
<https://curl.se/docs/caextract.html>

## ISRG / Let's Encrypt root certificates

`ktk-android/res/raw/roots.pem`, `ktk-symbian/certs/*.pem` and the generated
`ktk-symbian/src/core/trustanchors.c` contain public root certificates of the Internet Security Research Group.
