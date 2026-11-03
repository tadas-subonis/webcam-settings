#include "windows_camera_rotation.h"
#include "windows_camera_rotation_p.h"

#include <QtTest>
#include <QUuid>

class WindowsCameraRotationTests final : public QObject {
    Q_OBJECT
    HKEY key_{};
    QString path_;

private slots:
    void init()
    {
        path_ = QStringLiteral("Software\\WebcamSettingsTests\\%1")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QCOMPARE(RegCreateKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path_.utf16()),
                                 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key_, nullptr), ERROR_SUCCESS);
    }

    void cleanup()
    {
        if (key_) RegCloseKey(key_);
        key_ = nullptr;
        RegDeleteKeyW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path_.utf16()));
    }

    void missingOverrideMeansNoRotation()
    {
        qint64 degrees = -1;
        QCOMPARE(WindowsCameraRotation::detail::readKey(key_, &degrees), ERROR_SUCCESS);
        QCOMPARE(degrees, 0);
    }

    void persistsClockwiseOutputRotation_data()
    {
        QTest::addColumn<qint64>("degrees");
        QTest::addColumn<DWORD>("sensorOffset");
        QTest::newRow("none") << qint64(0) << DWORD(0);
        QTest::newRow("right") << qint64(90) << DWORD(270);
        QTest::newRow("upside-down") << qint64(180) << DWORD(180);
        QTest::newRow("left") << qint64(270) << DWORD(90);
    }

    void persistsClockwiseOutputRotation()
    {
        QFETCH(qint64, degrees);
        QFETCH(DWORD, sensorOffset);
        QCOMPARE(WindowsCameraRotation::detail::writeKey(key_, degrees), ERROR_SUCCESS);
        // Check the stored native offset independently: it is the opposite
        // direction from the rotation selected by the user.
        DWORD stored = 999;
        DWORD size = sizeof(stored);
        QCOMPARE(RegGetValueW(key_, nullptr, L"FSSensorOrientation", RRF_RT_REG_DWORD,
                              nullptr, &stored, &size), ERROR_SUCCESS);
        QCOMPARE(stored, sensorOffset);
        HKEY reopened = nullptr;
        QCOMPARE(RegOpenKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path_.utf16()),
                               0, KEY_QUERY_VALUE, &reopened), ERROR_SUCCESS);
        qint64 actual = -1;
        const auto result = WindowsCameraRotation::detail::readKey(reopened, &actual);
        RegCloseKey(reopened);
        QCOMPARE(result, ERROR_SUCCESS);
        QCOMPARE(actual, degrees);
    }

    void invalidAngleDoesNotReplaceSavedRotation()
    {
        QCOMPARE(WindowsCameraRotation::detail::writeKey(key_, 180), ERROR_SUCCESS);
        for (const qint64 degrees : {-90, 1, 45, 360}) {
            QCOMPARE(WindowsCameraRotation::detail::writeKey(key_, degrees), ERROR_INVALID_PARAMETER);
        }
        qint64 actual = -1;
        QCOMPARE(WindowsCameraRotation::detail::readKey(key_, &actual), ERROR_SUCCESS);
        QCOMPARE(actual, 180);
    }

    void wrongRegistryTypeFails()
    {
        const wchar_t bad[] = L"90";
        QCOMPARE(RegSetValueExW(key_, L"FSSensorOrientation", 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(bad), sizeof(bad)), ERROR_SUCCESS);
        qint64 actual = -1;
        QVERIFY(WindowsCameraRotation::detail::readKey(key_, &actual) != ERROR_SUCCESS);
        QCOMPARE(actual, -1);
    }

    void readOnlyKeyRejectsChangesAndPreservesRotation()
    {
        QCOMPARE(WindowsCameraRotation::detail::writeKey(key_, 180), ERROR_SUCCESS);
        HKEY readOnly = nullptr;
        QCOMPARE(RegOpenKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path_.utf16()),
                               0, KEY_QUERY_VALUE, &readOnly), ERROR_SUCCESS);
        const auto result = WindowsCameraRotation::detail::writeKey(readOnly, 90);
        RegCloseKey(readOnly);
        QCOMPARE(result, ERROR_ACCESS_DENIED);
        qint64 actual = -1;
        QCOMPARE(WindowsCameraRotation::detail::readKey(key_, &actual), ERROR_SUCCESS);
        QCOMPARE(actual, 180);
    }

    void invalidDeviceIsRejectedWithoutElevation()
    {
        QString error;
        QVERIFY(!WindowsCameraRotation::writeWithElevation("not-a-camera", 90, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(WindowsCameraRotationTests)
#include "windows_camera_rotation_tests.moc"
