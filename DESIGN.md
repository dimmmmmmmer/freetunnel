# FreeTunnel — UI Design Spec (locked)

Target: a modern, lightweight, "mobile-grade" desktop client built with **Qt Quick
(QML)** on the existing C++ TrustTunnel core. This document is the source of truth
for the QML implementation.

## Principles

- Flat, light, minimal. No heavy frames/boxes, no redundant dividers, no titles
  where the active nav already makes context obvious.
- **Monochrome gray** palette (light/dark aware) — no blue/coloured accents;
  semantic colours (green/amber/red) are used only for status. App mark = the
  **tunnel portal** (concentric gray arches).
- Everything adapts to light/dark mode. On macOS the title bar is unified
  (transparent) so the window background flows behind the traffic lights.

## Navigation

- **Top navigation bar, centered, icons only, no logo/title/version.**
- Tabs (5), in order: Подключение, Конфиги, Раздельный туннель, Настройки, Логи.
- Active tab highlighted (info background). Window grows downward; the navbar
  stays fixed at the top.
- The **Создать конфиг** sub-screen is **not** a tab — it uses a back arrow and
  no navbar (a centered card over a dimmed backdrop).

## i18n

- Qt `qsTr()` + `.ts` translation files (Qt Linguist), runtime language switch.
- **Default language: English.** Russian provided.

## Screens

### 1. Подключение (Home)
- Big **ring that is itself the connect/disconnect button**.
- State is shown by the ring only (no status text/pill):
  - Connected → solid blue ring; center: tunnel mark + session timer (`01:24:36`).
  - Connecting/reconnecting → animated blue arc.
  - Disconnected → thin gray ring; center: dimmed tunnel mark + "Выключено"; speed tiles hidden.
- Below the ring: **config name + ▾** (collapsed dropdown). Selecting another config:
  - if connected → switch & reconnect immediately;
  - if disconnected → just set it for next connect.
- Two speed tiles: Загрузка (download) / Отправка (upload), live (fed by the
  tunnel-stats wrapper patch).

### 2. Раздельный туннель (Split tunnel)
- Master toggle (on/off), labelled **Раздельное туннелирование**.
- Mode dropdown: "Указанное — мимо VPN" (general) / "Только указанное — в VPN"
  (selective).
- **Profiles**: named, switchable profiles (e.g. Default / Work). Chips with a
  `+` to add and `×` to delete; each profile owns its own rule set.
- **Rules**: chips list + "domain, IP or subnet" input. Rules are
  domain/IP/subnet based (the core has no per-app API, so per-app routing is
  intentionally not offered).

### 3. Конфиги (Configs)
- Centered, **frameless** actions above the list:
  - **＋** — opens a small slide-in menu: Вставить из буфера обмена / Из файла… /
    Создать новый…
  - **Speedometer** — ping all configs (latency shown per row).
- List rows: logo mark (dimmed unless active), name, latency, "connected" badge
  on the active+connected one, `⋯` (edit) and `✕` (delete, with confirmation).
  Clicking a row activates that config.

### 4. Создать / редактировать конфиг (sub-screen)
Form (back arrow, no navbar). Fields (client-side only):
- Имя (display name)
- Хост сервера (hostname)
- Адрес(а) — `host:port` (one or more)
- Логин / Пароль
- Протокол — HTTP/2 | HTTP/3
- DNS-серверы (DNS is **per-config**, not a global setting)
- Custom SNI
- Client random (hex)
- **Allow IPv6 connections** (toggle, right-aligned)
- Сертификат (PEM) — **load from file** and/or paste from clipboard
- Actions: Отмена / Сохранить

Not exposed (server-side / forced):
- MTU, anti-DPI — configured server-side, not in the client form.
- Post-quantum encryption — **always on**.
- Certificate verification — **always on** (no "skip verification").

### 5. Настройки (Settings)
Flat sections, no boxes. Row labels align with their section headers; values and
toggles sit at the right edge. Dropdowns slide in and anchor under the value.
- **Основное**: Язык (English default), Тема (Система/Светлая/Тёмная),
  Запускать при входе, Подключаться автоматически.
- **Безопасность**: Kill switch. Post-quantum and certificate verification are
  always on, so no toggles for them.
- **Исключённые маршруты**: IP/CIDR subnets that bypass the tunnel at the routing
  level (the core's `excluded_routes`) — chips + validated input. Distinct from
  the domain-based split-tunnel rules.
- **Горячие клавиши**: Toggle / Connect / Disconnect — each capturable and
  clearable (`×`); defaults Ctrl+Shift+T/E/D, shown with native glyphs on macOS.
- **Обслуживание**: Проверить обновления (shows "latest"/"download").
- Footer: clickable **FreeTunnel** (→ our repo) and **TrustTunnel core** (→
  upstream repo) with versions.

### 6. Логи (Logs)
- Top: **Clear** (hover button).
- Color-coded monospace console (INFO/WARN/ERROR), auto-trimmed on disk (5 MB).
- Footer: **clickable log-file path** (reveal in Finder/Explorer/file manager) +
  auto-scroll toggle.

## Implementation notes

- QML frontend over the C++ core; expose a backend object to QML
  (`Q_PROPERTY` state/speeds/active-config/config-list, `Q_INVOKABLE`
  connect/disconnect/selectConfig/import…). Develop against a mock backend
  locally for fast iteration (see `tools/qml_preview`), then wire the real
  `VpnHelperClient` (which drives the elevated helper running the core).
- Config defaults baked in: `post_quantum_group_enabled = true`,
  `skip_verification = false`.
