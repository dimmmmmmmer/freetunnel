// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

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

    int menuRequests = 0;
    int bringRequests = 0;

signals:
    void changed();

private:
    // The defaults the window had before any desktop said otherwise.
    QString m_style = QStringLiteral("adwaita");
    QStringList m_left;
    QStringList m_right{QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")};
    QString m_doubleClick = QStringLiteral("toggle-maximize");
    QString m_middleClick = QStringLiteral("none");
    QString m_rightClick = QStringLiteral("menu");
    Qt::ColorScheme m_colorScheme = Qt::ColorScheme::Unknown;
};
