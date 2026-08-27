#include "control_row.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

ControlRow::ControlRow(const CameraControl &control, QWidget *parent)
    : QWidget(parent), control_(control)
{
    nameLabel_ = new QLabel(control.name, this);
    nameLabel_->setMinimumWidth(145);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(control.minimum, control.maximum);
    slider_->setSingleStep(control.normalizedStep());
    slider_->setPageStep(control.normalizedStep() * 5);
    slider_->setValue(control.value);

    spinBox_ = new QSpinBox(this);
    spinBox_->setRange(control.minimum, control.maximum);
    spinBox_->setSingleStep(control.normalizedStep());
    spinBox_->setValue(control.value);
    spinBox_->setMinimumWidth(88);

    autoBox_ = new QCheckBox(tr("Auto"), this);
    autoBox_->setVisible(control.supportsAuto);
    autoBox_->setChecked(control.automatic);

    resetButton_ = new QPushButton(tr("Reset"), this);
    resetButton_->setToolTip(tr("Restore the camera's advertised default for this control"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 3, 0, 3);
    layout->addWidget(nameLabel_);
    layout->addWidget(slider_, 1);
    layout->addWidget(spinBox_);
    layout->addWidget(autoBox_);
    layout->addWidget(resetButton_);

    connect(slider_, &QSlider::valueChanged, this, [this](const int value) {
        if (updating_) {
            return;
        }
        const int normalized = control_.clamped(value);
        updating_ = true;
        slider_->setValue(normalized);
        spinBox_->setValue(normalized);
        updating_ = false;
        notifyChanged();
    });
    connect(spinBox_, &QSpinBox::valueChanged, this, [this](const int value) {
        if (updating_) {
            return;
        }
        const int normalized = control_.clamped(value);
        updating_ = true;
        slider_->setValue(normalized);
        spinBox_->setValue(normalized);
        updating_ = false;
        notifyChanged();
    });
    connect(autoBox_, &QCheckBox::toggled, this, [this] {
        updateEnabledState();
        notifyChanged();
    });
    connect(resetButton_, &QPushButton::clicked, this, [this] {
        setCurrentValue(control_.defaultValue, false, true);
    });

    updateEnabledState();
}

CameraControlValue ControlRow::currentValue() const
{
    return {control_.id, spinBox_->value(), control_.supportsAuto && autoBox_->isChecked()};
}

void ControlRow::setCurrentValue(const int value, const bool automatic, const bool notify)
{
    updating_ = true;
    const int normalized = control_.clamped(value);
    slider_->setValue(normalized);
    spinBox_->setValue(normalized);
    autoBox_->setChecked(control_.supportsAuto && automatic);
    updating_ = false;
    updateEnabledState();
    if (notify) {
        notifyChanged();
    }
}

void ControlRow::setChangeHandler(ChangeHandler handler)
{
    changeHandler_ = std::move(handler);
}

void ControlRow::notifyChanged()
{
    if (!updating_ && changeHandler_) {
        changeHandler_(currentValue());
    }
}

void ControlRow::updateEnabledState()
{
    const bool manual = !control_.supportsAuto || !autoBox_->isChecked();
    slider_->setEnabled(manual);
    spinBox_->setEnabled(manual);
}
