#include "control_backend.h"

#include <QCameraDevice>
#include <QMediaDevices>
#include <QtTest>

#include <algorithm>

class BackendSmokeTests final : public QObject {
    Q_OBJECT

private slots:
    void discoversConnectedCameraControls()
    {
        if (qEnvironmentVariableIsSet("CI")) {
            QSKIP("Physical-camera smoke testing is skipped on hosted CI runners.");
        }
        const auto devices = QMediaDevices::videoInputs();
        if (devices.isEmpty()) {
            QSKIP("No camera is connected on this test machine.");
        }

        auto selected = devices.cbegin();
        const auto ugreen = std::find_if(devices.cbegin(), devices.cend(), [](const QCameraDevice &device) {
            return device.description().contains(QStringLiteral("UGREEN"), Qt::CaseInsensitive);
        });
        if (ugreen != devices.cend()) {
            selected = ugreen;
        }

        auto backend = createPlatformBackend();
        QString error;
        QVERIFY2(backend->open(selected->id(), selected->description(), &error), qPrintable(error));
        const auto controls = backend->controls(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(!controls.isEmpty(), "The selected camera exposed no compatible controls.");

        if (selected->description().contains(QStringLiteral("UGREEN"), Qt::CaseInsensitive)) {
            const auto zoom = std::find_if(controls.cbegin(), controls.cend(), [](const CameraControl &control) {
                return control.id == QStringLiteral("zoom");
            });
            QVERIFY2(zoom != controls.cend(), "The UGREEN camera did not expose its expected zoom control.");
            QVERIFY(zoom->minimum <= zoom->value);
            QVERIFY(zoom->value <= zoom->maximum);
            QVERIFY(zoom->maximum > zoom->minimum);
            CameraControlValue accepted;
            QVERIFY2(backend->setControl({zoom->id, zoom->value, zoom->automatic},
                                         &accepted, &error), qPrintable(error));
            QCOMPARE(accepted.id, zoom->id);
            QCOMPARE(accepted.value, zoom->value);
        }
    }
};

QTEST_MAIN(BackendSmokeTests)
#include "backend_smoke_tests.moc"
