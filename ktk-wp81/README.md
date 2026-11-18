# KTKLegacy для Windows Phone 8.1

Клиент `ktk.education-system.ru` для Windows Phone 8.1.
Порт `../ktk-android`: расписание на неделю, оценки, задания, вебинары и файлы.
Протокол тот же: серверные функции TanStack Start, формат seroval (из бота
[kurumihere/ktk-schedule](https://github.com/kurumihere/ktk-schedule), BSD-3).

C# / XAML, Silverlight-приложение WP8.1 (`.xap`). Без NuGet-пакетов.

## TLS и транспорт

Сайт принимает только TLS 1.2+ и только ECDSA-сертификат. Цепочка:
Let's Encrypt YE2 → Root YE → ISRG Root X2 → X1.

Windows Phone 8.1 поддерживает TLS 1.2 нативно; сертификат проверяется по
системному хранилищу телефона (WinHTTP). Переопределить проверку в Silverlight
нельзя — `ServicePointManager.ServerCertificateValidationCallback` в профиле
Windows Phone отсутствует, поэтому вшитые ISRG-корни `Resources/roots.pem`
оставлены только как справка (приложение их не читает).

## Почему вход падал с «Сайт ответил ошибкой 403»

WAF сайта требует заголовок `Sec-Fetch-Site: same-origin` — без него (и с любым
другим значением) каждый вызов получает 403 Forbidden (проверено на живом сайте
18.11.2026). Silverlight-стек WP (browser stack поверх IE/WinINET) считает такие
заголовки restricted: присвоение в `WebHeaderCollection` бросает исключение,
которое глотается в `KtkHttp.SetHeader`, и на провод заголовок не попадает.

Поэтому на WP8.1 запросы идут через WinRT `Windows.Web.Http` (WinHTTP) с
`TryAppendWithoutValidation`, который отправляет произвольные заголовки как есть,
а куки передаются через `HttpBaseProtocolFilter.CookieManager` (заголовок
`Cookie` в Silverlight тоже restricted). Сверено с живым сайтом: WinRT-стек с
`sec-fetch-site: same-origin` → 200, без него → 403.

Сборка `KTKLegacy_WP80.csproj` оставлена на старом `HttpWebRequest`-стеке и на
нём эта проблема сохраняется — эталонная платформа проекта — WP8.1.

## Сборка (Visual Studio 2015 или новее)

Нужна **Visual Studio 2015** с компонентом **Windows Phone 8.1 Tools**
(или Visual Studio 2017; новее уже не включают шаблон WP8.1 Silverlight).

```bat
:: Открыть KTKLegacy.sln в Visual Studio, выбрать ARM / Any CPU, Platform = Windows Phone 8.1
:: Build → Build Solution → KTKLegacy/Bin/Release/KTKLegacy_Release_AnyCPU.xap
```

Или командная строкой (нужен MSBuild из VS2015):
```bat
msbuild KTKLegacy.sln /p:Configuration=Release /p:Platform="Any CPU"
```

**Установка:**
- Включить в настройках телефона «Параметры → Информация → Дополнительно → Флаги функций → Режим разработчика».
- Либо использовать [Windows Phone Power Tools](https://wptools.codeplex.com/) для установки `.xap` без Dev Center.
- Или залить через `ILSpy` / ADB-аналог для WP — Windows Phone Application Deployment.

## Устройство кода

| Файл | Android | Что делает |
|---|---|---|
| `Core/KtkError.cs` | `KtkError` | коды ошибок + исключение |
| `Core/KtkHttp.cs` | `Http`, `Tls` | WP8.1: WinRT `Windows.Web.Http` (WinHTTP) — все заголовки, куки через CookieManager; WP8.0: HttpWebRequest; лимиты, только HTTPS |
| `Core/KtkSeroval.cs` | `Seroval`, `Json` | кодирование запросов и разбор ответов seroval |
| `Core/KtkClient.cs` | `Client`, `Session` | вход, неделя, задания/вебинары (3 потока), файлы, выход, поиск сменившихся адресов |
| `Core/KtkWeek.cs` | `Week`, `Lesson`, `TaskFile` | разбор недели: пары, кабинеты, преподаватели, звонки, оценки, задания |
| `Core/CollegeCalendar.cs` | `CollegeCalendar` | даты в UTC+5 арифметикой |
| `LoginPage.xaml` | `LoginActivity` | экран входа |
| `WeekPage.xaml` | `WeekActivity` | неделя с Pivot «Пары / Оценки», навигация |
| `LessonPage.xaml` | `LessonActivity` | детали пары: задание, вебинар, файлы |

Кэш недель (`IsolatedStorage\ktk-weeks`) и скачанные файлы (`IsolatedStorage\ktk-files`)
удаляются при выходе и при входе в другой аккаунт. Пароль не хранится.
На сайте приложение видно как устройство `ktk-wp81`.
