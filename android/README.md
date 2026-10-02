# FreeTunnel для Android

Android клиент TrustTunnel с интерфейсом FreeTunnel. Приложение использует официальный TrustTunnel Android адаптер и его нативное ядро для VPN соединения. Для рабочего APK требуется полная сборка адаптера либо опубликованный AAR со всеми нативными библиотеками.

## Возможности

- Импорт и редактирование TOML конфигов. Сохранённые конфиги и профиль активного соединения шифруются ключом Android Keystore.
- Импорт ссылок `tt://` и `https://trusttunnel.org/qr.html#tt=…` из буфера и через «Открыть в приложении».
- Подключение через настоящий Android `VpnService` с TrustTunnel native клиентом.
- Плитка быстрых настроек показывает состояние VPN и отключает соединение. Для подключения она открывает приложение и системное подтверждение VPN.
- Раздельная маршрутизация доменов и приложений. Режим «В обход VPN» отправляет выбранные домены и приложения напрямую; режим «Через VPN» пропускает выбранные домены и приложения через туннель. Изменения применяются переподключением.
- Проверка доступности сервера TCP соединением с первым адресом конфига. Это не измерение задержки VPN.
- Автоподключение при запуске и журнал событий соединения.
- Постоянный VPN и блокировку трафика без туннеля можно включить в системных настройках Android.

## Сборка APK

Нужны JDK 17, Android SDK 35+, CMake 3.24+, Android NDK 29.0.14206865+ и Gradle из wrapper. Для Android адаптера также нужны Python 3.13+, Conan 2.0.5+, Go 1.18.3+, Rust и Cargo NDK.

Ядро публикуется как GitHub Package. Для скачивания бинарной зависимости укажите GitHub токен с `read:packages`:

```powershell
$env:GPR_USER = "ваш_логин_github"
$env:GPR_KEY = "ваш_токен"
cd android
./gradlew.bat :app:assembleDebug
```

Локально можно собрать Android адаптер из исходников, если рядом с этим репозиторием находится `TrustTunnelClient`. Сначала подготовьте Conan пакеты по [инструкции TrustTunnelClient](https://github.com/TrustTunnel/TrustTunnelClient/blob/master/platform/android/README.md), затем запустите Gradle:

```powershell
git clone https://github.com/TrustTunnel/TrustTunnelClient.git ..\TrustTunnelClient
cd android
./gradlew.bat :app:assembleDebug
```

В этом режиме Gradle подключает модуль из `TrustTunnelClient/platform/android/lib`. Без соседней копии проекта он использует опубликованный GitHub Package.

APK будет в `android/app/build/outputs/apk/debug/app-debug.apk`.

## Лицензии

FreeTunnel распространяется под Apache-2.0. TrustTunnel Android адаптер и ядро распространяются под Apache-2.0; их лицензия приведена в оригинальном репозитории TrustTunnelClient.
