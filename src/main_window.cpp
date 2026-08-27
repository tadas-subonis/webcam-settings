#include "main_window.h"

#include "control_row.h"
#include "profile_store.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoFrameFormat>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <utility>

namespace {
constexpr auto kSettingsOrganization = "Tadas";
constexpr auto kSettingsApplication = "WebcamSettings";

QString suggestedProfilePath(const QString &deviceName)
{
    QString safeName = deviceName;
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]")), QStringLiteral("_"));
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents + QLatin1Char('/') + safeName.trimmed() + QStringLiteral(".webcam.json");
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), backend_(createPlatformBackend())
{
    buildUi();

    reapplyTimer_ = new QTimer(this);
    reapplyTimer_->setInterval(5000);
    connect(reapplyTimer_, &QTimer::timeout, this, [this] { reapplyAll(); });

    connect(&mediaDevices_, &QMediaDevices::videoInputsChanged, this, [this] { refreshDevices(); });
    refreshDevices();
    restoreLastCamera();
}

MainWindow::~MainWindow()
{
    if (camera_) {
        camera_->stop();
    }
    if (backend_) {
        backend_->close();
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Webcam Settings"));
    resize(1120, 720);
    setMinimumSize(760, 500);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    auto *toolbar = new QHBoxLayout;
    deviceCombo_ = new QComboBox(central);
    deviceCombo_->setMinimumWidth(280);
    refreshButton_ = new QPushButton(tr("Refresh"), central);
    saveButton_ = new QPushButton(tr("Save profile"), central);
    loadButton_ = new QPushButton(tr("Load profile"), central);
    reapplyButton_ = new QPushButton(tr("Reapply now"), central);
    autoReapplyBox_ = new QCheckBox(tr("Reapply every 5 seconds"), central);

    toolbar->addWidget(new QLabel(tr("Camera:"), central));
    toolbar->addWidget(deviceCombo_, 1);
    toolbar->addWidget(refreshButton_);
    toolbar->addSpacing(8);
    toolbar->addWidget(saveButton_);
    toolbar->addWidget(loadButton_);
    toolbar->addWidget(reapplyButton_);
    toolbar->addWidget(autoReapplyBox_);
    root->addLayout(toolbar);

    auto *usageRow = new QHBoxLayout;
    cameraUsersCombo_ = new QComboBox(central);
    cameraUsersCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cameraUsersCombo_->setMinimumContentsLength(42);
    refreshUsersButton_ = new QPushButton(tr("Refresh use"), central);
    terminateUserButton_ = new QPushButton(tr("End process…"), central);
    usageRow->addWidget(new QLabel(tr("Camera use:"), central));
    usageRow->addWidget(cameraUsersCombo_, 1);
    usageRow->addWidget(refreshUsersButton_);
    usageRow->addWidget(terminateUserButton_);
    root->addLayout(usageRow);

    splitter_ = new QSplitter(Qt::Horizontal, central);
    videoLabel_ = new QLabel(tr("Starting camera preview…"), splitter_);
    videoLabel_->setMinimumSize(420, 300);
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLabel_->setStyleSheet(QStringLiteral(
        "background: #111318; color: #aeb4bf; border-radius: 8px; padding: 8px;"));
    videoSink_ = new QVideoSink(this);
    connect(videoSink_, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame &frame) { displayFrame(frame); });

    controlsContainer_ = new QWidget(splitter_);
    controlsLayout_ = new QVBoxLayout(controlsContainer_);
    controlsLayout_->setContentsMargins(12, 8, 12, 8);
    controlsLayout_->setSpacing(2);
    emptyLabel_ = new QLabel(tr("Select a camera to discover its controls."), controlsContainer_);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    controlsLayout_->addWidget(emptyLabel_);
    controlsLayout_->addStretch(1);

    controlsScroll_ = new QScrollArea(splitter_);
    controlsScroll_->setMinimumWidth(440);
    controlsScroll_->setWidgetResizable(true);
    controlsScroll_->setFrameShape(QFrame::NoFrame);
    controlsScroll_->setWidget(controlsContainer_);
    splitter_->addWidget(videoLabel_);
    splitter_->addWidget(controlsScroll_);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 2);
    splitter_->setSizes({660, 460});
    root->addWidget(splitter_, 1);

    statusLabel_ = new QLabel(central);
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);
    setCentralWidget(central);

    captureSession_.setVideoSink(videoSink_);

    connect(deviceCombo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (!refreshing_) {
            selectCamera(index);
        }
    });
    connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(saveButton_, &QPushButton::clicked, this, [this] { saveProfile(); });
    connect(loadButton_, &QPushButton::clicked, this, [this] { loadProfile(); });
    connect(reapplyButton_, &QPushButton::clicked, this, [this] { reapplyAll(); });
    connect(refreshUsersButton_, &QPushButton::clicked, this, [this] { refreshCameraUsers(); });
    connect(terminateUserButton_, &QPushButton::clicked, this,
            [this] { terminateSelectedCameraUser(); });
    connect(cameraUsersCombo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
        terminateUserButton_->setEnabled(index >= 0 && index < cameraUsers_.size()
                                         && cameraUsers_.at(index).pid > 0);
    });
    connect(autoReapplyBox_, &QCheckBox::toggled, this, [this](const bool enabled) {
        if (enabled) {
            reapplyTimer_->start();
        } else {
            reapplyTimer_->stop();
        }
    });

    saveButton_->setEnabled(false);
    loadButton_->setEnabled(false);
    reapplyButton_->setEnabled(false);
    autoReapplyBox_->setEnabled(false);
    terminateUserButton_->setEnabled(false);
}

