#pragma once

#include "camera_control.h"
#include "camera_usage.h"
#include "control_backend.h"

#include <QMainWindow>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVector>

#include <memory>

class QCamera;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSplitter;
class QTimer;
class QVBoxLayout;
class QVideoFrame;
class QVideoSink;
class ControlRow;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void buildUi();
    void refreshDevices();
    void selectCamera(int index);
    void rebuildControls();
    void clearControls();
    void applyValue(const CameraControlValue &value, bool reportFailure = true);
    void reapplyAll();
    void saveProfile();
    void loadProfile();
    void refreshCameraUsers();
    void terminateSelectedCameraUser();
    void restoreLastCamera();
    void setStatus(const QString &message, bool error = false);
    [[nodiscard]] QVector<CameraControlValue> currentValues() const;

    QMediaDevices mediaDevices_;
    QMediaCaptureSession captureSession_;
    QCamera *camera_{};
    std::unique_ptr<CameraControlBackend> backend_;

    QComboBox *deviceCombo_{};
    QPushButton *refreshButton_{};
    QPushButton *saveButton_{};
    QPushButton *loadButton_{};
    QPushButton *reapplyButton_{};
    QComboBox *cameraUsersCombo_{};
    QPushButton *refreshUsersButton_{};
    QPushButton *terminateUserButton_{};
    QCheckBox *autoReapplyBox_{};
    QLabel *videoLabel_{};
    QVideoSink *videoSink_{};
    QScrollArea *controlsScroll_{};
    QSplitter *splitter_{};
    QWidget *controlsContainer_{};
    QVBoxLayout *controlsLayout_{};
    QLabel *statusLabel_{};
    QLabel *emptyLabel_{};
    QTimer *reapplyTimer_{};
    QVector<ControlRow *> controlRows_;
    QVector<CameraUser> cameraUsers_;
    QByteArray selectedDeviceId_;
    QString selectedDeviceName_;
    bool refreshing_{false};

    void displayFrame(const QVideoFrame &frame);
};
