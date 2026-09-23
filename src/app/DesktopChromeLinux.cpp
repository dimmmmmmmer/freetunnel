// cppcheck-suppress-file missingIncludeSystem
// Linux only: the settings portal (QtDBus) and the X11 window menu (xcb). Built
// only where those exist, so the rest of DesktopChrome — and everything that
// includes its header — needs neither.
#include "DesktopChromeLinux.h"

#include "app/DesktopChrome.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QPair>
#include <QSet>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>

#include <xcb/xcb.h>

#include <cstdlib>
#include <cstring>
#include <limits>

namespace freetunnel {

namespace {

const char *const kPortalService = "org.freedesktop.portal.Desktop";
const char *const kPortalPath = "/org/freedesktop/portal/desktop";
const char *const kPortalSettings = "org.freedesktop.portal.Settings";

// A portal value can arrive wrapped more than once — the deprecated Read method
// wraps it twice by specification — so unwrap until it is not a variant any more.
QVariant unwrapped(QVariant value)
{
    while (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value;
}

// The portal's SettingChanged signal can only be connected to a slot by name —
// QtDBus 6.8 has no functor overload — so the receiver is this small object
// rather than DesktopChrome, whose header then needs nothing from QtDBus.
class PortalListener : public QObject {
    Q_OBJECT
public:
    explicit PortalListener(DesktopChrome *desktop) : QObject(desktop), m_desktop(desktop) {}

public slots:
    void onSettingChanged(const QDBusMessage &message)
    {
        const QList<QVariant> args = message.arguments();
        if (args.size() < 3)
            return;
        m_desktop->applySetting(args.at(0).toString(), args.at(1).toString(), unwrapped(args.at(2)));
    }

private:
    DesktopChrome *m_desktop;
};

} // namespace

namespace {

// The portal's ReadAll answer, applied in the order it arrived, keeping the first
// value given for each setting. xdg-desktop-portal 1.14 asks every backend and
// puts their answers side by side, so on Pop!_OS each namespace comes back twice,
// once from the GNOME backend and once from the GTK one. The portal's own Read
// method answers with the first; so does this. A QMap would have kept the last.
void applyReadAll(DesktopChrome *desktop, const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return; // no portal, or none with Settings: keep what the window had
    const QVariant first = reply.arguments().constFirst();
    if (first.metaType() != QMetaType::fromType<QDBusArgument>())
        return;
    const auto all = first.value<QDBusArgument>();
    if (all.currentSignature() != QLatin1String("a{sa{sv}}"))
        return;
    QSet<QPair<QString, QString>> seen;
    all.beginMap();
    while (!all.atEnd()) {
        QString ns;
        QVariantMap values;
        all.beginMapEntry();
        all >> ns >> values;
        all.endMapEntry();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (seen.contains({ns, it.key()}))
                continue;
            seen.insert({ns, it.key()});
            desktop->applySetting(ns, it.key(), unwrapped(it.value()));
        }
    }
    all.endMap();
}

bool timedOut(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ErrorMessage)
        return false;
    const QDBusError::ErrorType type = QDBusError(reply).type();
    return type == QDBusError::NoReply || type == QDBusError::Timeout || type == QDBusError::TimedOut;
}

} // namespace

void watchPortalSettings(DesktopChrome *desktop)
{
    watchPortalSettings(desktop, QDBusConnection::sessionBus());
}

