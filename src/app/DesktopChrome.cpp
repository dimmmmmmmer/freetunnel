// cppcheck-suppress-file missingIncludeSystem
#include "app/DesktopChrome.h"

#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QWindow>

#include <memory>

#ifdef Q_OS_LINUX
#include "DesktopChromeLinux.h"
#endif
#ifdef Q_OS_MACOS
#include "DesktopChromeMac.h"
#endif

namespace freetunnel {

namespace {

const QStringList &drawnButtons()
{
    static const QStringList buttons{QStringLiteral("minimize"), QStringLiteral("maximize"),
                                     QStringLiteral("close")};
    return buttons;
}

// The look the window starts with, and keeps unless the desktop says otherwise.
// On Linux that is libadwaita's until the portal answers, so where no portal
// answers nothing changes for anyone.
QString initialStyle()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("native"); // AppKit draws the traffic lights
#else
    return QStringLiteral("adwaita");
#endif
}

// Whether this changed the field.
bool assign(QString &field, const QString &next)
{
    if (field == next)
        return false;
    field = next;
    return true;
}

// org.freedesktop.appearance's color-scheme: 1 prefers dark, 2 prefers light; 0
// is "no preference", and anything else is a value this was not written for.
// Neither is a reason to guess.
Qt::ColorScheme colorSchemeFrom(const QVariant &value)
{
    bool ok = false;
    const uint scheme = value.toUInt(&ok);
    if (ok && scheme == 1)
        return Qt::ColorScheme::Dark;
    if (ok && scheme == 2)
        return Qt::ColorScheme::Light;
    return Qt::ColorScheme::Unknown;
}

QStringList buttonsIn(const QString &side, const QStringList &alreadyPlaced)
{
    QStringList out;
    for (const QString &raw : side.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString name = raw.trimmed();
        if (drawnButtons().contains(name) && !out.contains(name) && !alreadyPlaced.contains(name))
            out << name;
    }
    return out;
}

} // namespace

ButtonLayout parseButtonLayout(const QString &layout)
{
    // GTK's own rule: what is before the colon goes on the left, what is after it
    // on the right, and a layout with no colon at all is entirely the left side.
    const int colon = layout.indexOf(QLatin1Char(':'));
    const QString left = colon < 0 ? layout : layout.left(colon);
    const QString right = colon < 0 ? QString() : layout.mid(colon + 1);
    ButtonLayout out;
    out.left = buttonsIn(left, {});
    out.right = buttonsIn(right, out.left);
    return out;
}

DesktopChrome::DesktopChrome(QObject *parent)
    : QObject(parent)
    , m_style(initialStyle())
{
#ifndef Q_OS_MACOS
    m_layout.right = drawnButtons();
#endif
#ifdef Q_OS_LINUX
    m_iconified = x11WindowIconified;
    m_trayHost = [] { return trayHostRegistered(); };
#endif
}

void DesktopChrome::followDesktop()
{
#ifdef Q_OS_LINUX
    watchPortalSettings(this);
#endif
}

bool DesktopChrome::applyWindowManagerSetting(const QString &key, const QString &text)
{
    if (key == QLatin1String("button-layout")) {
        const ButtonLayout next = parseButtonLayout(text);
        if (next.left == m_layout.left && next.right == m_layout.right)
            return false;
        m_layout = next;
        return true;
    }
    if (key == QLatin1String("action-double-click-titlebar"))
        return assign(m_doubleClick, text);
    if (key == QLatin1String("action-middle-click-titlebar"))
        return assign(m_middleClick, text);
    if (key == QLatin1String("action-right-click-titlebar"))
        return assign(m_rightClick, text);
    return false;
}

void DesktopChrome::applySetting(const QString &ns, const QString &key, const QVariant &value)
{
    bool moved = false;
    if (ns == QLatin1String("org.gnome.desktop.wm.preferences")) {
        moved = applyWindowManagerSetting(key, value.toString());
    } else if (ns == QLatin1String("org.freedesktop.appearance") && key == QLatin1String("color-scheme")) {
        const Qt::ColorScheme next = colorSchemeFrom(value);
        moved = next != m_colorScheme;
        m_colorScheme = next;
    } else if (ns == QLatin1String("org.gnome.desktop.interface") && key == QLatin1String("gtk-theme")) {
        // Only the two looks this window can draw. Pop draws a filled close button
        // and bare circles for the others; every other GNOME-family theme gets
        // libadwaita's, which is also the closest this has to KDE's.
        if (m_style == QLatin1String("pop") || m_style == QLatin1String("adwaita"))
            moved = assign(m_style, value.toString().startsWith(QLatin1String("Pop"), Qt::CaseInsensitive)
                                            ? QStringLiteral("pop")
                                            : QStringLiteral("adwaita"));
    }
    if (moved)
        emit changed();
}

