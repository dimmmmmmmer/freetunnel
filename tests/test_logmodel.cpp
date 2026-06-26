// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QAbstractItemModel>
#include <QSignalSpy>

#include "app/LogModel.h"

class TestLogModel : public QObject {
    Q_OBJECT

private slots:
    void appendAddsRowsAndRoles();
    void capDropsOldest();
    void clearEmpties();
    void plainTextJoinsRows();
};

void TestLogModel::appendAddsRowsAndRoles()
{
    LogModel m;
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.count(), 0);

    QSignalSpy inserted(&m, &QAbstractItemModel::rowsInserted);
    m.append(QStringLiteral("12:00:00"), QStringLiteral("INFO"), QStringLiteral("hello"));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(inserted.count(), 1);

    const QModelIndex idx = m.index(0, 0);
    QCOMPARE(m.data(idx, LogModel::TimeRole).toString(), QStringLiteral("12:00:00"));
    QCOMPARE(m.data(idx, LogModel::LevelRole).toString(), QStringLiteral("INFO"));
    QCOMPARE(m.data(idx, LogModel::MsgRole).toString(), QStringLiteral("hello"));

    // Role names QML binds to.
    const QHash<int, QByteArray> roles = m.roleNames();
    QCOMPARE(roles.value(LogModel::TimeRole), QByteArrayLiteral("time"));
    QCOMPARE(roles.value(LogModel::LevelRole), QByteArrayLiteral("level"));
    QCOMPARE(roles.value(LogModel::MsgRole), QByteArrayLiteral("msg"));
}

void TestLogModel::capDropsOldest()
{
    LogModel m;
    for (int i = 0; i < 600; ++i)
        m.append(QString::number(i), QStringLiteral("INFO"), QStringLiteral("m"));
    // Capped at 500, oldest 100 dropped.
    QCOMPARE(m.rowCount(), 500);
    QCOMPARE(m.data(m.index(0, 0), LogModel::TimeRole).toString(), QStringLiteral("100"));
    QCOMPARE(m.data(m.index(499, 0), LogModel::TimeRole).toString(), QStringLiteral("599"));
}

void TestLogModel::clearEmpties()
{
    LogModel m;
    m.append(QStringLiteral("t"), QStringLiteral("INFO"), QStringLiteral("a"));
    m.append(QStringLiteral("t"), QStringLiteral("INFO"), QStringLiteral("b"));

    QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
    m.clear();
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(reset.count(), 1);
    QVERIFY(m.toPlainText().isEmpty());
}

void TestLogModel::plainTextJoinsRows()
{
    LogModel m;
    m.append(QStringLiteral("12:00:00"), QStringLiteral("INFO"), QStringLiteral("one"));
    m.append(QStringLiteral("12:00:01"), QStringLiteral("CORE"), QStringLiteral("two"));
    QCOMPARE(m.toPlainText(), QStringLiteral("12:00:00 INFO one\n12:00:01 CORE two"));
}

QTEST_MAIN(TestLogModel)
#include "test_logmodel.moc"