void MainWindow::refreshDevices()
{
    const QByteArray selectedId = deviceCombo_->currentData().toByteArray();
    refreshing_ = true;
    deviceCombo_->clear();

    const auto devices = QMediaDevices::videoInputs();
    for (const auto &device : devices) {
        deviceCombo_->addItem(device.description(), device.id());
    }

    int selectedIndex = -1;
    for (int i = 0; i < deviceCombo_->count(); ++i) {
        if (deviceCombo_->itemData(i).toByteArray() == selectedId) {
            selectedIndex = i;
            break;
        }
    }
    if (selectedIndex < 0 && !devices.isEmpty()) {
        selectedIndex = 0;
    }
    deviceCombo_->setCurrentIndex(selectedIndex);
    refreshing_ = false;

    if (selectedIndex >= 0) {
        selectCamera(selectedIndex);
    } else {
        selectCamera(-1);
        setStatus(tr("No video cameras were found."), true);
    }
}

void MainWindow::selectCamera(const int index)
{
    clearControls();
    if (camera_) {
        camera_->stop();
        captureSession_.setCamera(nullptr);
        delete camera_;
        camera_ = nullptr;
    }
    backend_->close();
    selectedDeviceId_.clear();
    selectedDeviceName_.clear();
    cameraUsers_.clear();
    cameraUsersCombo_->clear();
    terminateUserButton_->setEnabled(false);

    const auto devices = QMediaDevices::videoInputs();
    if (index < 0 || index >= devices.size()) {
        saveButton_->setEnabled(false);
        loadButton_->setEnabled(false);
        reapplyButton_->setEnabled(false);
        autoReapplyBox_->setEnabled(false);
        refreshUsersButton_->setEnabled(false);
        return;
    }

    const auto device = devices.at(index);
    selectedDeviceId_ = device.id();
    selectedDeviceName_ = device.description();
    refreshUsersButton_->setEnabled(true);
    refreshCameraUsers();
    camera_ = new QCamera(device, this);
    const auto formats = device.videoFormats();
    if (!formats.isEmpty()) {
        // A settings preview values reliability over capture resolution. 720p30
        // avoids expensive hardware transforms selected by some Windows drivers.
        const QSize target(1280, 720);
        const auto best = std::min_element(formats.cbegin(), formats.cend(), [&target](const auto &left, const auto &right) {
            const auto score = [&target](const QCameraFormat &format) {
                const QSize resolution = format.resolution();
                const qint64 areaDifference = std::abs(
                    static_cast<qint64>(resolution.width()) * resolution.height()
                    - static_cast<qint64>(target.width()) * target.height());
                const qint64 oversizedPenalty =
                    (resolution.width() > target.width() || resolution.height() > target.height()) ? 10'000'000 : 0;
                const qint64 lowFrameRatePenalty = format.maxFrameRate() < 29.0 ? 5'000'000 : 0;
                const qint64 frameRateDifference = static_cast<qint64>(
                    std::abs(format.maxFrameRate() - 30.0) * 100'000.0);
                const qint64 uncompressedPenalty =
                    format.pixelFormat() == QVideoFrameFormat::Format_Jpeg ? 0 : 20'000'000;
                return areaDifference + oversizedPenalty + lowFrameRatePenalty
                    + frameRateDifference + uncompressedPenalty;
            };
            return score(left) < score(right);
        });
        camera_->setCameraFormat(*best);
    }
    captureSession_.setCamera(camera_);
    connect(camera_, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &description) {
                if (description.contains(QStringLiteral("lack of hardware resources"),
                                         Qt::CaseInsensitive)) {
                    videoLabel_->setText(tr(
                        "Preview unavailable because another application is using the camera.\n"
                        "You can still change and save controls here."));
                    setStatus(tr("Camera preview is busy in another application; controls remain available."),
                              true);
                    refreshCameraUsers();
                } else {
                    videoLabel_->setText(tr("Camera preview unavailable."));
                    setStatus(description, true);
                }
            });
    QString error;
    if (!backend_->open(device.id(), device.description(), &error)) {
        camera_->start();
        setStatus(tr("Preview started, but camera controls could not be opened: %1").arg(error), true);
        return;
    }

    QSettings settings(QString::fromLatin1(kSettingsOrganization), QString::fromLatin1(kSettingsApplication));
    settings.setValue(QStringLiteral("lastDeviceId"), device.id());
    rebuildControls();
    camera_->start();
}

