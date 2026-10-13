#include "MainWindow.h"
#include "CasterBoard.h"
#include "CasterPlayer.h"
#include "CasterLabelColorPicker.h"
#include "CasterOSCServerConfigPicker.h"
#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QTabWidget>
#include <QComboBox>
#include <QPushButton>
#include <QToolBar>
#include <QAction>
#include <QTextBrowser>
#include "audio/AudioDevicesDialog.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QFileInfo>
#include "audio/AudioDeviceController.h"
#include <QtMultimedia/QMediaDevices>
#include <QTimer>
#include <QUdpSocket>
#include <QTest>
#include <QProcess>
#include <QFileInfo>
#include <QDir>

class ApplicationTest : public QObject {
    Q_OBJECT
private slots:
    void syncTabsDisabledAndHardening() {
        QUdpSocket receiver;
        QVERIFY(receiver.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        receiver.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1024 * 1024);
        QList<QByteArray> packets;
        connect(&receiver, &QUdpSocket::readyRead, [&] {
            while (receiver.hasPendingDatagrams()) {
                QByteArray data(receiver.pendingDatagramSize(), '\0');
                receiver.readDatagram(data.data(), data.size());
                packets.append(data);
            }
        });
        MainWindow window;
        auto addTab = [&] {
            QTimer::singleShot(0, [] {
                auto dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->setTextValue("Metadata test"); dialog->accept();
            });
            window.addNewTab();
        };
        addTab(); addTab();
        auto tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs); QCOMPARE(tabs->count(), 2);
        auto first = qobject_cast<CasterBoard *>(tabs->widget(0));
        auto second = qobject_cast<CasterBoard *>(tabs->widget(1));
        tabs->setCurrentIndex(1);
        auto player = second->players->value(",");
        QTimer::singleShot(0, [] {
            auto dialog = qobject_cast<CasterLabelColorPicker *>(QApplication::activeModalWidget());
            Q_ASSERT(dialog);
            dialog->set_slider_H_Value(60); dialog->button_Accept_Clicked();
        });
        player->openColorPicker();
        first->syncWithOSCClient(); second->syncWithOSCClient();
        QTest::qWait(30); QVERIFY(packets.isEmpty()); // disabled

        // Allocate an unused listener port, and configure through the real dialog.
        QUdpSocket probe;
        QVERIFY(probe.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        const quint16 inboundPort = probe.localPort(); probe.close();
        QString localhost("127.0.0.1");
        QTimer dialogs;
        connect(&dialogs, &QTimer::timeout, [&] {
            if (auto config = qobject_cast<CasterOSCServerConfigPicker *>(QApplication::activeModalWidget())) {
                config->setInBoundPort(inboundPort);
                config->setOutBoundIPv4(&localhost);
                config->setOutBoundPort(receiver.localPort());
                config->ok = true;
                config->Result = CasterOSCServerConfigPicker::Action_Start_Server;
                config->accept();
            } else if (auto message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                message->accept();
            }
        });
        dialogs.start(10); window.openOSCSettings(); dialogs.stop();
        QTRY_VERIFY(packets.size() >= 258);
        QTest::qWait(30); packets.clear();
        auto requestSync = [&] {
            OscMessageComposer sync("/glo/sync"); sync.pushFloat(1.0f);
            receiver.writeDatagram(*sync.getBytes(), QHostAddress::LocalHost, inboundPort);
        };
        requestSync(); QTRY_COMPARE(packets.size(), 258);
        auto contains = [&](const QByteArray &address, const QByteArray &value) {
            for (const auto &packet : packets)
                if (packet.startsWith(address + '\0') && packet.contains(value)) return true;
            return false;
        };
        QVERIFY(contains("/cbp/,/m/color", player->displayColor().toUtf8()));
        packets.clear(); tabs->setCurrentIndex(0);
        QTRY_COMPARE(packets.size(), 257);
        QVERIFY(contains("/cbp/,/m/color", "#DADADA"));
        packets.clear(); tabs->setCurrentIndex(1);
        QTRY_COMPARE(packets.size(), 257);
        QVERIFY(contains("/cbp/,/m/color", player->displayColor().toUtf8()));

        // Incoming metadata must remain read-only.
        const QString before = player->styleSheet();
        OscMessageComposer writeColor("/cbp/,/m/color"); writeColor.pushString("#000000");
        receiver.writeDatagram(*writeColor.getBytes(), QHostAddress::LocalHost, inboundPort);
        OscMessageComposer writeGrid("/cbp/,/m/grid"); writeGrid.pushInt32(99); writeGrid.pushInt32(99);
        receiver.writeDatagram(*writeGrid.getBytes(), QHostAddress::LocalHost, inboundPort);
        QTest::qWait(30); QCOMPARE(player->styleSheet(), before);

        // Run the local hardening regression sender when that separate patch is present.
        const QString script = QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("../osc/test_osc_packets.py");
        if (QFileInfo::exists(script)) {
            QProcess process;
            process.start("python3", {script, "127.0.0.1", QString::number(inboundPort)});
            QTRY_VERIFY_WITH_TIMEOUT(process.state() == QProcess::NotRunning, 10000);
            QCOMPARE(process.exitCode(), 0);
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        }
        packets.clear(); requestSync(); QTRY_COMPARE(packets.size(), 258);
        QVERIFY(contains("/cbp/,/m/color", player->displayColor().toUtf8()));
        // Existing older-client command still works.
        OscMessageComposer volume("/cbp/,/m/vol"); volume.pushFloat(0.42f);
        receiver.writeDatagram(*volume.getBytes(), QHostAddress::LocalHost, inboundPort);
        QTRY_COMPARE(player->player->volume(), 42);
    }

