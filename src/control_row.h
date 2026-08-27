#pragma once

#include "camera_control.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

class ControlRow final : public QWidget {
public:
    using ChangeHandler = std::function<void(const CameraControlValue &)>;

    explicit ControlRow(const CameraControl &control, QWidget *parent = nullptr);

    [[nodiscard]] CameraControlValue currentValue() const;
    void setCurrentValue(int value, bool automatic, bool notify);
    void setChangeHandler(ChangeHandler handler);

private:
    void notifyChanged();
    void updateEnabledState();

    CameraControl control_;
    QLabel *nameLabel_{};
    QSlider *slider_{};
    QSpinBox *spinBox_{};
    QCheckBox *autoBox_{};
    QPushButton *resetButton_{};
    ChangeHandler changeHandler_;
    bool updating_{false};
};

