// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QKeySequence>

#include <QHotkey>

void Backend::unregisterHotkeys() {
    delete m_hkToggle;     m_hkToggle = nullptr;
    delete m_hkConnect;    m_hkConnect = nullptr;
    delete m_hkDisconnect; m_hkDisconnect = nullptr;
}

void Backend::registerHotkeys() {
    unregisterHotkeys();
    if (!m_settings.hotkeys_enabled) // master switch off — leave everything unbound
        return;
    auto make = [this](const QString &seq, void (Backend::*slot)()) -> QHotkey * {
        const QString s = seq.trimmed();
        if (s.isEmpty())
            return nullptr;
        QKeySequence ks(s);
        if (ks.isEmpty())
            return nullptr;
        auto *hk = new QHotkey(ks, true /*autoRegister*/, this);
        connect(hk, &QHotkey::activated, this, slot);
        return hk;
    };
    m_hkToggle = make(m_settings.hotkey_toggle, &Backend::toggle);
    m_hkConnect = make(m_settings.hotkey_connect, &Backend::connectVpn);
    m_hkDisconnect = make(m_settings.hotkey_disconnect, &Backend::disconnectVpn);
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
