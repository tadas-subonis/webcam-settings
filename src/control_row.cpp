#include "control_row.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

#include <limits>

ControlRow::ControlRow(const CameraControl &control, QWidget *parent)
    : QWidget(parent), control_(control)
{
    nameLabel_ = new QLabel(control.name, this);
    nameLabel_->setMinimumWidth(165);
    nameLabel_->setToolTip(QStringLiteral("%1 · %2").arg(control.category, control.id));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 3, 0, 3);
    layout->addWidget(nameLabel_);

    const bool normalInteger = control.type == CameraControlType::Integer
        && control.minimum >= std::numeric_limits<int>::min()
        && control.maximum <= std::numeric_limits<int>::max();
    if (normalInteger) {
        slider_ = new QSlider(Qt::Horizontal, this);
        slider_->setRange(static_cast<int>(control.minimum), static_cast<int>(control.maximum));
        slider_->setSingleStep(static_cast<int>(control.normalizedStep()));
        slider_->setPageStep(static_cast<int>(control.normalizedStep() * 5));
        spinBox_ = new QSpinBox(this);
        spinBox_->setRange(static_cast<int>(control.minimum), static_cast<int>(control.maximum));
        spinBox_->setSingleStep(static_cast<int>(control.normalizedStep()));
        spinBox_->setMinimumWidth(88);
        layout->addWidget(slider_, 1);
        layout->addWidget(spinBox_);
        connect(slider_, &QSlider::valueChanged, this, [this](const int value) {
            if (!updating_) setCurrentValue(value, autoBox_ && autoBox_->isChecked(), true);
        });
        connect(spinBox_, &QSpinBox::valueChanged, this, [this](const int value) {
            if (!updating_) setCurrentValue(value, autoBox_ && autoBox_->isChecked(), true);
        });
    } else if (control.type == CameraControlType::Integer) {
        wideValueEdit_ = new QLineEdit(QString::number(control.value), this);
        wideValueEdit_->setToolTip(tr("Accepted range: %1 to %2").arg(control.minimum).arg(control.maximum));
        layout->addWidget(wideValueEdit_, 1);
        connect(wideValueEdit_, &QLineEdit::editingFinished, this, [this] { notifyChanged(); });
    } else if (control.type == CameraControlType::Menu) {
        menuBox_ = new QComboBox(this);
        for (const auto &option : control.options) menuBox_->addItem(option.label, option.value);
        layout->addWidget(menuBox_, 1);
        connect(menuBox_, &QComboBox::currentIndexChanged, this, [this] { notifyChanged(); });
    } else if (control.type == CameraControlType::Boolean) {
        booleanBox_ = new QCheckBox(tr("Enabled"), this);
        layout->addWidget(booleanBox_, 1);
        connect(booleanBox_, &QCheckBox::toggled, this, [this] { notifyChanged(); });
    } else {
        actionButton_ = new QPushButton(tr("Run"), this);
        layout->addWidget(actionButton_, 1);
        connect(actionButton_, &QPushButton::clicked, this, [this] { notifyChanged(); });
    }

    autoBox_ = new QCheckBox(tr("Auto"), this);
    autoBox_->setVisible(control.supportsAuto);
    layout->addWidget(autoBox_);
    connect(autoBox_, &QCheckBox::toggled, this, [this] {
        updateEnabledState();
        notifyChanged();
    });

    resetButton_ = new QPushButton(tr("Reset"), this);
    resetButton_->setVisible(control.type != CameraControlType::Button);
    resetButton_->setToolTip(tr("Restore the camera's advertised default"));
    layout->addWidget(resetButton_);
    connect(resetButton_, &QPushButton::clicked, this, [this] {
        setCurrentValue(control_.defaultValue, false, true);
    });

    setCurrentValue(control.value, control.automatic, false);
    updateEnabledState();
}

CameraControlValue ControlRow::currentValue() const
{
    qint64 value = control_.value;
    if (spinBox_) value = spinBox_->value();
    else if (wideValueEdit_) value = wideValueEdit_->text().toLongLong();
    else if (menuBox_) value = menuBox_->currentData().toLongLong();
    else if (booleanBox_) value = booleanBox_->isChecked() ? 1 : 0;
    return {control_.id, control_.clamped(value), control_.supportsAuto && autoBox_->isChecked()};
}

QString ControlRow::controlName() const
{
    return control_.name;
}

bool ControlRow::canPersist() const
{
    return !control_.readOnly && control_.type != CameraControlType::Button;
}

void ControlRow::setCurrentValue(const qint64 value, const bool automatic, const bool notify)
{
    updating_ = true;
    const qint64 normalized = control_.clamped(value);
    control_.value = normalized;
    if (slider_) slider_->setValue(static_cast<int>(normalized));
    if (spinBox_) spinBox_->setValue(static_cast<int>(normalized));
    if (wideValueEdit_) wideValueEdit_->setText(QString::number(normalized));
    if (menuBox_) {
        const int index = menuBox_->findData(normalized);
        if (index >= 0) menuBox_->setCurrentIndex(index);
    }
    if (booleanBox_) booleanBox_->setChecked(normalized != 0);
    autoBox_->setChecked(control_.supportsAuto && automatic);
    updating_ = false;
    updateEnabledState();
    if (notify) notifyChanged();
}

void ControlRow::setChangeHandler(ChangeHandler handler)
{
    changeHandler_ = std::move(handler);
}

void ControlRow::notifyChanged()
{
    if (!updating_ && !control_.readOnly && changeHandler_) changeHandler_(currentValue());
}

void ControlRow::updateEnabledState()
{
    const bool manual = !control_.readOnly && (!control_.supportsAuto || !autoBox_->isChecked());
    if (slider_) slider_->setEnabled(manual);
    if (spinBox_) spinBox_->setEnabled(manual);
    if (wideValueEdit_) wideValueEdit_->setEnabled(manual);
    if (menuBox_) menuBox_->setEnabled(manual);
    if (booleanBox_) booleanBox_->setEnabled(manual);
    if (actionButton_) actionButton_->setEnabled(!control_.readOnly);
    resetButton_->setEnabled(manual);
}
