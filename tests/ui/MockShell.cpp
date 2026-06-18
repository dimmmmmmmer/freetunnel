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
