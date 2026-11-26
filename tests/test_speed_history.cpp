// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "app/SpeedHistory.h"

class TestSpeedHistory : public QObject {
    Q_OBJECT

private slots:
    void keepsTheLastMinuteOldestFirst()
    {
        SpeedHistory h;
        for (int i = 0; i < SpeedHistory::CAPACITY + 5; ++i)
            h.add(i, -i);
        QCOMPARE(h.size(), SpeedHistory::CAPACITY);
        QCOMPARE(h.down(0), 5.0);
        QCOMPARE(h.down(SpeedHistory::CAPACITY - 1), double(SpeedHistory::CAPACITY + 4));
        // A negative speed is 0.
        QCOMPARE(h.up(3), 0.0);
    }

    void theScaleIsTheHighestSpeedAndNeverBelowTheFloor()
    {
        SpeedHistory h;
        QCOMPARE(h.scale(), SpeedHistory::FLOOR);
        h.add(100, 200);
        QCOMPARE(h.scale(), SpeedHistory::FLOOR);
        h.add(10, 2000000);
        h.add(1000000, 0);
        QCOMPARE(h.scale(), 2000000.0);
    }

    void clearStartsAfresh()
    {
        SpeedHistory h;
        h.add(1, 1);
        h.clear();
        QCOMPARE(h.size(), 0);
        h.add(7, 8);
        QCOMPARE(h.down(0), 7.0);
        QCOMPARE(h.up(0), 8.0);
    }
};

QTEST_APPLESS_MAIN(TestSpeedHistory)
#include "test_speed_history.moc"
