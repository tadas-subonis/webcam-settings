#include "profile_store.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ProfileStoreTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsProfile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("camera.webcam.json"));
        const QVector<CameraControlValue> source{
            {QStringLiteral("zoom"), 175, false},
            {QStringLiteral("vendor:tracking-mode"), 3, false},
        };

        QString error;
        QVERIFY2(ProfileStore::save(path, QByteArray("device-id"), QStringLiteral("Test Camera"),
                                    source, &error), qPrintable(error));

        QVector<CameraControlValue> loaded;
        QString deviceName;
        QVERIFY2(ProfileStore::load(path, &loaded, &deviceName, &error), qPrintable(error));
        QCOMPARE(deviceName, QStringLiteral("Test Camera"));
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).id, QStringLiteral("zoom"));
        QCOMPARE(loaded.at(0).value, 175);
        QCOMPARE(loaded.at(0).automatic, false);
        QCOMPARE(loaded.at(1).id, QStringLiteral("vendor:tracking-mode"));
    }

    void rejectsMalformedProfile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("broken.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not json");
        file.close();

        QVector<CameraControlValue> loaded;
        QString error;
        QVERIFY(!ProfileStore::load(path, &loaded, nullptr, &error));
        QVERIFY(!error.isEmpty());
    }

    void normalizesControlValues()
    {
        const CameraControl control{
            QStringLiteral("zoom"), QStringLiteral("Zoom"), QStringLiteral("Camera"),
            CameraControlType::Integer, 100, 400, 25, 100, 100, {}, false, false, false,
        };
        QCOMPARE(control.clamped(87), 100);
        QCOMPARE(control.clamped(174), 150);
        QCOMPARE(control.clamped(401), 400);

        const CameraControl toggle{
            QStringLiteral("vendor:tracking"), QStringLiteral("Tracking"), QStringLiteral("Vendor"),
            CameraControlType::Boolean, 0, 1, 1, 0, 0,
        };
        QCOMPARE(toggle.clamped(-10), 1);
        QCOMPARE(toggle.displayValue(0), QStringLiteral("off"));

        const CameraControl menu{
            QStringLiteral("vendor:mode"), QStringLiteral("Mode"), QStringLiteral("Vendor"),
            CameraControlType::Menu, 0, 10, 1, 2, 2,
            {{2, QStringLiteral("Wide")}, {7, QStringLiteral("Portrait")}},
        };
        QCOMPARE(menu.clamped(7), 7);
        QCOMPARE(menu.clamped(5), 2);
        QCOMPARE(menu.displayValue(7), QStringLiteral("Portrait"));
    }
};

QTEST_APPLESS_MAIN(ProfileStoreTests)
#include "profile_store_tests.moc"
