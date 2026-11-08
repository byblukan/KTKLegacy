# KTKLegacy

КТК для iOS 6.

Клиент `ktk.education-system.ru` для iOS 6 (armv7): расписание на неделю, оценки, задания,
вебинары и файлы. Протокол перенесён из бота [kurumihere/ktk-schedule](https://github.com/kurumihere/ktk-schedule)
(BSD-3): серверные функции TanStack Start, формат seroval.

Системный сетевой стек iOS 6 не доверяет сертификату сайта (Let's Encrypt, ISRG Root X1/X2),
поэтому все запросы идут через libcurl 8.0.1 + OpenSSL 3.4.6 (`Vendor/curl`, только armv7)
со списком корневых сертификатов Mozilla (`Resources/cacert.pem`).

## Сборка на Mac (OS X 10.7+, Xcode 4.5–4.6)

Xcode-проект не нужен, скрипт сам вызывает clang из Xcode:

```sh
cd ktk-ios
./build-mac.sh                     # build/KTKLegacy.app, build/ru.ktkschedule.ios6_*.deb, build/KTKLegacy.ipa
./build-mac.sh install root@<ip>   # собрать и поставить .deb на устройство по SSH (нужен OpenSSH из Cydia)
./build-mac.sh clean
```

- SDK ищется сам (`iPhoneOS6.1.sdk` из Xcode 4.6). Если не нашёлся, укажите путь: `SDK=/путь/к/iPhoneOS6.1.sdk ./build-mac.sh`.
- Приложение подписывается ad-hoc (`codesign -s -`) с `entitlements.plist`. Этого достаточно для устройства с джейлбрейком.
  С сертификатом разработчика: `SIGN_IDENTITY="iPhone Developer: …" ./build-mac.sh`.
- `.deb` ставится через `dpkg -i` (или `install`). `.ipa` подходит для AppSync и IPA Installer.

## Сборка (theos на Linux)

```sh
export THEOS=~/theos
git clone --recursive https://github.com/theos/theos.git $THEOS

# Тулчейн iOS для Linux
curl -LO https://github.com/L1ghtmann/llvm-project/releases/latest/download/iOSToolchain-x86_64.tar.xz
tar -xf iOSToolchain-x86_64.tar.xz -C $THEOS/toolchain

# SDK 9.3 (последний с armv7 и deployment target 6.0)
git clone --depth 1 --filter=blob:none --sparse https://github.com/theos/sdks.git /tmp/sdks
git -C /tmp/sdks sparse-checkout set iPhoneOS9.3.sdk
cp -r /tmp/sdks/iPhoneOS9.3.sdk $THEOS/sdks/

cd ktk-ios
make package            # -> packages/ru.ktkschedule.ios6_*.deb
```

Вместо ручной установки можно использовать официальный установщик theos (он ставит зависимости через pacman):
`bash -c "$(curl -fsSL https://raw.githubusercontent.com/theos/theos/master/bin/install-theos)"`.

## Установка (джейлбрейк)

Скопировать `.deb` на устройство и выполнить `dpkg -i ru.ktkschedule.ios6_*.deb` (postinst вызовет `uicache`),
или `make package install THEOS_DEVICE_IP=<ip>`. OpenSSH на iOS 6 старый, современному клиенту может
понадобиться `-o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa`.

## Устройство кода

| Файл | Что делает |
|---|---|
| `KTKHTTP` | libcurl: HTTPS-only, TLS 1.2+, проверка сертификата, общий пул соединений, лимиты размера |
| `KTKSeroval` | кодирование запросов и разбор ответов seroval (`model.rs` бота) |
| `KTKClient` | вход, неделя, задания/вебинары, файлы, выход; повторный поиск id серверных функций после обновления сайта |
| `KTKWeek` | разбор недели: пары, кабинеты, преподаватели, звонки, оценки, задания |
| `KTKCalendar` | даты в UTC+5 арифметикой (tzdata iOS 6 считает Екатеринбург UTC+6) |
| `KTKSession` | сессия (cookie) в Keychain; пароль не хранится |
| `KTK*ViewController` | вход, неделя (пары/оценки), пара (задание, вебинар, файлы через Quick Look) |

Кэш недель и скачанные файлы лежат в `Library/Caches` и удаляются при выходе.
