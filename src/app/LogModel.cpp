// cppcheck-suppress-file missingIncludeSystem
#include "app/LogModel.h"

#include <QStringList>

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent) {}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case TimeRole:
        return r.time;
    case LevelRole:
        return r.level;
    case MsgRole:
        return r.msg;
    default:
        return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        {TimeRole, QByteArrayLiteral("time")},
        {LevelRole, QByteArrayLiteral("level")},
        {MsgRole, QByteArrayLiteral("msg")},
    };
}

void LogModel::append(const QString &time, const QString &level, const QString &msg)
{
    // Cap the in-memory view; drop the oldest line first so indices stay sane.
    if (m_rows.size() >= kMaxRows) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_rows.removeFirst();
        endRemoveRows();
    }
    const int row = static_cast<int>(m_rows.size());
    beginInsertRows(QModelIndex(), row, row);
    m_rows.push_back(Row{time, level, msg});
    endInsertRows();
    emit countChanged();
}

void LogModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

QString LogModel::toPlainText() const
{
    QStringList out;
    out.reserve(static_cast<int>(m_rows.size()));
    for (const Row &r : m_rows)
        out << r.time + QLatin1Char(' ') + r.level + QLatin1Char(' ') + r.msg;
    return out.join(QLatin1Char('\n'));
}
