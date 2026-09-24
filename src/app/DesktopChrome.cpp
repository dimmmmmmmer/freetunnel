// cppcheck-suppress-file missingIncludeSystem
#include "app/DesktopChrome.h"

#include <QWindow>

#ifdef Q_OS_LINUX
#include "DesktopChromeLinux.h"
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
