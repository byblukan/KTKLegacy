# KTKLegacy для Symbian

Клиент `ktk.education-system.ru` для Symbian^3 / Anna / Belle (проверялся под Nokia N8, Belle FP2).
Это порт `../ktk-android` и `../ktk-ios`: расписание на неделю, оценки, задания, вебинары и файлы.
Протокол тот же: серверные функции TanStack Start, формат seroval (из бота
[kurumihere/ktk-schedule](https://github.com/kurumihere/ktk-schedule), BSD-3).

C++ и Qt 4.7.4, интерфейс на QML (Qt Quick 1.1 и Qt Quick Components for Symbian).

## TLS

Сайт принимает только TLS 1.2+ и только ECDSA-сертификат. Цепочка: сертификат сайта (P-256) →
Let's Encrypt YE2 → Root YE → ISRG Root X2 → X1. Qt для Symbian работает через OpenSSL 0.9.8
телефона, а тот умеет только TLS 1.0. Поэтому в приложение встроен
[BearSSL](https://bearssl.org) (MIT, `third_party/bearssl`) поверх обычного `QTcpSocket`:

- только TLS 1.2, ECDHE-ECDSA/RSA с AES-GCM, ChaCha20 или AES-CBC;
- цепочка проверяется по корням из `src/core/trustanchors.c`: ISRG Root X1, X2 и Root YE;
  других корней нет, поэтому чужие сайты приложение не откроет;
- сертификат проверяется по дате телефона. Если дата сбита, будет понятная ошибка
  «проверьте дату и время»;
- случайные числа берутся из `Math::Random` (ядро Symbian), затем проходят через HMAC-DRBG.

Корни меняются так: положить PEM-файлы в `certs/` и выполнить `python3 tools/gen-trust-anchors.py`
(нужен модуль `cryptography`).

В сборку попадают не все файлы BearSSL, а только 106 используемых (`bearssl.pri`). Список
получен линковкой ядра в 32- и 64-битной сборке.

## Сборка .sis (Windows)

Нужен **Qt SDK 1.2.1** с целью «Qt 4.7.4 for Symbian Anna/Belle» (`SymbianSR1Qt474`, компилятор GCCE 4.4.1).

- Проект должен лежать **на том же диске, что и Qt SDK**, в пути **без пробелов и кириллицы**
  (так требует сборочная система Symbian). Например `C:\work\ktk-symbian` при SDK в `C:\QtSDK`.
- SDK ищется в `C:\QtSDK`. Если он в другом месте: `set QTSDK=D:\QtSDK`.

```bat
build-symbian.cmd          :: KTKLegacy_<версия>.sis (и KTKLegacy_installer_<версия>.sis для Anna)
build-symbian.cmd bump     :: сначала поднять последнюю цифру VERSION в KTKLegacy.pro
build-symbian.cmd clean
```

Пакет самоподписанный, ему нужна только возможность NetworkServices. На телефоне должна быть включена
установка любых приложений: «Настройки → Приложения → Установки → Параметры установки →
Установка ПО: Все».

Если Symbian отказывается ставить пакет поверх старого, соберите с `bump` или удалите старую версию.

## Сборка .sis (Linux, wine)

Тот же `build-symbian.cmd` с тем же Qt SDK 1.2.1, только под wine. Нужны `wine`, `7z` и `python3`
(Arch: `sudo pacman -S wine p7zip python`) и два файла из [QT-SDK.txt](QT-SDK.txt): офлайн-установщик
и архив Belle SDK. Установщик не запускается: скрипт вынимает из него компилятор, Raptor и Perl.

```sh
./build-symbian-wine.sh setup ~/Загрузки/QtSdk-offline-win-x86-v1.2.1.exe \
                              ~/Загрузки/Belle_SDK_for_QtSDK_v1.2.1_SymbianSR1Qt474.7z   # один раз, ~1 ГБ
./build-symbian-wine.sh          # KTKLegacy_<версия>.sis и KTKLegacy_installer_<версия>.sis в этой папке
./build-symbian-wine.sh bump     # сначала поднять последнюю цифру VERSION в KTKLegacy.pro
./build-symbian-wine.sh check    # UID, версия, зависимости и права собранного пакета
./build-symbian-wine.sh clean
```

SDK и префикс wine лежат в `~/.local/share/ktk-symbian-sdk` (другое место: `KTK_SYMBIAN_SDK=…`).
Собирается копия проекта в `C:\work\ktk-symbian` внутри префикса: файлы проекта (без того, что
в `.gitignore`) копируются туда перед каждой сборкой, готовые `.sis` возвращаются сюда.
Проверено с wine 11: первая сборка занимает около минуты, пакет ставится и работает на Nokia N8 (Belle FP2).

## Проверка на ПК (Linux, Qt 5)

Ядро (TLS, HTTP, seroval, клиент сайта) собирается и с Qt 5. Так протокол проверяется на живом
сайте без телефона:

```sh
mkdir build && cd build
qmake ../tools/ktk-cli/ktk-cli.pro && make -j8
./ktk-cli selftest                                   # JSON, seroval, даты: без сети
./ktk-cli get https://ktk.education-system.ru/       # TLS 1.2 через BearSSL, keep-alive
KTK_LOGIN=… KTK_PASSWORD=… ./ktk-cli signin          # вход; сессия в ./ktk-data
./ktk-cli week [2026-11-02]                          # неделя, как её покажет телефон
./ktk-cli file <№ пары> <№ файла>                    # скачать файл задания
./ktk-cli signout
```

`tools/app-check` собирает `AppController` без QML и прогоняет поток «сохранённая сессия → сайт её
отклонил → выход». Компоненты Symbian есть только на телефоне (и в Qt Simulator из SDK).

Код рассчитан на GCCE 4.4, то есть на C++98: без C++11, без API, которых нет в Qt 4.7.

## Устройство кода

| Файл | Android | Что делает |
|---|---|---|
| `src/core/tlsconnection` | `Tls` | BearSSL поверх блокирующего `QTcpSocket`, TLS 1.2, вшитые корни |
| `src/core/http` | `Http` | HTTP/1.1, только HTTPS, keep-alive, chunked, без автоматических редиректов, лимиты размера, запись в файл |
| `src/core/json`, `seroval` | `Json`, `Seroval` | свой JSON (в Qt 4 его нет), запросы и ответы seroval, защита от раздувания по ссылкам |
| `src/core/client` | `Client`, `Session` | вход, неделя, задания и вебинары, файлы, выход, поиск сменившихся адресов серверных функций |
| `src/core/week` | `Week`, `Lesson`, `TaskFile` | разбор недели: пары, кабинеты, преподаватели, звонки, оценки, задания |
| `src/core/collegecalendar` | `CollegeCalendar` | даты в UTC+5 арифметикой |
| `src/app/appcontroller` | активности | состояние для QML, фоновые потоки, подключение к сети (`QNetworkSession`) |
| `qml/` | разметка | вход; неделя с вкладками «Пары / Оценки»; пара |

Отличия от Android:

- задания и вебинары недели загружаются по очереди через одно keep-alive соединение, а не в 3 потока;
- скачанные файлы лежат в `E:\KTKLegacy` (массовая память N8) или `C:\Data\KTKLegacy`, а не в закрытой папке
  приложения: иначе их не откроет другое приложение. Открываются системным обработчиком;
- текст задания копируется долгим нажатием, ссылка на вебинар открывается в браузере или копируется.

Сессия (cookie) и сохранённые недели лежат в закрытой папке приложения. Пароль не хранится.
При выходе и при входе в другой аккаунт недели и файлы удаляются. На сайте приложение видно
как устройство `ktk-symbian`.

## Известное

Поиск сменившихся адресов серверных функций (`Client::crawl`) в текущей сборке сайта ничего не находит.
Это касается всех трёх клиентов, подробности в [корневом README](../README.md#известное).