namespace {

// The bring-forward a window is on, counted per window: a request that finds a
// newer one on the window has been overtaken, and its check or its wait ends.
constexpr const char *kBringGeneration = "freetunnelBringGeneration";

quint64 bringGenerationOf(const QWindow *window)
{
    return window->property(kBringGeneration).toULongLong();
}

void settled(const std::function<void(bool)> &done, bool remapped)
{
    if (done)
        done(remapped);
}

// What bringWindowForward() has always done, once.
void bringWindowForwardOnce(QWindow *window)
{
#ifdef Q_OS_MACOS
    unhideMacApplication();
#endif
    if (window->windowStates() & Qt::WindowMinimized)
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
    // setVisible, not show(): show() is showNormal() here, and a window minimised
    // from maximised would come back no longer maximised.
    window->setVisible(true);
    window->raise();
#ifdef Q_OS_LINUX
    if (activateX11Window(window))
        return;
#endif
    window->requestActivate();
}

// Wait for the window manager to have withdrawn the window (asked now, and then
// every pollMs until the deadline), and then map it again. A window manager that
// cannot be asked is not waited for. Ends without a word when the window is gone,
// and with done(false) when a newer request has taken the window over.
void remapWhenWithdrawn(const QPointer<QWindow> &window, quint64 request,
                        const WithdrawnProbe &withdrawn, int pollMs, QDeadlineTimer deadline,
                        const std::function<void(bool)> &done)
{
    if (!window)
        return;
    if (bringGenerationOf(window) != request) {
        settled(done, false);
        return;
    }
    const std::optional<bool> gone = withdrawn ? withdrawn(window) : std::nullopt;
    if (gone.has_value() && !*gone && !deadline.hasExpired()) {
        QTimer::singleShot(pollMs, window.data(), [window, request, withdrawn, pollMs, deadline, done]() {
            remapWhenWithdrawn(window, request, withdrawn, pollMs, deadline, done);
        });
        return;
    }
    bringWindowForwardOnce(window);
    settled(done, true);
}

} // namespace

void bringWindowForward(QWindow *window)
{
    if (!window)
        return;
#ifdef Q_OS_LINUX
    // On an X server, XWayland included, the window manager may take the request
    // and do nothing with it; see bringWindowForwardChecked(). Native Wayland and
    // everywhere else, the request is all there is.
    if (runsOnX11()) {
        bringWindowForwardChecked(window, {}, x11WindowWithdrawn, kActivateCheckMs);
        return;
    }
#endif
    bringWindowForwardOnce(window);
}

void bringWindowForwardChecked(QWindow *window, const ActiveProbe &active,
                               const WithdrawnProbe &withdrawn, int checkMs,
                               std::function<void(bool remapped)> done, int pollMs, int waitMs)
{
    if (!window)
        return;
    const quint64 request = bringGenerationOf(window) + 1;
    window->setProperty(kBringGeneration, request);
    if (!window->isVisible()) {
        bringWindowForwardOnce(window); // mapped, and a new map is what gets the focus
        settled(done, false);
        return;
    }
    // Whether the window was active at any moment from now on: the focus may come
    // and go again before the look, and a window the user has since moved on from is
    // not one the request failed for. Watched from before the request is made.
    auto wasActive = std::make_shared<bool>(false);
    const QMetaObject::Connection watch =
            QObject::connect(window, &QWindow::activeChanged, window, [wasActive, window]() {
                if (window->isActive())
                    *wasActive = true;
            });
    bringWindowForwardOnce(window);
    // The window is the timer's context, so a window destroyed in the meantime takes
    // the check with it; the guard is for one that is only hidden or deleted later.
    QPointer<QWindow> guard(window);
    QTimer::singleShot(checkMs, window,
                       [guard, request, watch, wasActive, active, withdrawn, pollMs, waitMs,
                        done = std::move(done)]() {
        QObject::disconnect(watch);
        if (!guard)
            return;
        // In the order of what each costs; the probe, which may ask the server, last.
        const bool overtaken = bringGenerationOf(guard) != request;
        const bool away = !guard->isVisible(); // hidden since, by the X: leave it
        if (overtaken || away || *wasActive || (active ? active(guard) : guard->isActive())) {
            settled(done, false);
            return;
        }
        // hide() sends the window manager an UnmapNotify, and it withdraws the window.
        guard->hide();
        remapWhenWithdrawn(guard, request, withdrawn, pollMs, QDeadlineTimer(waitMs), done);
    });
}

