#pragma once
#include <QObject>
#include <QString>

// Stand-in for the real VPN backend so the QML UI can be built/previewed
// without the C++ core. Exposes the same surface the QML expects.
class MockBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString sessionTime READ sessionTime NOTIFY changed)
    Q_PROPERTY(QString downSpeed READ downSpeed NOTIFY changed)
    Q_PROPERTY(QString upSpeed READ upSpeed NOTIFY changed)
    Q_PROPERTY(QString activeConfig READ activeConfig NOTIFY changed)
public:
    bool connected() const { return m_on; }
    QString sessionTime() const { return QStringLiteral("01:24:36"); }
    QString downSpeed() const { return QStringLiteral("12.4"); }
    QString upSpeed() const { return QStringLiteral("1.2"); }
    QString activeConfig() const { return QStringLiteral("Германия · Франкфурт"); }
    Q_INVOKABLE void toggle() { m_on = !m_on; emit changed(); }
signals:
    void changed();
private:
    bool m_on = true;
};
