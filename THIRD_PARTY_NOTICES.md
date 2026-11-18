# Third-party notices

KTKLegacy itself is MIT-licensed (see [LICENSE](LICENSE)). It includes or is derived from the
following third-party work, each under its own license.

## ktk-schedule (protocol)

The site protocol (TanStack Start server functions, seroval encoding, week parsing rules) used by
all four clients (Android, iOS, Symbian, Windows Phone) was ported from <https://github.com/kurumihere/ktk-schedule>.

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

MIT license, Copyright (c) 2016 Thomas Pornin. `inc/config.h` is patched for `__SYMBIAN32__`.

```
Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>

Permission is hereby granted, free of charge, to any person obtaining 
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be 
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## libcurl 8.0.1 — `ktk-ios/Vendor/curl`

curl license (MIT/X derivative), Copyright (c) 1996 – 2023, Daniel Stenberg, <daniel@haxx.se>, and many
contributors. <https://curl.se/docs/copyright.html>

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2023, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder.
```

## OpenSSL 3.4.6 — `ktk-ios/Vendor/curl/lib/libssl.a`, `libcrypto.a`

Apache License 2.0, Copyright (c) 1998-2025 The OpenSSL Project Authors.
<https://www.openssl.org/source/license.html>

```
                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   Copyright (c) 1998-2025 The OpenSSL Project Authors. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```

## Mozilla CA certificate bundle — `ktk-ios/Resources/cacert.pem`

Mozilla Public License 2.0, distributed as extracted by the curl project.
<https://curl.se/docs/caextract.html>

Covered Software is provided under the Mozilla Public License, Version 2.0.
Full license text available at <https://www.mozilla.org/MPL/2.0/>.

## ISRG / Let's Encrypt root certificates

- `ktk-android/res/raw/roots.pem`
- `ktk-symbian/certs/*.pem` and `ktk-symbian/src/core/trustanchors.c`
- `ktk-wp81/KTKLegacy/Resources/roots.pem`

Contain public root certificates (ISRG Root X1, ISRG Root X2, Let's Encrypt) of the Internet Security Research Group.
Root certificates are published without copyright restrictions (CC0 1.0 Universal / Public Domain).

## Windows Phone SDK Application Bar Icons — `ktk-wp81/KTKLegacy/Assets/Icons/`

Icons `today.png` and `refresh.png` are provided as standard Application Bar icons from the Microsoft Windows Phone SDK.
Copyright (c) Microsoft Corporation.
