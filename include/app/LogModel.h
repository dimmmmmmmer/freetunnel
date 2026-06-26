// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// Append-only list model for the log view. Backing a virtualized ListView with a
// proper model (incremental begin/endInsertRows) keeps each new line O(1) on the
// UI and — unlike re-handing QML a fresh QVariantList every update — never resets
// the view, so the user's scroll position is preserved when auto-scroll is off.
class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { TimeRole = Qt::UserRole + 1, LevelRole, MsgRole };

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    void append(const QString &time, const QString &level, const QString &msg);
    void clear();
    QString toPlainText() const; // whole log as plain text (for Copy)

signals:
    void countChanged();

private:
    struct Row {
        QString time;
        QString level;
        QString msg;
    };
    QVector<Row> m_rows;
    static constexpr int kMaxRows = 500;
};
