#include "camera_usage.h"

#include <QCoreApplication>
#include <QtTest>

class CameraUsageTests final : public QObject {
    Q_OBJECT
private slots:
    void rejectsInvalidProcessIds_data()
    {
        QTest::addColumn<qint64>("pid");
        QTest::newRow("zero") << qint64(0);
        QTest::newRow("negative") << qint64(-1);
        QTest::newRow("self") << QCoreApplication::applicationPid();
        // If narrowed to 32 bits, this becomes INT_MAX, never a process group.
        // Do not test values that could wrap to zero or -1 and signal groups.
        QTest::newRow("overflow") << qint64(0x17fffffffLL);
    }

    void rejectsInvalidProcessIds()
    {
        QFETCH(qint64, pid);
        QString error;
        QVERIFY(!CameraUsageInspector::terminate(pid, &error));
        QVERIFY2(error.contains(QStringLiteral("invalid or current-process PID")), qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(CameraUsageTests)
#include "camera_usage_tests.moc"
