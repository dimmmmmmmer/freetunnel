// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace freetunnel {

// Which of the three window buttons go on which side of the title bar.
struct ButtonLayout {
    QStringList left;
    QStringList right;
};

// A GNOME/GTK decoration layout — "appmenu:minimize,close", "close:", ":" — read
// into the buttons this window draws. Anything else in it (appmenu, icon, menu,
// spacer) is not a button we have and is dropped, and a name given twice is kept
// once, where it first appeared.
ButtonLayout parseButtonLayout(const QString &layout);

// What the desktop the app is running on expects of a window that draws its own
// title bar, for the QML that draws it.
//
// The window is frameless on Windows and Linux, which means it gives up everything
// the system would have done for a title bar it drew itself: which buttons exist
// and on which side, what they look like, what a double-, middle- or right-click
// on the bar does. On Windows QWindowKit hands most of that back natively. On
// Linux there is no such library that does not also swallow the presses, so the
// desktop is simply asked, through the settings portal, and followed — including
// when the user changes a setting while the app is open.
class DesktopChrome : public QObject {
    Q_OBJECT
    // "windows" | "pop" | "adwaita" | "native" (macOS: AppKit draws them).
    Q_PROPERTY(QString controlStyle READ controlStyle NOTIFY changed)
    Q_PROPERTY(QStringList controlsLeft READ controlsLeft NOTIFY changed)
    Q_PROPERTY(QStringList controlsRight READ controlsRight NOTIFY changed)
    // GNOME's GDesktopTitlebarAction names: "toggle-maximize", "minimize", "lower",
    // "menu", "none", and a few this window cannot do, which it treats as "none".
    Q_PROPERTY(QString doubleClickAction READ doubleClickAction NOTIFY changed)
    Q_PROPERTY(QString middleClickAction READ middleClickAction NOTIFY changed)
    Q_PROPERTY(QString rightClickAction READ rightClickAction NOTIFY changed)

public:
    explicit DesktopChrome(QObject *parent = nullptr);

    const QString &controlStyle() const { return m_style; }
    const QStringList &controlsLeft() const { return m_layout.left; }
    const QStringList &controlsRight() const { return m_layout.right; }
    const QString &doubleClickAction() const { return m_doubleClick; }
    const QString &middleClickAction() const { return m_middleClick; }
    const QString &rightClickAction() const { return m_rightClick; }

    // Ask the window manager for its own window menu (Move, Resize, Always on Top,
    // Close...) at the pointer, as a right-click on a real title bar would. Linux on
    // X11 only, and only where the window manager says it understands the request;
    // false everywhere else, so the caller can decide what to do instead.
    Q_INVOKABLE bool showWindowMenu(QObject *window);

    // For tests: apply a setting as if the portal had just reported it.
    void applySetting(const QString &ns, const QString &key, const QVariant &value);

signals:
    void changed();

private:
    QString m_style;
    ButtonLayout m_layout;
    QString m_doubleClick = QStringLiteral("toggle-maximize");
    QString m_middleClick = QStringLiteral("none");
    QString m_rightClick = QStringLiteral("menu");
};

} // namespace freetunnel
