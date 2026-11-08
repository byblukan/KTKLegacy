# KTKLegacy для Android 4.1+

Клиент `ktk.education-system.ru` для старых Android (4.1–4.4 и новее). Это порт `../ktk-ios`:
расписание на неделю, оценки, задания, вебинары и файлы. Протокол тот же: серверные функции
TanStack Start, формат seroval (из бота [kurumihere/ktk-schedule](https://github.com/kurumihere/ktk-schedule), BSD-3).

Java без AndroidX и сторонних библиотек, APK около 70 КБ.

## TLS

Сайт принимает только TLS 1.2+ и только ECDSA-сертификат. Цепочка: Let's Encrypt YE2 → Root YE →
ISRG Root X2 → X1. В Android 4.1–4.4 TLS 1.2 есть, но выключен, а корней ISRG в системе нет. Поэтому `Tls`:

- включает на сокетах TLS 1.2 (и 1.3, где он есть) и шифры `ECDHE_ECDSA` (на 4.1–4.3 работает `ECDHE-ECDSA-AES128-SHA`);
- проверяет сертификат по корням ISRG X1 и X2 из `res/raw/roots.pem`, а системное хранилище использует запасным
  вариантом.

## Сборка (Arch)

```sh
sudo pacman -S jdk-openjdk                       # подойдёт любой JDK 17+
yay -S android-sdk-build-tools android-platform-16 android-sdk-platform-tools
./build.sh            # build/ktk.apk
./build.sh install    # собрать, поставить через adb и запустить
./build.sh clean
```

Gradle не нужен: скрипт вызывает aapt2 → javac → d8 → zipalign → apksigner. Код компилируется против
`android.jar` API 16, поэтому вызов API новее 4.1 даст ошибку сборки. Лямбды не используются: им нужен
`java.lang.invoke`, которого в android-16 нет.

APK подписывается ключом `~/.android/debug.keystore`, скрипт создаёт его сам. Можно указать свой ключ:
`KEYSTORE=… KEYSTORE_PASS=… ./build.sh`. Подпись v1 (JAR) с SHA-1 нужна Android до 7.0.

## Устройство кода (`src/ru/ktkschedule/android`)

| Файл | iOS | Что делает |
|---|---|---|
| `Http`, `Tls` | `KTKHTTP` | HttpsURLConnection, TLS 1.2, вшитые корни, только HTTPS, без автоматических редиректов, лимиты размера |
| `Seroval`, `Json` | `KTKSeroval` | кодирование запросов и разбор ответов seroval, защита от раздувания по ссылкам |
| `Client` | `KTKClient` | вход, неделя, задания и вебинары (3 потока), файлы, выход, поиск сменившихся адресов серверных функций |
| `Week`, `Lesson`, `TaskFile` | `KTKWeek` | разбор недели: пары, кабинеты, преподаватели, звонки, оценки, задания |
| `CollegeCalendar` | `KTKCalendar` | даты в UTC+5 арифметикой (в старом tzdata Екатеринбург = UTC+6) |
| `Session` | `KTKSession` | cookie сессии в приватных настройках; пароль не хранится, `allowBackup=false` |
| `LoginActivity`, `WeekActivity`, `LessonActivity` | контроллеры | вход; неделя с вкладками «Пары / Оценки»; пара |
| `FilesProvider` | Quick Look | отдаёт скачанные файлы другим приложениям через `content://` (замена FileProvider) |

Кэш недель (`cache/ktk-weeks`) и скачанные файлы (`cache/ktk-files`) удаляются при выходе и при входе в другой аккаунт.
На сайте приложение видно как устройство `ktk-android`.
