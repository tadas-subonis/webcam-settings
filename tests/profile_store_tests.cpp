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
            {CameraControlId::Zoom, 175, false},
            {CameraControlId::Focus, 560, true},
        };

        QString error;
        QVERIFY2(ProfileStore::save(path, QByteArray("device-id"), QStringLiteral("Test Camera"),
                                    source, &error), qPrintable(error));

        QVector<CameraControlValue> loaded;
        QString deviceName;
        QVERIFY2(ProfileStore::load(path, &loaded, &deviceName, &error), qPrintable(error));
        QCOMPARE(deviceName, QStringLiteral("Test Camera"));
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).id, CameraControlId::Zoom);
        QCOMPARE(loaded.at(0).value, 175);
        QCOMPARE(loaded.at(0).automatic, false);
        QCOMPARE(loaded.at(1).id, CameraControlId::Focus);
        QCOMPARE(loaded.at(1).automatic, true);
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
            CameraControlId::Zoom, QStringLiteral("Zoom"), 100, 400, 25, 100, 100, false, false,
        };
        QCOMPARE(control.clamped(87), 100);
        QCOMPARE(control.clamped(174), 150);
        QCOMPARE(control.clamped(401), 400);
    }
};

QTEST_APPLESS_MAIN(ProfileStoreTests)
#include "profile_store_tests.moc"