    void audioOutputDeviceSelection() {
        QSettings settings("CasterSoundboard", "CasterSoundboard");
        settings.remove("audio/outputDeviceId");

        MainWindow window;
        auto comboBox = window.findChild<QComboBox *>("audioOutputComboBox");
        QVERIFY(comboBox);
        QVERIFY(comboBox->count() >= 1);
        QCOMPARE(comboBox->itemText(0), QString("Default System Output"));
        QCOMPARE(comboBox->currentIndex(), 0);
        QVERIFY(CasterMediaPlayer::globalAudioDevice().isNull());

        const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
        if (!devices.isEmpty()) {
            // Select the first real device
            comboBox->setCurrentIndex(1);
            const QByteArray expectedId = devices.at(0).id();
            QCOMPARE(settings.value("audio/outputDeviceId").toByteArray(), expectedId);
            QCOMPARE(CasterMediaPlayer::globalAudioDevice().id(), expectedId);

            // A newly created player should inherit this device
            CasterMediaPlayer newPlayer;
            QCOMPARE(newPlayer.audioDevice().id(), expectedId);

            // Switching back to default output
            comboBox->setCurrentIndex(0);
            QVERIFY(settings.value("audio/outputDeviceId").toByteArray().isEmpty());
            QVERIFY(CasterMediaPlayer::globalAudioDevice().isNull());

            // Test persistence across new MainWindow instance
            settings.setValue("audio/outputDeviceId", expectedId);
            MainWindow restoredWindow;
            auto restoredCombo = restoredWindow.findChild<QComboBox *>("audioOutputComboBox");
            QVERIFY(restoredCombo);
            QCOMPARE(restoredCombo->currentData().toByteArray(), expectedId);
            QCOMPARE(CasterMediaPlayer::globalAudioDevice().id(), expectedId);

            // Cleanup
            settings.remove("audio/outputDeviceId");
        }
    }

    void audioDeviceAvailabilityAndSetupHelp() {
        MainWindow window;
        auto toolbar = window.findChild<QToolBar *>();
        auto output = window.findChild<QComboBox *>("audioOutputComboBox");
        auto devices = window.findChild<QPushButton *>("audioDevicesButton");
        auto help = window.findChild<QTextBrowser *>("audioSetupHelp");
        auto controls = window.findChild<QWidget *>("audioDeviceControls");
        QVERIFY(toolbar && output && devices && help && controls);
        auto actionFor = [toolbar](QWidget *widget) -> QAction * {
            for (auto action : toolbar->actions())
                if (toolbar->widgetForAction(action) == widget) return action;
            return nullptr;
        };
        auto outputAction = actionFor(output);
        auto devicesAction = actionFor(devices);
        QVERIFY(outputAction && devicesAction);
        auto controllers = qApp->findChildren<Audio::AudioDeviceController *>();
        QVERIFY(!controllers.isEmpty());
        auto controller = controllers.last();

        Audio::Discovery missing;
        missing.backend = "PulseAudio-compatible";
        missing.error = "pactl is missing";
        emit controller->devicesChanged(missing);
        QVERIFY(devicesAction->isVisible());
        QVERIFY(outputAction->isVisible());
        QVERIFY(!help->isHidden());
        QVERIFY(controls->isHidden());
#ifdef Q_OS_LINUX
        QVERIFY(help->toPlainText().contains("sudo apt install pulseaudio-utils"));
        for (const auto &domain : {"wiki.ubuntu.com", "wiki.debian.org", "docs.fedoraproject.org", "wiki.nixos.org"})
            QVERIFY(help->toHtml().contains(domain));
#endif
        QVERIFY(help->openExternalLinks());

        Audio::Discovery working;
        working.backend = "PulseAudio (on PipeWire)";
        working.capabilities = {true, true, true, true, true, false};
        emit controller->devicesChanged(working);
        QVERIFY(devicesAction->isVisible());
        QVERIFY(!outputAction->isVisible());
        QVERIFY(help->isHidden());
        QVERIFY(!controls->isHidden());

        missing.error = "Connection refused";
        missing.capabilities.serviceRestart = true;
        emit controller->devicesChanged(missing);
        QVERIFY(devicesAction->isVisible());
        QVERIFY(outputAction->isVisible());
        QVERIFY(!help->isHidden());
#ifdef Q_OS_LINUX
        QVERIFY(help->toPlainText().contains("connection error"));
#endif
        emit controller->devicesChanged(working);
        QVERIFY(!outputAction->isVisible());
        QVERIFY(help->isHidden());

        auto dialog = window.findChild<Audio::AudioDevicesDialog *>();
        auto helpButton = dialog->findChild<QPushButton *>("audioDevicesHelpButton");
        auto exitButton = dialog->findChild<QPushButton *>("audioDevicesExitButton");
        auto helpDialog = dialog->findChild<QDialog *>("audioDevicesHelpDialog");
        QVERIFY(helpButton && exitButton && helpDialog);
        QCOMPARE(exitButton->text(), QString("Exit"));
        dialog->show();
        helpButton->click();
        QVERIFY(helpDialog->isVisible());
        QVERIFY(!controls->isHidden());
        QVERIFY(helpDialog->findChild<QTextBrowser *>()->toPlainText().contains("sudo apt install pulseaudio-utils"));
        helpDialog->reject();
        QVERIFY(dialog->isVisible());
        helpButton->click();
        exitButton->click();
        QVERIFY(!dialog->isVisible());
        QVERIFY(!helpDialog->isVisible());
    }

