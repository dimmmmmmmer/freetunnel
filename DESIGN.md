# FreeTunnel — UI Design Spec (locked)

Target: a modern, lightweight, "mobile-grade" desktop client built with **Qt Quick
(QML)** on the existing C++ TrustTunnel core. This document is the source of truth
for the QML implementation.

## Principles

- Flat, light, minimal. No heavy frames/boxes, no redundant dividers, no titles
  where the active nav already makes context obvious.
- Single accent: **blue flame** `#185FA5` (light/dark aware). App mark = the flame.
- Everything adapts to light/dark mode.

## Navigation

- **Top navigation bar, centered, icons only, no logo/title/version.**
- Tabs (5): Подключение, Раздельный туннель, Конфиги, Настройки, Логи.
- Active tab highlighted (info background). Window grows downward; the navbar
  stays fixed at the top.
- Sub-screens (Создать конфиг, Сетевые адаптеры) are **not** tabs — they use a
  back arrow and no navbar.

## i18n

- Qt `qsTr()` + `.ts` translation files (Qt Linguist), runtime language switch.
- **Default language: English.** Russian provided.

## Screens

### 1. Подключение (Home)
- Big **ring that is itself the connect/disconnect button**.
- State is shown by the ring only (no status text/pill):
  - Connected → solid blue ring; center: blue flame + session timer (`01:24:36`).
  - Connecting/reconnecting → animated blue arc.
  - Disconnected → thin gray ring; center: gray flame + "Выключено"; speed tiles hidden.
- Below the ring: **config name + ▾** (collapsed dropdown). Selecting another config:
  - if connected → switch & reconnect immediately;
  - if disconnected → just set it for next connect.
- Two speed tiles: Загрузка (download) / Отправка (upload), live (fed by the
  tunnel-stats wrapper patch).

### 2. Раздельный туннель (Split tunnel)
- Master toggle (on/off).
- **Profiles**: named, switchable profiles (e.g. Default / Work / Gaming). A
  profile dropdown at the top + manage (create/rename/delete). Each profile owns
  its own mode + apps + domains.
- Mode: "Указанное — мимо VPN" / "Только указанное — в VPN".
- Приложения: per-app rows with toggles; + Добавить.
- Домены: chips list + "добавить домен" input.

### 3. Конфиги (Configs)
- Centered, **frameless** actions above the list:
  - **Импорт ▾** (primary, listed first) — collapsed dropdown: Из файла /
    Вставить из буфера обмена / Сканировать QR-код.
  - Создать (secondary).
- List rows: name, `host:port · protocol`, "активен" badge on the active one,
  `⋯` per-row menu (activate / edit / duplicate / export QR / delete).

### 4. Создать / редактировать конфиг (sub-screen)
Form (back arrow, no navbar). Fields (client-side only):
- Имя (display name)
- Хост сервера (hostname)
- Адрес(а) — `host:port` (one or more)
- Логин / Пароль
- Протокол — HTTP/2 | HTTP/3
- DNS-серверы (DNS is **per-config**, not a global setting)
- Custom SNI
- Routing profile (dropdown)
- Client random (hex sequence) — `prefix[/mask]`
- **Allow IPv6 connections** (checkbox)
- Сертификат (PEM) — **load from file** and/or paste from clipboard
- Actions: Отмена / Сохранить

Not exposed (server-side / forced):
- MTU, anti-DPI — configured server-side, not in the client form.
- Post-quantum encryption — **always on**.
- Certificate verification — **always on** (no "skip verification").

### 5. Настройки (Settings)
Flat sections, no boxes:
- **Основное**: Язык (English default), Тема (Система/Светлая/Тёмная),
  Запускать при входе, Подключаться автоматически.
- **Безопасность**: Kill switch (+ allowed ports). Post-quantum and certificate
  verification are always on, so no toggles for them.
- **Обслуживание**: Сетевые адаптеры (Windows) →, Проверить обновления.
- Footer: version + "ядро TrustTunnel".

### 6. Сетевые адаптеры (Windows, sub-screen)
- Back arrow + "Сканировать".
- Lists third-party virtual VPN adapters (TAP/WinTUN) that can conflict with our
  WinTUN; toggle to disable/enable. Our adapter is marked "наш" and not toggled.
- Windows-only (hidden/empty elsewhere).

### 7. Логи (Logs)
- Top: level filter, copy, clear.
- Color-coded monospace console (INFO/WARN/ERROR/DEBUG).
- Footer: **clickable log-file path** (reveal in Finder/Explorer/file manager) +
  auto-scroll toggle.

## Implementation notes

- QML frontend over the C++ core; expose a backend object to QML
  (`Q_PROPERTY` state/speeds/active-config/config-list, `Q_INVOKABLE`
  connect/disconnect/selectConfig/import…). Develop against a mock backend
  locally for fast iteration, then wire the real `QtTrustTunnelClient`.
- Config defaults baked in: `post_quantum_group_enabled = true`,
  `skip_verification = false`.
