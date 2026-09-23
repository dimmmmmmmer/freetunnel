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

void DesktopChrome::applySetting(const QString &ns, const QString &key, const QVariant &value)
{
    const QString text = value.toString();
    bool moved = false;
    auto set = [&moved](QString &field, const QString &next) {
        if (field != next) {
            field = next;
            moved = true;
        }
    };
    if (ns == QLatin1String("org.gnome.desktop.wm.preferences")) {
        if (key == QLatin1String("button-layout")) {
            const ButtonLayout next = parseButtonLayout(text);
            if (next.left != m_layout.left || next.right != m_layout.right) {
                m_layout = next;
                moved = true;
            }
        } else if (key == QLatin1String("action-double-click-titlebar")) {
            set(m_doubleClick, text);
        } else if (key == QLatin1String("action-middle-click-titlebar")) {
            set(m_middleClick, text);
        } else if (key == QLatin1String("action-right-click-titlebar")) {
            set(m_rightClick, text);
        }
    } else if (ns == QLatin1String("org.freedesktop.appearance") && key == QLatin1String("color-scheme")) {
        // 1 prefers dark, 2 prefers light; 0 is "no preference", and anything else
        // is a value this was not written for. Neither is a reason to guess.
        bool ok = false;
        const uint scheme = value.toUInt(&ok);
        const Qt::ColorScheme next = !ok ? Qt::ColorScheme::Unknown
                : scheme == 1 ? Qt::ColorScheme::Dark
                : scheme == 2 ? Qt::ColorScheme::Light
                              : Qt::ColorScheme::Unknown;
        if (next != m_colorScheme) {
            m_colorScheme = next;
            moved = true;
        }
    } else if (ns == QLatin1String("org.gnome.desktop.interface") && key == QLatin1String("gtk-theme")) {
        // Only the two looks this window can draw. Pop draws a filled close button
        // and bare circles for the others; every other GNOME-family theme gets
        // libadwaita's, which is also the closest this has to KDE's.
        if (m_style == QLatin1String("pop") || m_style == QLatin1String("adwaita"))
            set(m_style, text.startsWith(QLatin1String("Pop"), Qt::CaseInsensitive)
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
