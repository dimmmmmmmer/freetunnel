// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QGuiApplication>
#include <QKeySequence>
#include <QTimer>

#include <QHotkey>

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
    auto make = [this](const QString &seq, const QString &label, void (Backend::*slot)()) -> QHotkey * {
        const QString s = seq.trimmed();
        if (s.isEmpty())
            return nullptr;
        QKeySequence ks(s);
        if (ks.isEmpty())
            return nullptr;
        auto *hk = new QHotkey(ks, true /*autoRegister*/, this);
        connect(hk, &QHotkey::activated, this, slot);
        if (!hk->isRegistered()) {
            appendLog(QStringLiteral("INFO"),
                      tr("Hotkey “%1” (%2) could not be registered — it may be in use by another app.")
                              .arg(s, label));
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