void MainWindow::refreshCameraUsers()
{
    cameraUsers_.clear();
    cameraUsersCombo_->clear();
    terminateUserButton_->setEnabled(false);
    if (selectedDeviceId_.isEmpty()) {
        cameraUsersCombo_->addItem(tr("Select a camera first"));
        return;
    }

    QString error;
    cameraUsers_ = CameraUsageInspector::users(selectedDeviceId_, &error);
    for (const CameraUser &user : std::as_const(cameraUsers_)) {
        const QString label = user.executable.isEmpty()
            ? tr("%1 (PID %2)").arg(user.name).arg(user.pid)
            : tr("%1 (PID %2) — %3").arg(user.name).arg(user.pid).arg(user.executable);
        cameraUsersCombo_->addItem(label);
        cameraUsersCombo_->setItemData(cameraUsersCombo_->count() - 1, user.detail, Qt::ToolTipRole);
    }
    if (cameraUsers_.isEmpty()) {
        cameraUsersCombo_->addItem(error.isEmpty()
                                       ? tr("No other active camera process detected")
                                       : error);
        cameraUsersCombo_->setToolTip(error);
    } else {
        cameraUsersCombo_->setToolTip(cameraUsers_.first().detail);
        terminateUserButton_->setEnabled(cameraUsers_.first().pid > 0);
    }
}

void MainWindow::terminateSelectedCameraUser()
{
    const int index = cameraUsersCombo_->currentIndex();
    if (index < 0 || index >= cameraUsers_.size()) {
        return;
    }
    const CameraUser user = cameraUsers_.at(index);
    const auto answer = QMessageBox::warning(
        this, tr("End camera process?"),
        tr("End %1 (PID %2)?\n\nUnsaved work in that application may be lost. The process will be terminated immediately.")
            .arg(user.name).arg(user.pid),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!CameraUsageInspector::terminate(user.pid, &error)) {
        QMessageBox::warning(this, tr("Could not end process"), error);
        refreshCameraUsers();
        return;
    }
    setStatus(tr("Ended %1 (PID %2). Retrying camera preview…").arg(user.name).arg(user.pid));
    QTimer::singleShot(750, this, [this] {
        refreshCameraUsers();
        if (camera_) {
            camera_->stop();
            camera_->start();
        }
    });
}

void MainWindow::rebuildControls()
{
    clearControls();
    QString error;
    const auto controls = backend_->controls(&error);
    if (!error.isEmpty()) {
        setStatus(error, true);
    }

    emptyLabel_->setVisible(controls.isEmpty());
    if (controls.isEmpty()) {
        emptyLabel_->setText(tr("This camera exposes no compatible controls."));
    }

    for (const auto &control : controls) {
        auto *row = new ControlRow(control, controlsContainer_);
        row->setChangeHandler([this](const CameraControlValue &value) { applyValue(value); });
        controlsLayout_->insertWidget(controlsLayout_->count() - 1, row);
        controlRows_.push_back(row);
    }
    splitter_->setSizes({660, 460});

    const bool enabled = !controls.isEmpty();
    saveButton_->setEnabled(enabled);
    loadButton_->setEnabled(enabled);
    reapplyButton_->setEnabled(enabled);
    autoReapplyBox_->setEnabled(enabled);
    if (enabled) {
        setStatus(tr("%1 controls available. Changes are applied directly to the camera.")
                      .arg(controls.size()));
    }
}

void MainWindow::clearControls()
{
    for (auto *row : std::as_const(controlRows_)) {
        controlsLayout_->removeWidget(row);
        delete row;
    }
    controlRows_.clear();
    emptyLabel_->setVisible(true);
}

