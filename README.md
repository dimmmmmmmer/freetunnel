# FreeTunnel

<img src="assets/logo.png" width="96" align="right" alt="FreeTunnel logo"/>

Графический клиент для [TrustTunnel](https://github.com/TrustTunnel/TrustTunnelClient) на Qt — для Linux, macOS и Windows.

## Установка

Скачайте готовую сборку под свою систему со страницы
[**Releases**](https://github.com/enrvate/freetunnel/releases/latest):

| Система | Файл |
| --- | --- |
| **Windows 10/11** | `freetunnel-windows-x86_64-Setup.exe` (установщик) или `…​.zip` (портативная) |
| **macOS** (Apple Silicon + Intel) | `freetunnel-macos-universal.dmg` |
| **Linux** | `freetunnel-linux-x86_64.AppImage` (универсальная) или `…​.tar.gz` |

Сборки **не подписаны** (нет сертификатов), поэтому при первом запуске система может предупредить:

- **macOS**: правый клик по приложению → **Открыть** (или
  `xattr -dr com.apple.quarantine /Applications/FreeTunnel.app`).
- **Windows**: SmartScreen → **Подробнее** → **Выполнить в любом случае**.
- **Linux (AppImage)**: `chmod +x freetunnel-linux-x86_64.AppImage && ./freetunnel-linux-x86_64.AppImage`.

> Для TUN-интерфейса VPN нужны повышенные права: на Windows установщик запрашивает
> UAC, на Linux/macOS приложение поднимает права при подключении.

## Быстрый старт

1. На вкладке **Конфиги** (＋) создайте конфиг или импортируйте его (см. ниже).
2. На главном экране выберите конфиг и нажмите на большой круг — это
   подключение/отключение.
3. В **Настройках**: автоподключение при старте, kill switch, тема, язык,
   горячие клавиши и проверка обновлений.

## Импорт конфигурации

- **Вкладка «Конфиги» → ＋** — создать новый TOML или импортировать.
- Официальный формат TrustTunnel `tt://?<base64url>` (тот же, что в QR-кодах и
  мобильных клиентах) — вставкой из буфера, файлом, или ссылкой при запуске:

```sh
FreeTunnel "tt://?AQAL...."
FreeTunnel /path/to/vpn.toml
```

## Управление снаружи

- **Диплинки** (как `happ://toggle`): `freetunnel://toggle`,
  `freetunnel://connect`, `freetunnel://disconnect`, плюс `tt://…` для импорта.
  Приложение одно-экземплярное — повторный запуск со ссылкой передаёт команду
  уже открытому окну.
- **Глобальные горячие клавиши** — настраиваются в Настройках (переключить /
  подключить / отключить); работают, даже когда окно свёрнуто.
- **Системный трей** — быстрые действия и сворачивание в трей вместо закрытия.

## Совместимость

- **Windows**: 10/11 (x64).
- **macOS**: 11 Big Sur и новее — **universal** (Apple Silicon и Intel в одном `.dmg`).
- **Linux**: AppImage запускается на большинстве актуальных дистрибутивов;
  `.tar.gz` требует системного Qt 6.

## Для разработчиков

Сборка полностью автоматизирована в GitHub Actions
([`.github/workflows/build.yml`](.github/workflows/build.yml)): клиент линкуется
с C++-ядром [`TrustTunnel/TrustTunnelClient`](https://github.com/TrustTunnel/TrustTunnelClient),
HTTP/3 в релизных сборках отключён. Юнит-тесты —
[`.github/workflows/tests.yml`](.github/workflows/tests.yml). Релиз публикуется
автоматически по тегу `v*`.

## Лицензия

См. [LICENSE](LICENSE).
