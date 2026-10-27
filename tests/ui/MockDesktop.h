// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

// Stands in for DesktopChrome in the QML tests: the same properties, set by the
// test rather than read from a desktop, so a layout can be put in front of the
// window and what it draws checked against it.
class MockDesktop : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString controlStyle MEMBER m_style NOTIFY changed)
    Q_PROPERTY(QStringList controlsLeft MEMBER m_left NOTIFY changed)
    Q_PROPERTY(QStringList controlsRight MEMBER m_right NOTIFY changed)
    Q_PROPERTY(QString doubleClickAction MEMBER m_doubleClick NOTIFY changed)
    Q_PROPERTY(QString middleClickAction MEMBER m_middleClick NOTIFY changed)
    Q_PROPERTY(QString rightClickAction MEMBER m_rightClick NOTIFY changed)
    Q_PROPERTY(Qt::ColorScheme colorScheme MEMBER m_colorScheme NOTIFY changed)

public:
    using QObject::QObject;

    void setLayout(const QStringList &left, const QStringList &right)
    {
        m_left = left;
        m_right = right;
        emit changed();
    }
    void setControlStyle(const QString &style)
    {
        m_style = style;
        emit changed();
    }

    Q_INVOKABLE bool showWindowMenu(QObject *) { ++menuRequests; return false; }
    Q_INVOKABLE void bringToFront(QObject *) { ++bringRequests; }
    Q_INVOKABLE void minimize(QObject *) { ++minimizeRequests; }
    Q_INVOKABLE void hideToTray(QObject *) { ++hideRequests; }

    // The tray, as the QML asks about it. Unless a test says otherwise there is
    // whatever the tray icon itself reports, which is what the real thing answers,
    // so a test that does not care runs as it always did: offscreen has no tray.
    Q_INVOKABLE bool hasTray(QObject *trayIcon) const
    {
        return m_hasTray.value_or(trayIcon && trayIcon->property("available").toBool());
    }
    Q_INVOKABLE bool trayCanHoldWindow(QObject *trayIcon) const
    {
        return hasTray(trayIcon) && m_trayHostRegistered;
    }
    // Whether a minimise can be checked: an X server, as against native Wayland.
    // Yes unless a test says otherwise, which is what the shipped builds run on.
    Q_INVOKABLE bool minimizeCanBeChecked() const { return m_minimizeCanBeChecked; }

    // A desktop with a tray (and a host for it, or not), the way a test puts one in
    // front of the window; useRealTray() puts the machine's own back.
    void useTray(bool hasTray, bool hostRegistered = true)
    {
        m_hasTray = hasTray;
        m_trayHostRegistered = hostRegistered;
    }
    void useRealTray()
    {
        m_hasTray.reset();
        m_trayHostRegistered = true;
    }
    void setMinimizeCanBeChecked(bool can) { m_minimizeCanBeChecked = can; }

    int menuRequests = 0;
    int bringRequests = 0;
    int minimizeRequests = 0;
    int hideRequests = 0;

signals:
    void changed();
    void trayHostAppeared();
    // What the real one says a moment after minimize(); a test emits it.
    void minimizeSettled(QObject *window, bool honoured);

private:
    // The defaults the window had before any desktop said otherwise.
    QString m_style = QStringLiteral("adwaita");
    QStringList m_left;
    QStringList m_right{QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")};
    QString m_doubleClick = QStringLiteral("toggle-maximize");
    QString m_middleClick = QStringLiteral("none");
    QString m_rightClick = QStringLiteral("menu");
    Qt::ColorScheme m_colorScheme = Qt::ColorScheme::Unknown;
    std::optional<bool> m_hasTray;
    bool m_trayHostRegistered = true;
    bool m_minimizeCanBeChecked = true;
};