void minimizeWindow(QWindow *window)
{
    if (window)
        window->setWindowStates(window->windowStates() | Qt::WindowMinimized);
}

void minimizeWindowChecked(QWindow *window, const IconifiedProbe &probe, int checkMs,
                           std::function<void(bool honoured)> done)
{
    if (!window)
        return;
    minimizeWindow(window);
    // The window is the timer's context, so a window destroyed in the meantime takes
    // the check with it; the guard is for one that is only hidden or deleted later.
    QPointer<QWindow> guard(window);
    QTimer::singleShot(checkMs, window, [guard, probe, done = std::move(done)]() {
        if (!guard)
            return;
        if (!(guard->windowStates() & Qt::WindowMinimized)) {
            done(true); // restored since: nothing stuck, nothing to put right
            return;
        }
        const std::optional<bool> iconified = probe ? probe(guard) : std::nullopt;
        if (!iconified.has_value() || *iconified) {
            done(true);
            return;
        }
        guard->setWindowStates(guard->windowStates() & ~Qt::WindowMinimized);
        done(false);
    });
}

void hideWindowToTray(QWindow *window)
{
    if (!window)
        return;
#ifdef Q_OS_MACOS
    // The Space is AppKit's, so the end of leaving it is asked of AppKit: Qt says
    // the state changed as soon as it has asked for the change. Only on the real
    // platform — elsewhere, as under offscreen, a window id is not an NSView.
    if ((window->windowStates() & Qt::WindowFullScreen)
        && QGuiApplication::platformName() == QLatin1String("cocoa")) {
        QPointer<QWindow> guard(window);
        auto hidden = std::make_shared<bool>(false);
        const auto hideOnce = [guard, hidden]() {
            if (*hidden || !guard)
                return;
            *hidden = true;
            guard->hide();
        };
        whenMacFullScreenEnds(window, hideOnce);
        QTimer::singleShot(2000, window, hideOnce); // for an exit that never reports
        window->setWindowStates(window->windowStates() & ~Qt::WindowFullScreen);
        return;
    }
#endif
    window->hide();
}

void DesktopChrome::bringToFront(QObject *window)
{
    bringWindowForward(qobject_cast<QWindow *>(window));
}

void DesktopChrome::minimize(QObject *window)
{
    auto *target = qobject_cast<QWindow *>(window);
    if (!target)
        return;
    if (!m_iconified) {
        // Nothing to ask (Windows, macOS): the system does what it was asked.
        minimizeWindow(target);
        emit minimizeSettled(target, true);
        return;
    }
    // On the display server this window can be asked about, minimising is a request
    // to be checked on; see minimizeWindowChecked().
    QPointer<DesktopChrome> self(this);
    minimizeWindowChecked(target, m_iconified, kMinimizeCheckMs,
                          [self, guard = QPointer<QWindow>(target)](bool honoured) {
                              if (self && guard)
                                  emit self->minimizeSettled(guard, honoured);
                          });
}

bool DesktopChrome::hasTray(QObject *trayIcon) const
{
    return trayIcon && trayIcon->property("available").toBool();
}

bool DesktopChrome::trayCanHoldWindow(QObject *trayIcon) const
{
    if (!hasTray(trayIcon))
        return false;
    return !m_trayHost || m_trayHost().value_or(true);
}

bool DesktopChrome::minimizeCanBeChecked() const
{
#ifdef Q_OS_LINUX
    return runsOnX11();
#else
    return false;
#endif
}

void DesktopChrome::hideToTray(QObject *window)
{
    hideWindowToTray(qobject_cast<QWindow *>(window));
}

bool DesktopChrome::showWindowMenu(QObject *window)
{
#ifdef Q_OS_LINUX
    return showX11WindowMenu(qobject_cast<QWindow *>(window));
#else
    Q_UNUSED(window)
    return false;
#endif
}

} // namespace freetunnel