void watchPortalSettings(DesktopChrome *desktop, const QDBusConnection &bus)
{
    if (!bus.isConnected())
        return;
    QDBusConnection connection = bus;

    // Following first, reading second: a change that lands while the read is on
    // its way is then applied after it rather than lost. Moving the buttons to the
    // left in Tweaks, or switching to dark, should not need the app restarted.
    auto *listener = new PortalListener(desktop);
    connection.connect(QLatin1String(kPortalService), QLatin1String(kPortalPath),
                       QLatin1String(kPortalSettings), QStringLiteral("SettingChanged"), listener,
                       SLOT(onSettingChanged(QDBusMessage)));

    // ReadAll, not ReadOne: ReadOne arrived in xdg-desktop-portal 1.17.1, and the
    // 1.14 on a current Pop!_OS answers it with UnknownMethod.
    QDBusMessage call = QDBusMessage::createMethodCall(QLatin1String(kPortalService),
                                                       QLatin1String(kPortalPath),
                                                       QLatin1String(kPortalSettings),
                                                       QStringLiteral("ReadAll"));
    call << QStringList{QStringLiteral("org.gnome.desktop.wm.preferences"),
                        QStringLiteral("org.gnome.desktop.interface"),
                        QStringLiteral("org.freedesktop.appearance")};
    const QDBusMessage reply = connection.call(call, QDBus::Block, kPortalReadTimeoutMs);
    if (!timedOut(reply)) {
        applyReadAll(desktop, reply);
        return;
    }
    // A portal still starting up, or stuck. Not worth holding the window for any
    // longer, but its answer is still worth having whenever it comes — so no time
    // limit of our own this time. The default one, libdbus's 25 seconds, would give
    // up on a portal that a slow login is still starting, and it announces nothing
    // when it does come up: only later changes. INT_MAX is libdbus's "no limit";
    // the bus still fails the call if the portal never starts at all.
    auto *watcher = new QDBusPendingCallWatcher(
            connection.asyncCall(call, std::numeric_limits<int>::max()), desktop);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, desktop,
                     [desktop](QDBusPendingCallWatcher *w) {
                         w->deleteLater();
                         applyReadAll(desktop, w->reply());
                     });
}

namespace {

xcb_atom_t internAtom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
            c, xcb_intern_atom(c, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr);
    const xcb_atom_t atom = reply ? reply->atom : static_cast<xcb_atom_t>(XCB_ATOM_NONE);
    std::free(reply);
    return atom;
}

// Whether the running window manager lists this atom among the requests it
// supports. A window manager that does not — COSMIC's XWayland one, for instance —
// would simply drop the message, and the right-click would do nothing at all.
bool windowManagerSupports(xcb_connection_t *c, xcb_window_t root, xcb_atom_t atom)
{
    const xcb_atom_t supported = internAtom(c, "_NET_SUPPORTED");
    if (supported == XCB_ATOM_NONE)
        return false;
    xcb_get_property_reply_t *prop = xcb_get_property_reply(
            c, xcb_get_property(c, 0, root, supported, XCB_ATOM_ATOM, 0, 4096), nullptr);
    if (!prop)
        return false;
    const auto *atoms = static_cast<const xcb_atom_t *>(xcb_get_property_value(prop));
    const int count = xcb_get_property_value_length(prop) / static_cast<int>(sizeof(xcb_atom_t));
    bool found = false;
    for (int i = 0; i < count && !found; ++i)
        found = atoms[i] == atom;
    std::free(prop);
    return found;
}

// The same request GTK makes for its own client-side title bars: a
// _GTK_SHOW_WINDOW_MENU client message to the root window, at the pointer, after
// letting go of the pointer grab the press took. Mutter, KWin and xfwm4 all act
// on it; Qt 6.8's xcb plugin has no API of its own for it.
} // namespace

bool showX11WindowMenu(QWindow *window)
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !window)
        return false; // native Wayland, or not a GUI application
    xcb_connection_t *c = x11->connection();
    if (!c)
        return false;
    const auto win = static_cast<xcb_window_t>(window->winId());
    const xcb_atom_t menu = internAtom(c, "_GTK_SHOW_WINDOW_MENU");
    if (menu == XCB_ATOM_NONE)
        return false;
    // Root and pointer from the server, in X-native pixels, which is what the
    // window managers that honour this expect — not Qt's device-independent ones.
    xcb_query_pointer_reply_t *pointer =
            xcb_query_pointer_reply(c, xcb_query_pointer(c, win), nullptr);
    if (!pointer)
        return false;
    const xcb_window_t root = pointer->root;
    const int16_t rootX = pointer->root_x;
    const int16_t rootY = pointer->root_y;
    std::free(pointer);
    if (!windowManagerSupports(c, root, menu))
        return false;

    xcb_ungrab_pointer(c, XCB_CURRENT_TIME);
    xcb_client_message_event_t event{};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = win;
    event.type = menu;
    event.data.data32[0] = 0; // input device; Mutter and KWin ignore it
    event.data.data32[1] = static_cast<uint32_t>(rootX);
    event.data.data32[2] = static_cast<uint32_t>(rootY);
    xcb_send_event(c, 0, root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
    xcb_flush(c);
    return true;
}

} // namespace freetunnel

#include "DesktopChromeLinux.moc"
