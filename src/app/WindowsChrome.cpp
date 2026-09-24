// cppcheck-suppress-file missingIncludeSystem
#include "app/WindowsChrome.h"

#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>

#include <QWKQuick/quickwindowagent.h>

namespace freetunnel {

namespace {

// Hands the window's darkChrome property to the agent as its "dark-mode", now and
// whenever it changes, so the thin Windows 11 border and the system menu follow
// FreeTunnel's own light/dark switch rather than the OS setting.
class DarkModeFollower : public QObject {
    Q_OBJECT
public:
    DarkModeFollower(QWK::QuickWindowAgent *agent, QQuickWindow *window)
        : QObject(agent), m_agent(agent), m_window(window) {}

public slots:
    void apply()
    {
        m_agent->setWindowAttribute(QStringLiteral("dark-mode"),
                                    m_window->property("darkChrome").toBool());
    }

private:
    QWK::QuickWindowAgent *m_agent;
    QQuickWindow *m_window;
};

} // namespace

void setupWindowsChrome(QQuickWindow *window)
{
    if (!window)
        return;

    // Parented to the window: it lives exactly as long as the thing it manages.
    auto *agent = new QWK::QuickWindowAgent(window);
    agent->setup(window);

    // The QML names these items for exactly this. Missing ones are skipped rather
    // than fatal: a window without its agent-registered buttons is still a window.
    const auto item = [window](const char *name) {
        return window->findChild<QQuickItem *>(QLatin1String(name));
    };
    if (QQuickItem *bar = item("titleBar"))
        agent->setTitleBar(bar);
    // Registered as the system's own buttons: that is what makes the agent answer
    // HTMAXBUTTON over maximise, which is the whole of what Windows 11 needs to
    // show Snap Layouts on hover.
    if (QQuickItem *button = item("windowMinButton"))
        agent->setSystemButton(QWK::WindowAgentBase::Minimize, button);
    if (QQuickItem *button = item("windowMaxButton"))
        agent->setSystemButton(QWK::WindowAgentBase::Maximize, button);
    if (QQuickItem *button = item("windowCloseRect"))
        agent->setSystemButton(QWK::WindowAgentBase::Close, button);
    // The nav tiles reach down into the title band. Without this they would be
    // part of the title bar, and clicking one would start a window drag.
    if (QQuickItem *nav = item("navRow"))
        agent->setHitTestVisible(nav, true);

    auto *dark = new DarkModeFollower(agent, window);
    dark->apply();
    const QMetaObject *meta = window->metaObject();
    const int darkIndex = meta->indexOfProperty("darkChrome");
    if (darkIndex >= 0) {
        const QMetaProperty property = meta->property(darkIndex);
        const int slot = dark->metaObject()->indexOfSlot("apply()");
        if (property.hasNotifySignal() && slot >= 0)
            QObject::connect(window, property.notifySignal(), dark, dark->metaObject()->method(slot));
    }

    // Only now: QWindowKit changes how Qt computes the window's size, so a size
    // constraint applied before setup() is computed the old way and comes out wrong
    // (its README, and issue #76, where the window came out 31 px too tall).
    window->setMinimumSize(QSize(400, 460));
    window->show();
}

} // namespace freetunnel

#include "WindowsChrome.moc"
