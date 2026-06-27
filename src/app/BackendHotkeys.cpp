// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QGuiApplication>
#include <QHash>
#include <QKeySequence>
#include <QTimer>

#include <QHotkey>

// Physical key position → Latin letter. nativeScanCode is layout-independent, so
// the same physical key yields the same shortcut regardless of the active layout.
// macOS: kVK_* virtual key code. Windows: set-1 scan code. X11/xcb: evdev keycode.
QString Backend::physicalLetterForScanCode(quint32 nativeScanCode) const
{
    static const QHash<quint32, char> kMap = {
#if defined(Q_OS_MACOS)
        {0, 'A'},  {11, 'B'}, {8, 'C'},  {2, 'D'},  {14, 'E'}, {3, 'F'},  {5, 'G'},
        {4, 'H'},  {34, 'I'}, {38, 'J'}, {40, 'K'}, {37, 'L'}, {46, 'M'}, {45, 'N'},
        {31, 'O'}, {35, 'P'}, {12, 'Q'}, {15, 'R'}, {1, 'S'},  {17, 'T'}, {32, 'U'},
        {9, 'V'},  {13, 'W'}, {7, 'X'},  {16, 'Y'}, {6, 'Z'},
#elif defined(Q_OS_WIN)
        {0x10, 'Q'}, {0x11, 'W'}, {0x12, 'E'}, {0x13, 'R'}, {0x14, 'T'}, {0x15, 'Y'},
        {0x16, 'U'}, {0x17, 'I'}, {0x18, 'O'}, {0x19, 'P'}, {0x1E, 'A'}, {0x1F, 'S'},
        {0x20, 'D'}, {0x21, 'F'}, {0x22, 'G'}, {0x23, 'H'}, {0x24, 'J'}, {0x25, 'K'},
        {0x26, 'L'}, {0x2C, 'Z'}, {0x2D, 'X'}, {0x2E, 'C'}, {0x2F, 'V'}, {0x30, 'B'},
        {0x31, 'N'}, {0x32, 'M'},
#else // X11 / xcb evdev keycodes
        {38, 'A'}, {56, 'B'}, {54, 'C'}, {40, 'D'}, {26, 'E'}, {41, 'F'}, {42, 'G'},
        {43, 'H'}, {31, 'I'}, {44, 'J'}, {45, 'K'}, {46, 'L'}, {58, 'M'}, {57, 'N'},
        {32, 'O'}, {33, 'P'}, {24, 'Q'}, {27, 'R'}, {39, 'S'}, {28, 'T'}, {30, 'U'},
        {55, 'V'}, {25, 'W'}, {53, 'X'}, {29, 'Y'}, {52, 'Z'},
#endif
    };
    const auto it = kMap.constFind(nativeScanCode);
    return it == kMap.cend() ? QString() : QString(QChar::fromLatin1(it.value()));
}

bool Backend::hotkeysSupported() const {
    // Global hotkeys go through X11 grabs; a Wayland compositor never delivers
    // them to an X11 grab (even via XWayland). Report the feature as unavailable
    // so it can be disabled in the UI instead of silently doing nothing.
    return !QGuiApplication::platformName().contains(QLatin1String("wayland"), Qt::CaseInsensitive);
}

void Backend::unregisterHotkeys() {
    delete m_hkToggle;     m_hkToggle = nullptr;
    delete m_hkConnect;    m_hkConnect = nullptr;
    delete m_hkDisconnect; m_hkDisconnect = nullptr;
}

void Backend::registerHotkeys() {
    unregisterHotkeys();
#ifdef FT_ENABLE_TEST_HOOKS
    // QHotkey needs a real windowing session; offscreen CI (QT_QPA_PLATFORM=offscreen) segfaults.
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen"))
        return;
#endif
    if (!m_settings.hotkeys_enabled) // master switch off — leave everything unbound
        return;
    // Under Wayland the compositor owns global shortcuts and never forwards them
    // to an X11 grab (even via XWayland), so registration would silently "succeed"
    // yet nothing fires. Don't bind anything; the UI also disables the feature.
    if (!hotkeysSupported()) {
        if (!m_waylandHotkeyWarned) {
            m_waylandHotkeyWarned = true;
            appendLog(QStringLiteral("WARN"),
                      tr("Global hotkeys are not supported under Wayland. Log in to an "
                         "X11/Xorg session (or run with QT_QPA_PLATFORM=xcb) to use them."));
        }
        return;
    }
    const QString platform = QGuiApplication::platformName();
    auto make = [this, &platform](const QString &seq, const QString &label,
                                  void (Backend::*slot)()) -> QHotkey * {
        const QString s = seq.trimmed();
        if (s.isEmpty())
            return nullptr;
        QKeySequence ks(s);
        if (ks.isEmpty()) {
            appendLog(QStringLiteral("WARN"),
                      tr("Hotkey “%1” (%2) is not a valid key sequence — ignored.").arg(s, label));
            return nullptr;
        }
        auto *hk = new QHotkey(ks, true /*autoRegister*/, this);
        connect(hk, &QHotkey::activated, this, slot);
        if (hk->isRegistered()) {
            appendLog(QStringLiteral("INFO"),
                      tr("Hotkey “%1” (%2) registered [%3].").arg(s, label, platform));
        } else {
            appendLog(QStringLiteral("WARN"),
                      tr("Hotkey “%1” (%2) could not be registered [%3] — it may be in use "
                         "by another app or the desktop.").arg(s, label, platform));
        }
        return hk;
    };
    m_hkToggle = make(m_settings.hotkey_toggle, tr("Toggle VPN"), &Backend::toggle);
    m_hkConnect = make(m_settings.hotkey_connect, tr("Connect"), &Backend::connectVpn);
    m_hkDisconnect = make(m_settings.hotkey_disconnect, tr("Disconnect"), &Backend::disconnectVpn);
}

void Backend::ensureHotkeysRegistered()
{
    if (!m_settings.hotkeys_enabled)
        return;
    const auto ok = [](QHotkey *hk, const QString &seq) {
        return seq.trimmed().isEmpty() || (hk && hk->isRegistered());
    };
    if (ok(m_hkToggle, m_settings.hotkey_toggle) && ok(m_hkConnect, m_settings.hotkey_connect)
        && ok(m_hkDisconnect, m_settings.hotkey_disconnect))
        return;
    registerHotkeys();
}

void Backend::wireHotkeyLifecycle()
{
    QTimer::singleShot(0, this, [this] { registerHotkeys(); });
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
            if (s == Qt::ApplicationActive)
                ensureHotkeysRegistered();
        });
    }
}

void Backend::setHotkeysEnabled(bool v) {
    if (m_settings.hotkeys_enabled == v) return;
    m_settings.hotkeys_enabled = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}
void Backend::setHotkeyToggle(const QString &v) {
    if (m_settings.hotkey_toggle == v) return;
    m_settings.hotkey_toggle = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}
void Backend::setHotkeyConnect(const QString &v) {
    if (m_settings.hotkey_connect == v) return;
    m_settings.hotkey_connect = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}
void Backend::setHotkeyDisconnect(const QString &v) {
    if (m_settings.hotkey_disconnect == v) return;
    m_settings.hotkey_disconnect = v;
    persistSettings();
    registerHotkeys();
    emit hotkeysChanged();
}
