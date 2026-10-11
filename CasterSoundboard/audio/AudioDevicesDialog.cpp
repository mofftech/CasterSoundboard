#include "AudioDevicesDialog.h"
#include <QComboBox>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
namespace Audio {
AudioDevicesDialog::AudioDevicesDialog(AudioDeviceController *c, QWidget *parent) : QDialog(parent), controller(c) {
    setWindowTitle(tr("Audio Devices")); setModal(false); resize(720, 540);
    auto layout = new QVBoxLayout(this); backend = new QLabel; backend->setWordWrap(true); layout->addWidget(backend);
    auto form = new QFormLayout; form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); microphone = new QComboBox; systemOutput = new QComboBox; monitorOutput = new QComboBox;
    for (auto combo : {microphone, systemOutput, monitorOutput}) { combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); combo->setMinimumContentsLength(20); combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); }
    form->addRow(tr("Physical microphone"), microphone); form->addRow(tr("System default output (global)"), systemOutput);
    form->addRow(tr("Soundboard monitor output"), monitorOutput); layout->addLayout(form);
    auto appHint = new QLabel(tr("Set the game’s input and output to Default/System Default. Enable the mixed virtual microphone as system default input below, then Apply. If an existing voice session keeps its old input, reconnect voice chat.")); appHint->setWordWrap(true); layout->addWidget(appHint);
    mix = new QCheckBox(tr("Send microphone + soundboard to a virtual microphone"));
    monitor = new QCheckBox(tr("Monitor soundboard through chosen output (never the live microphone)"));
    defaultInput = new QCheckBox(tr("Use mixed virtual microphone as system default input (global)"));
    layout->addWidget(mix); layout->addWidget(monitor); layout->addWidget(defaultInput);
    recoveryDevice = new QComboBox;
    recoveryDevice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    recoveryDevice->setMinimumContentsLength(20);
    recoveryDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto recoveryForm = new QFormLayout; recoveryForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    recoveryForm->addRow(tr("Device to recover"), recoveryDevice); layout->addLayout(recoveryForm);
    recoveryHint = new QLabel(tr("Recovery can enable disabled devices or recreate missing endpoints. An idle/suspended device is not necessarily broken."));
    recoveryHint->setWordWrap(true); layout->addWidget(recoveryHint);
    auto recoveryButtons = new QHBoxLayout;
    recover = new QPushButton(tr("Recover selected device…")); restart = new QPushButton(tr("Restart desktop audio…"));
    recoveryButtons->addWidget(recover); recoveryButtons->addWidget(restart); recoveryButtons->addStretch(); layout->addLayout(recoveryButtons);
    connect(recoveryDevice, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateEnabled(); });
    connect(recover, &QPushButton::clicked, this, [this] {
        Plan p; p.metadata["recoveryEndpoint"] = recoveryDevice->currentData();
        controller->request(AudioDeviceController::RecoveryPreview, Configuration(), p);
    });
    connect(restart, &QPushButton::clicked, this, [this] { controller->request(AudioDeviceController::RestartPreview); });
    auto confirmRecovery = [this](Plan p, bool services) {
        if (!p.error.isEmpty()) { status->setText(p.error); details->appendPlainText(p.error); return; }
        details->appendPlainText(p.changes.join('\n'));
        QMessageBox box(QMessageBox::Warning, services ? tr("Restart ALL desktop audio?") : tr("Recover selected device?"), p.changes.join('\n'), QMessageBox::Cancel, this);
        auto confirm = box.addButton(services ? tr("Restart all audio") : tr("Recover device"), QMessageBox::AcceptRole);
        box.setDefaultButton(QMessageBox::Cancel); box.exec();
        if (box.clickedButton() == confirm) controller->request(services ? AudioDeviceController::Restart : AudioDeviceController::Recover, Configuration(), p);
    };
    connect(c, &AudioDeviceController::recoveryPreviewReady, this, [confirmRecovery](Plan p) { confirmRecovery(p, false); });
    connect(c, &AudioDeviceController::restartPreviewReady, this, [confirmRecovery](Plan p) { confirmRecovery(p, true); });
    status = new QLabel; status->setWordWrap(true); status->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status);
    details = new QPlainTextEdit; details->setReadOnly(true); details->setPlaceholderText(tr("Device IDs, planned changes and troubleshooting details")); layout->addWidget(details);
    auto buttons = new QHBoxLayout; refresh = new QPushButton(tr("Refresh")); stop = new QPushButton(tr("Stop routing"));
    auto cancel = new QPushButton(tr("Cancel")); apply = new QPushButton(tr("Apply…"));
    buttons->addWidget(refresh); buttons->addWidget(stop); buttons->addStretch(); buttons->addWidget(cancel); buttons->addWidget(apply); layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(refresh, &QPushButton::clicked, this, [this] { controller->request(AudioDeviceController::Refresh); });
    connect(stop, &QPushButton::clicked, this, [this] { controller->request(AudioDeviceController::Stop); });
    connect(apply, &QPushButton::clicked, this, [this] { controller->request(AudioDeviceController::Preview, staged()); });
    connect(mix, &QCheckBox::toggled, this, &AudioDevicesDialog::updateEnabled);
    connect(monitor, &QCheckBox::toggled, this, &AudioDevicesDialog::updateEnabled);
    connect(c, &AudioDeviceController::devicesChanged, this, &AudioDevicesDialog::populate);
    connect(c, &AudioDeviceController::busyChanged, this, [this](bool b) { busy = b; updateEnabled(); });
    connect(c, &AudioDeviceController::previewReady, this, [this](Plan plan) {
        if (!plan.error.isEmpty()) { status->setText(plan.error); details->appendPlainText(plan.error); return; }
        details->appendPlainText(plan.changes.join('\n'));
        if (QMessageBox::question(this, tr("Apply audio changes?"), plan.changes.join('\n') + tr("\n\nApply these changes now?"), QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Apply)
            controller->request(AudioDeviceController::Apply, plan.desired, plan);
    });
    connect(c, &AudioDeviceController::resultReady, this, [this](Result r) {
        status->setText(r.message + (r.voiceSource.isEmpty() ? QString() : tr("\nVoice input for games: ") + r.voiceSource));
        if (!r.message.isEmpty() && !details->toPlainText().endsWith(r.message)) details->appendPlainText(r.message);
    });
    populate(c->devices());
}
Configuration AudioDevicesDialog::staged() const {
    Configuration c; c.microphone = microphone->currentData().toString(); c.systemOutput = systemOutput->currentData().toString();
    c.monitorOutput = monitorOutput->currentData().toString(); c.mix = mix->isChecked() && capabilities.virtualSinks && capabilities.loopbacks && capabilities.streamRetargeting; c.monitor = monitor->isChecked() && capabilities.virtualSinks && capabilities.loopbacks && capabilities.streamRetargeting; c.defaultInput = defaultInput->isChecked() && capabilities.defaults; return c;
}
void AudioDevicesDialog::openSettings() {
    auto saved = controller->saved();
    auto select = [](QComboBox *box, const QString &id) {
        int i = box->findData(id); if (i < 0 && !id.isEmpty()) { box->addItem("Missing/disconnected — " + id, id); i = box->count() - 1; }
        box->setCurrentIndex(i < 0 ? 0 : i);
    };
    select(microphone, saved.microphone); select(systemOutput, saved.systemOutput); select(monitorOutput, saved.monitorOutput);
    mix->setChecked(saved.mix); monitor->setChecked(saved.monitor); defaultInput->setChecked(saved.defaultInput);
    show(); raise(); activateWindow(); controller->request(AudioDeviceController::Refresh);
}
void AudioDevicesDialog::populate(const Discovery &d) {
    capabilities = d.capabilities;
    mix->setVisible(capabilities.virtualSinks && capabilities.loopbacks && capabilities.streamRetargeting);
    monitor->setVisible(capabilities.virtualSinks && capabilities.loopbacks && capabilities.streamRetargeting);
    defaultInput->setVisible(capabilities.defaults && capabilities.virtualSinks && capabilities.loopbacks);
    ready = d.error.isEmpty() && d.capabilities.defaults;
    backend->setText(tr("Audio backend: ") + d.backend + "\n" + d.status + "\n" + d.error);
    auto fill = [&](QComboBox *box, bool input) {
        const QString selected = box->currentData().toString(); box->clear(); box->addItem(tr("Choose a device…"), QString());
        for (auto e : d.endpoints) if (e.input == input) {
            QString suffix = !e.healthWarning.isEmpty() ? tr(" — needs attention") : !e.available ? tr(" — reported unavailable") : e.active ? tr(" — active") : tr(" — available after profile/port change");
            box->addItem(e.label + suffix, e.id); box->setItemData(box->count() - 1, e.id + (e.healthWarning.isEmpty() ? QString() : "\n" + e.healthWarning), Qt::ToolTipRole);
        }
        int index = box->findData(selected);
        if (index < 0 && !selected.isEmpty()) { box->addItem(tr("Missing/disconnected — ") + selected, selected); index = box->count() - 1; }
        box->setCurrentIndex(index < 0 ? 0 : index);
    };
    fill(microphone, true); fill(systemOutput, false); fill(monitorOutput, false);
    const auto selected = recoveryDevice->currentData().toString(); recoveryDevice->clear();
    recoveryDevice->addItem(tr("Choose a device to enable or reset…"), QString());
    for (auto e : d.endpoints) if (e.recoverySupported) {
        recoveryDevice->addItem(e.label + (e.input ? tr(" (input)") : tr(" (output)")) +
            (e.healthWarning.isEmpty() ? QString() : tr(" — needs attention")), e.id);
        recoveryDevice->setItemData(recoveryDevice->count() - 1, e.healthWarning + "\n" + e.id, Qt::ToolTipRole);
        recoveryDevice->setItemData(recoveryDevice->count() - 1, e.healthWarning, Qt::UserRole + 1);
    }
    int selectedIndex = recoveryDevice->findData(selected);
    if (selectedIndex < 0 && !selected.isEmpty()) {
        recoveryDevice->addItem(tr("Missing/disconnected — ") + selected, selected);
        selectedIndex = recoveryDevice->count() - 1;
        recoveryDevice->setItemData(selectedIndex, tr("This card is missing. Reconnect it and refresh devices before attempting recovery."), Qt::UserRole + 1);
    }
    recoveryDevice->setCurrentIndex(selectedIndex < 0 ? 0 : selectedIndex);
    for (auto box : {microphone, systemOutput, monitorOutput, recoveryDevice}) {
        int width = 0;
        for (int i = 0; i < box->count(); ++i) {
            width = qMax(width, box->fontMetrics().horizontalAdvance(box->itemText(i)) + 40);
            box->setItemData(i, box->itemText(i) + "\n" + box->itemData(i, Qt::ToolTipRole).toString(), Qt::ToolTipRole);
        }
        box->view()->setMinimumWidth(width);
    }
    restart->setVisible(capabilities.serviceRestart);
    updateEnabled();
}
void AudioDevicesDialog::updateEnabled() {
    recoveryDevice->setEnabled(ready && !busy);
    recover->setEnabled(ready && !busy && !recoveryDevice->currentData().toString().isEmpty());
    restart->setEnabled(capabilities.serviceRestart && !busy);
    const auto health = recoveryDevice->currentData(Qt::UserRole + 1).toString();
    recoveryHint->setText(health.isEmpty() ? tr("Recovery can enable disabled devices or recreate missing endpoints. An idle/suspended device is not necessarily broken.") : health);
    apply->setEnabled(ready && !busy); refresh->setEnabled(!busy); stop->setEnabled(!busy);
    systemOutput->setEnabled(ready && !busy); mix->setEnabled(ready && !busy); monitor->setEnabled(ready && !busy);
    microphone->setEnabled(ready && !busy && mix->isChecked()); monitorOutput->setEnabled(ready && !busy && monitor->isChecked());
    defaultInput->setEnabled(ready && !busy && mix->isChecked());
}
}
