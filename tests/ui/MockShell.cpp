// cppcheck-suppress-file missingIncludeSystem
#include "ui/MockShell.h"

MockShell::MockShell(QObject *parent) : QObject(parent) {}

void MockShell::setCurrentPage(int v)
{
    if (m_currentPage == v)
        return;
    m_currentPage = v;
    emit currentPageChanged();
}

void MockShell::setOverlay(const QString &v)
{
    if (m_overlay == v)
        return;
    m_overlay = v;
    emit overlayChanged();
}

void MockShell::setEditIndex(int v)
{
    if (m_editIndex == v)
        return;
    m_editIndex = v;
    emit editIndexChanged();
}

void MockShell::setWindowPopupOpen(bool v)
{
    if (m_windowPopupOpen == v)
        return;
    m_windowPopupOpen = v;
    emit windowPopupOpenChanged();
}

QString MockShell::keyName(int key, const QString &text) const
{
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        return QString(QChar(static_cast<char16_t>(key)));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    switch (key) {
    case Qt::Key_Space:
        return QStringLiteral("Space");
    case Qt::Key_Tab:
        return QStringLiteral("Tab");
    case Qt::Key_Return:
        return QStringLiteral("Return");
    case Qt::Key_Enter:
        return QStringLiteral("Enter");
    case Qt::Key_Home:
        return QStringLiteral("Home");
    case Qt::Key_End:
        return QStringLiteral("End");
    case Qt::Key_Insert:
        return QStringLiteral("Ins");
    case Qt::Key_PageUp:
        return QStringLiteral("PgUp");
    case Qt::Key_PageDown:
        return QStringLiteral("PgDown");
    case Qt::Key_Up:
        return QStringLiteral("Up");
    case Qt::Key_Down:
        return QStringLiteral("Down");
    case Qt::Key_Left:
        return QStringLiteral("Left");
    case Qt::Key_Right:
        return QStringLiteral("Right");
    default:
        break;
    }
    // ASCII printable only — a non-Latin layout reports a Cyrillic char here.
    if (text.size() == 1) {
        const char16_t cc = text.at(0).unicode();
        if (cc >= 33 && cc < 127)
            return text.toUpper();
    }
    return QString();
}

void MockShell::showToast(const QString &msg)
{
    m_lastToast = msg;
}

QString MockShell::elide(const QString &s, int n) const
{
    return s.length() > n ? s.left(n - 1) + QStringLiteral("…") : s;
}

QString MockShell::elideMiddle(const QString &s, int n) const
{
    if (s.length() <= static_cast<int>(n))
        return s;
    if (n <= 1)
        return QStringLiteral("…");
    const int keep = n - 1;
    const int head = (keep + 1) / 2;
    const int tail = keep / 2;
    return s.left(head) + QStringLiteral("…") + s.right(tail);
}