void MainWindow::applyValue(const CameraControlValue &value, const bool reportFailure)
{
    QString error;
    if (!backend_->isOpen() && !backend_->open(selectedDeviceId_, selectedDeviceName_, &error)) {
        if (reportFailure) {
            setStatus(error, true);
        }
        return;
    }
    CameraControlValue accepted;
    if (!backend_->setControl(value, &accepted, &error)) {
        if (reportFailure) {
            setStatus(error, true);
        }
        return;
    }
    const auto row = std::find_if(controlRows_.cbegin(), controlRows_.cend(), [&accepted](const auto *item) {
        return item->currentValue().id == accepted.id;
    });
    QString name = accepted.id;
    if (row != controlRows_.cend()) {
        name = (*row)->controlName();
        (*row)->setCurrentValue(accepted.value, accepted.automatic, false);
    }
    setStatus(tr("Applied %1: %2%3")
                  .arg(name)
                  .arg(accepted.value)
                  .arg(accepted.automatic ? tr(" (auto)") : QString()));
}

void MainWindow::reapplyAll()
{
    QString error;
    if (!backend_->isOpen() && !backend_->open(selectedDeviceId_, selectedDeviceName_, &error)) {
        setStatus(error, true);
        return;
    }
    int failures = 0;
    for (const auto &value : currentValues()) {
        CameraControlValue accepted;
        if (!backend_->setControl(value, &accepted, &error)) {
            ++failures;
        }
    }
    setStatus(failures == 0
                  ? tr("All camera settings reapplied.")
                  : tr("Reapply completed with %1 failed controls.").arg(failures),
              failures != 0);
}

void MainWindow::saveProfile()
{
    if (deviceCombo_->currentIndex() < 0) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save webcam profile"), suggestedProfilePath(deviceCombo_->currentText()),
        tr("Webcam profiles (*.webcam.json);;JSON files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!ProfileStore::save(path, deviceCombo_->currentData().toByteArray(),
                            deviceCombo_->currentText(), currentValues(), &error)) {
        QMessageBox::warning(this, tr("Could not save profile"), error);
        return;
    }
    setStatus(tr("Profile saved to %1").arg(path));
}

void MainWindow::loadProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load webcam profile"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Webcam profiles (*.webcam.json);;JSON files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QVector<CameraControlValue> values;
    QString profileDevice;
    QString error;
    if (!ProfileStore::load(path, &values, &profileDevice, &error)) {
        QMessageBox::warning(this, tr("Could not load profile"), error);
        return;
    }

    if (!profileDevice.isEmpty() && profileDevice != deviceCombo_->currentText()) {
        const auto answer = QMessageBox::question(
            this, tr("Profile is for another camera"),
            tr("This profile was saved for “%1”, but “%2” is selected. Apply it anyway?")
                .arg(profileDevice, deviceCombo_->currentText()));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    int applied = 0;
    for (const auto &value : values) {
        const auto it = std::find_if(controlRows_.begin(), controlRows_.end(), [&value](ControlRow *row) {
            return row->currentValue().id == value.id;
        });
        if (it != controlRows_.end()) {
            (*it)->setCurrentValue(value.value, value.automatic, true);
            ++applied;
        }
    }
    setStatus(tr("Loaded %1 settings from %2").arg(applied).arg(path));
}

void MainWindow::restoreLastCamera()
{
    QSettings settings(QString::fromLatin1(kSettingsOrganization), QString::fromLatin1(kSettingsApplication));
    const QByteArray lastId = settings.value(QStringLiteral("lastDeviceId")).toByteArray();
    if (lastId.isEmpty()) {
        return;
    }
    for (int i = 0; i < deviceCombo_->count(); ++i) {
        if (deviceCombo_->itemData(i).toByteArray() == lastId) {
            deviceCombo_->setCurrentIndex(i);
            return;
        }
    }
}

void MainWindow::setStatus(const QString &message, const bool error)
{
    statusLabel_->setText(message);
    statusLabel_->setStyleSheet(error ? QStringLiteral("color: #d95555;")
                                      : QStringLiteral("color: palette(mid);"));
}

QVector<CameraControlValue> MainWindow::currentValues() const
{
    QVector<CameraControlValue> values;
    values.reserve(controlRows_.size());
    for (const auto *row : controlRows_) {
        if (row->canPersist()) values.push_back(row->currentValue());
    }
    return values;
}

void MainWindow::displayFrame(const QVideoFrame &frame)
{
    const QImage image = frame.toImage();
    if (image.isNull()) {
        return;
    }
    const QSize target = videoLabel_->contentsRect().size();
    videoLabel_->setPixmap(QPixmap::fromImage(image).scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