    void managedRoutingOwnsOutputSelection() {
        MainWindow window;
        auto output = window.findChild<QComboBox *>("audioOutputComboBox");
        QVERIFY(output);
        auto controllers = qApp->findChildren<Audio::AudioDeviceController *>();
        QVERIFY(!controllers.isEmpty());
        auto controller = controllers.last();
        emit controller->playbackRoutingChanged(true);
        QVERIFY(!output->isEnabled());
        emit controller->playbackRoutingChanged(false);
        QVERIFY(output->isEnabled());
    }

    void oscServerProgrammaticControl() {
        MainWindow window;
        QVERIFY(!window.isOSCServerRunning());

        QUdpSocket probe;
        QVERIFY(probe.bind(QHostAddress::LocalHost, quint16(0)));
        const quint16 inboundPort = probe.localPort();
        probe.close();

        QVERIFY(window.startOSCServer(inboundPort, "127.0.0.1", 5052));
        QVERIFY(window.isOSCServerRunning());

        // Stop server
        window.stopOSCServer();
        QVERIFY(!window.isOSCServerRunning());
    }

    void defaultProfileAndSettings() {
        QSettings settings("CasterSoundboard", "CasterSoundboard");
        const QString origDefault = settings.value("profile/defaultProfile").toString();
        const QString origLast = settings.value("profile/lastOpenedProfile").toString();
        const bool origReopen = settings.value("profile/reopenLastOnStartup", false).toBool();
        const bool origAutoOsc = settings.value("osc/autoStart", false).toBool();

        MainWindow window;
        auto addTab = [&] {
            QTimer::singleShot(0, [] {
                auto dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                Q_ASSERT(dialog);
                dialog->setTextValue("ProfileTest");
                dialog->accept();
            });
            window.addNewTab();
        };
        addTab();
        auto tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);

        auto board = qobject_cast<CasterBoard *>(tabs->widget(0));
        QVERIFY(board);

        // Before setting default profile, tab tooltip is "Unsaved soundboard"
        QCOMPARE(tabs->tabToolTip(0), QString("Unsaved soundboard"));

        QTemporaryDir profiles;
        QVERIFY(profiles.isValid());
        const QString dummyPath = profiles.filePath("default.caster");
        *board->profileFilePath = dummyPath;
        window.updateTabToolTips();
        QCOMPARE(tabs->tabToolTip(0), dummyPath);

        // The custom atomic save also updates upstream startup preferences.
        window.saveTab();
        QVERIFY(QFileInfo::exists(dummyPath));
        QCOMPARE(settings.value("profile/lastOpenedProfile").toString(), dummyPath);

        // Mark as default profile
        settings.setValue("profile/defaultProfile", dummyPath);
        window.updateTabToolTips();
        QCOMPARE(tabs->tabToolTip(0), dummyPath + " [Default Startup Profile]");

        // Clear default profile
        settings.remove("profile/defaultProfile");
        window.updateTabToolTips();
        QCOMPARE(tabs->tabToolTip(0), dummyPath);

        // Test QSettings keys for reopen and autoStart
        settings.setValue("profile/reopenLastOnStartup", true);
        QCOMPARE(settings.value("profile/reopenLastOnStartup").toBool(), true);

        settings.setValue("osc/autoStart", true);
        QCOMPARE(settings.value("osc/autoStart").toBool(), true);

        // Restore settings
        if (origDefault.isEmpty()) settings.remove("profile/defaultProfile");
        else settings.setValue("profile/defaultProfile", origDefault);
        if (origLast.isEmpty()) settings.remove("profile/lastOpenedProfile");
        else settings.setValue("profile/lastOpenedProfile", origLast);
        settings.setValue("profile/reopenLastOnStartup", origReopen);
        settings.setValue("osc/autoStart", origAutoOsc);
    }
};
QTEST_MAIN(ApplicationTest)
#include "test_application.moc"
