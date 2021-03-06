/*
 * Copyright 2017 Jacob Jordan <doublejinitials@ou.edu>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "maincontroller.h"

#include <QTimer>
#include <QMessageBox>
#include <QtWebEngine>

#include <SDL2/SDL.h>

#include <Qt5GStreamer/QGst/Init>
#include <Qt5GStreamer/QGlib/Error>

#include "qmlgstreamerglitem.h"
#include "mapviewimpl.h"
#include "qmlgstreamerpainteditem.h"
#include "pitchrollview.h"
#include "soro_core/constants.h"
#include "soro_core/logger.h"
#include "soro_core/notificationmessage.h"

#define LogTag "MainController"

namespace Soro {

MainController *MainController::_self = nullptr;

MainController::MainController(QObject *parent) : QObject(parent) { }

void MainController::panic(QString tag, QString message)
{
    LOG_E(LogTag, QString("panic(): %1: %2").arg(tag, message));
    QMessageBox::critical(0, "Mission Control", "<b>Fatal error in " + tag + "</b><br><br>" + message);
    LOG_I(LogTag, "Committing suicide...");
    delete _self;
    LOG_I(LogTag, "Exiting with code 1");
    exit(1);
}

void MainController::init(QApplication *app)
{
    if (_self)
    {
        LOG_E(LogTag, "init() called when already initialized");
    }
    else
    {
        LOG_I(LogTag, "Starting...");
        _self = new MainController(app);

        // Use a timer to wait for the event loop to start
        QTimer::singleShot(0, _self, []()
        {
            //
            // Create the settings model and load the main settings file
            //
            LOG_I(LogTag, "Loading settings...");
            try
            {
                _self->_settingsModel = new SettingsModel;
                _self->_settingsModel->load();
            }
            catch (QString err)
            {
                panic(LogTag, QString("Error loading settings: %1").arg(err));
            }

            //
            // Create a unique identifier for this mission control, it is mainly used as a unique node name for MQTT
            //
            _self->_mcId = _self->genId();
            LOG_I(LogTag, QString("Mission Control ID is: %1").arg(_self->_mcId).toLocal8Bit().constData());

            //
            // Create camera settings model to load camera configuration
            //
            LOG_I(LogTag, "Loading camera settings...");
            try
            {
                _self->_cameraSettingsModel = new CameraSettingsModel;
                _self->_cameraSettingsModel->load();
            }
            catch (QString err)
            {
                panic(LogTag, QString("Error loading camera settings: %1").arg(err));
                return;
            }

            //
            // Create media settings model to load audio/video profile configuration
            //
            LOG_I(LogTag, "Loading media profile settings...");
            try
            {
                _self->_mediaProfileSettingsModel = new MediaProfileSettingsModel;
                _self->_mediaProfileSettingsModel->load();
            }
            catch (QString err)
            {
                panic(LogTag, QString("Error loading media profile settings: %1").arg(err));
                return;
            }

            /*//
            // Create media settings model to load keyboard & gamepad bindings
            //
            LOG_I(LogTag, "Loading keyboard and gamepad binding profile settings...");
            try
            {
                _self->_bindsSettingsModel = new BindsSettingsModel;
                _self->_bindsSettingsModel->load();
            }
            catch (QString err)
            {
                panic(LogTag, QString("Error loading keyboard and gamepad binding profile settings: %1").arg(err));
                return;
            }*/

            //
            // Initialize Qt5GStreamer, must be called before anything else is done with it
            //
            LOG_I(LogTag, "Initializing QtGstreamer...");
            try
            {
                QGst::init();
            }
            catch (QGlib::Error e)
            {
                panic(LogTag, QString("Failed to initialize QtGStreamer:  %1").arg(e.message()));
                return;
            }

            //
            // Initiaize the Qt webengine (i.e. blink web engine) for use in QML
            //
            LOG_I(LogTag, "Initializing QtWebEngine...");
            QtWebEngine::initialize();

            //
            // Initialize SDL (for gamepad reading)
            //
            LOG_I(LogTag, "Initializing SDL...");
            if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0)
            {
                panic(LogTag, QString("Failed to initialize SDL:  %1").arg(SDL_GetError()));
                return;
            }

            //
            // Load the SDL gamepad map file
            // This map file allows SDL to know which button/axis (i.e. "Left X Axis") corresponds
            // to the raw reading from the controller (i.e. "Axis 0")
            //
            LOG_I(LogTag, "Initializing SDL gamepad map...");
            QFile gamepadMap(":/config/gamecontrollerdb.txt"); // Loads from assets.qrc
            gamepadMap.open(QIODevice::ReadOnly);
            while (gamepadMap.bytesAvailable())
            {
                if (SDL_GameControllerAddMapping(gamepadMap.readLine().data()) == -1)
                {
                    panic(LogTag, QString("Failed to apply SDL gamepad map: %1").arg(SDL_GetError()));
                    gamepadMap.close();
                    return;
                }
            }
            gamepadMap.close();

            //
            // Create connection status controller
            //
            LOG_I(LogTag, "Initializing connection status controller...");
            _self->_connectionStatusController = new ConnectionStatusController(_self->_settingsModel, _self);

            //
            // Create the GamepadController instance
            //
            LOG_I(LogTag, "Initializing gamepad controller...");
            _self->_gamepadController = new GamepadController(_self);

            switch (_self->_settingsModel->getConfiguration()) {
            case SettingsModel::DriverConfiguration:
                //
                // Create drive control system
                //
                LOG_I(LogTag, "Initializing drive control system...");
                _self->_driveControlSystem = new DriveControlSystem(_self->_settingsModel, _self);
                break;
            case SettingsModel::ArmOperatorConfiguration:
                //
                // Create arm control system
                //
                LOG_I(LogTag, "Initializing arm control system...");
                _self->_armControlSystem = new ArmControlSystem(_self->_settingsModel, _self);
                break;
            case SettingsModel::CameraOperatorConfiguration:
                //TODO
                break;
            case SettingsModel::ScienceArmOperatorConfiguration:
                //
                // Create science arm control system
                //
                LOG_I(LogTag, "Initializing science arm control system...");
                _self->_scienceArmControlSystem = new ScienceArmControlSystem(_self->_settingsModel, _self);
                break;
            case SettingsModel::ScienceCameraOperatorConfiguration:
                //
                // Create science camera gimbal control system
                //
                LOG_I(LogTag, "Initializing science camera control system...");
                _self->_scienceCameraControlSystem = new ScienceCameraControlSystem(_self->_settingsModel, _self);
                break;
            case SettingsModel::ObserverConfiguration:
                break;
            }

            //
            // Create the audio controller instance
            //
            LOG_I(LogTag, "Initializing audio controller...");
            _self->_audioClient = new AudioClient(_self->_settingsModel, _self);

            //
            // Create the QML application engine and setup the GStreamer surface
            //
            qmlRegisterType<MapViewImpl>("Soro", 1, 0, "MapViewImpl");
            qmlRegisterType<PitchRollView>("Soro", 1, 0, "PitchRollView");
            if (_self->_settingsModel->getEnableHwRendering())
            {
                // Use the hardware opengl rendering surface, doesn't work on some hardware
                LOG_I(LogTag, "Registering QmlGStreamerItem as GStreamerSurface...");
                qmlRegisterType<QmlGStreamerGlItem>("Soro", 1, 0, "GStreamerSurface");
            }
            else
            {
                // Use the software rendering surface, works everywhere but slower
                LOG_I(LogTag, "Registering QmlGStreamerPaintedItem as GStreamerSurface...");
                qmlRegisterType<QmlGStreamerPaintedItem>("Soro", 1, 0, "GStreamerSurface");
            }
            LOG_I(LogTag, "Initializing QML engine...");
            _self->_qmlEngine = new QQmlEngine(_self);

            //
            // Create the main UI
            //
            LOG_I(LogTag, "Creating main window...");
            _self->_mainWindowController = new MainWindowController(_self->_qmlEngine, _self->_settingsModel, _self->_mediaProfileSettingsModel, _self->_cameraSettingsModel, _self);

            //
            // Create the video controller instance
            //
            LOG_I(LogTag, "Initializing video controller...");
            _self->_videoClient = new VideoClient(_self->_settingsModel, _self->_cameraSettingsModel, _self->_mainWindowController->getVideoSinks(), _self);

            //
            // Connect to connection status signals
            //
            connect(_self->_connectionStatusController, &ConnectionStatusController::dataRateUpdate,
                    _self->_mainWindowController, &MainWindowController::onDataRateUpdated);
            connect(_self->_connectionStatusController, &ConnectionStatusController::latencyUpdate,
                    _self->_mainWindowController, &MainWindowController::onLatencyUpdated);
            connect(_self->_connectionStatusController, &ConnectionStatusController::connectedChanged,
                    _self->_mainWindowController, &MainWindowController::onConnectedChanged);
            connect(_self->_connectionStatusController, &ConnectionStatusController::connectedChanged, _self, [](bool connected)
            {
                // Enable/disable control systems depending on the connection state. Although the connection status controller reports
                // the connection is down, it is possible that the individual controls systems may still be connected. They should
                // not be allowed to control the rover unless everything is connected for safety
                if (connected)
                {
                    if (_self->_driveControlSystem) _self->_driveControlSystem->enable();
                    if (_self->_armControlSystem) _self->_armControlSystem->enable();
                    if (_self->_scienceArmControlSystem) _self->_scienceArmControlSystem->enable();
                    if (_self->_scienceCameraControlSystem) _self->_scienceCameraControlSystem->enable();
                }
                else
                {
                    if (_self->_driveControlSystem) _self->_driveControlSystem->disable();
                    if (_self->_armControlSystem) _self->_armControlSystem->disable();
                    if (_self->_scienceArmControlSystem) _self->_scienceArmControlSystem->disable();
                    if (_self->_scienceCameraControlSystem) _self->_scienceCameraControlSystem->disable();
                }
            });

            //
            // Connect signals relating to the gamepad system
            //
            connect(_self->_gamepadController, &GamepadController::gamepadChanged, _self, [](bool connected, QString name)
            {
                if (connected)
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Info, name + " connected", "This controller is ready for use.");
                }
                else
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Controller disconnected", "No active controller available.");
                }
            });

            //
            // Connect signals relating to the audio/video system
            //
            connect(_self->_audioClient, &AudioClient::playing, _self->_mainWindowController, &MainWindowController::onAudioProfileChanged);
            connect(_self->_audioClient, &AudioClient::stopped, _self, []()
            {
                _self->_mainWindowController->onAudioProfileChanged(GStreamerUtil::AudioProfile());
            });
            connect(_self->_videoClient, &VideoClient::playing, _self->_mainWindowController, &MainWindowController::onVideoProfileChanged);
            connect(_self->_videoClient, &VideoClient::stopped, _self, [](uint cameraIndex)
            {
                _self->_mainWindowController->onVideoProfileChanged(cameraIndex, GStreamerUtil::VideoProfile());
            });
            connect(_self->_videoClient, &VideoClient::videoServerDisconnected, _self, [](uint computer)
            {
                _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Video Server Stopped", "Video server #" + QString::number(computer) + " has either exited, crashed, or lost connection.");
            });
            connect(_self->_videoClient, &VideoClient::masterVideoClientDisconnected, _self, []()
            {
                _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Mission Control Master Stopped", "The mission control master has either exited, crashed, or lost connection. Audio/Video and several other features will not work until it is restarted.");
            });
            connect(_self->_audioClient, &AudioClient::audioServerDisconnected, _self, []()
            {
                _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Audio Server Stopped", "Audio server has either exited, crashed, or lost connection.");
            });
            connect(_self->_videoClient, &VideoClient::gstError, _self, [](QString message, uint cameraIndex)
            {
                _self->_self->_mainWindowController->notify(NotificationMessage::Level_Error,
                                              "Error playing " + _self->_cameraSettingsModel->getCamera(cameraIndex).name,
                                              "There was an error while decoding the video stream: " + message);
            });
            connect(_self->_audioClient, &AudioClient::gstError, _self, [](QString message)
            {
                _self->_mainWindowController->notify(NotificationMessage::Level_Error,
                                              "Error playing audio",
                                              "There was an error while decoding the audio stream: " + message);
            });

            //
            // Connect signals related to the drive system
            //
            if (_self->_driveControlSystem)
            {
                connect(_self->_gamepadController, &GamepadController::axisChanged,
                        _self->_driveControlSystem, &DriveControlSystem::onGamepadAxisUpdate);
                connect(_self->_driveControlSystem, &DriveControlSystem::driveMultiplexerDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Drive Multiplexer Disconnected", "The drive multiplexer system running on the rover computer has either exited, crashed, or lost connection.");
                });
                connect(_self->_driveControlSystem, &DriveControlSystem::driveMicrocontrollerDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Drive Microcontroller Disconnected", "The drive microcontroller on the rover has either exited, crashed, or lost connection.");
                });
                connect(_self->_driveControlSystem, &DriveControlSystem::driveControllerDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Drive Controller Disconnected", "The drive controller running on the rover computer has either exited, crashed, or lost connection.");
                });
            }

            //
            // Connect signals related to the arm system
            //
            if (_self->_armControlSystem)
            {
                connect(_self->_armControlSystem, &ArmControlSystem::masterArmConnectedChanged, _self, [](bool connected)
                {
                    if (connected)
                    {
                        _self->_mainWindowController->notify(NotificationMessage::Level_Info, "Master Arm Connected", "The master arm is connected.");
                    }
                    else
                    {
                        _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Master Arm Disconnected", "The master arm is not connected.");
                    }
                });

                connect(_self->_armControlSystem, &ArmControlSystem::slaveArmDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Arm Disconnected", "The arm microcontroller on the rover has either exited, crashed, or lost connection.");
                });
                connect(_self->_armControlSystem, &ArmControlSystem::armControllerDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Arm Controller Disconnected", "The arm control system running on the rover computer has either exited, crashed, or lost connection.");
                });
            }

            //
            // Connect signals related to the science arm system
            //
            if (_self->_scienceArmControlSystem)
            {
                connect(_self->_scienceArmControlSystem, &ScienceArmControlSystem::masterArmConnectedChanged, _self, [](bool connected)
                {
                    if (connected)
                    {
                        _self->_mainWindowController->notify(NotificationMessage::Level_Info, "Master Science Arm Connected", "The master science arm is connected.");
                    }
                    else
                    {
                        _self->_mainWindowController->notify(NotificationMessage::Level_Warning, "Master Science Arm Disconnected", "The master science arm is not connected.");
                    }
                });
                connect(_self->_scienceArmControlSystem, &ScienceArmControlSystem::sciencePackageControllerDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Science System Disconnected", "The science package control system running on the rover computer has either exited, crashed, or lost connection.");
                });
                connect(_self->_scienceArmControlSystem, &ScienceArmControlSystem::sciencePackageDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Science Package Disconnected", "The science package microcontroller on the rover has either exited, crashed, or lost connection.");
                });
                connect(_self->_scienceArmControlSystem, &ScienceArmControlSystem::flirDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Flir System Disconnected", "The Flir control system running on the rover has either exited, crashed, or lost connection.");
                });
                connect(_self->_scienceArmControlSystem, &ScienceArmControlSystem::lidarDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "LIDAR System Disconnected", "The LIDAR control system running on the rover has either exited, crashed, or lost connection.");
                });
            }

            //
            // Connect signals related to the science camera control system
            //
            if (_self->_scienceCameraControlSystem)
            {
                connect(_self->_scienceCameraControlSystem, &ScienceCameraControlSystem::sciencePackageControllerDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Science System Disconnected", "The science package control system running on the rover computer has either exited, crashed, or lost connection.");
                });
                connect(_self->_scienceCameraControlSystem, &ScienceCameraControlSystem::sciencePackageDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Science Package Disconnected", "The science package microcontroller on the rover has either exited, crashed, or lost connection.");
                });
                connect(_self->_scienceCameraControlSystem, &ScienceCameraControlSystem::flirDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "Flir System Disconnected", "The Flir control system running on the rover has either exited, crashed, or lost connection.");
                });
                connect(_self->_scienceCameraControlSystem, &ScienceCameraControlSystem::lidarDisconnected, _self, []()
                {
                    _self->_mainWindowController->notify(NotificationMessage::Level_Error, "LIDAR System Disconnected", "The LIDAR control system running on the rover has either exited, crashed, or lost connection.");
                });
            }

            //
            // Connect to key events. This is where all the keybinding is done
            //
            connect(_self->_mainWindowController, &MainWindowController::keyPressed, _self, [](int key)
            {
                switch (key)
                {
                case Qt::Key_F1:
                    _self->_mainWindowController->takeMainContentViewScreenshot();
                    break;
                case Qt::Key_1:
                    _self->_videoClient->stop(0);
                    break;
                case Qt::Key_Q:
                    _self->_videoClient->play(0, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_A:
                    _self->_videoClient->play(0, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_Z:
                    _self->_videoClient->play(0, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_2:
                    _self->_videoClient->stop(1);
                    break;
                case Qt::Key_W:
                    _self->_videoClient->play(1, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_S:
                    _self->_videoClient->play(1, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_X:
                    _self->_videoClient->play(1, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_3:
                    _self->_videoClient->stop(2);
                    break;
                case Qt::Key_E:
                    _self->_videoClient->play(2, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_D:
                    _self->_videoClient->play(2, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_C:
                    _self->_videoClient->play(2, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_4:
                    _self->_videoClient->stop(3);
                    break;
                case Qt::Key_R:
                    _self->_videoClient->play(3, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_F:
                    _self->_videoClient->play(3, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_V:
                    _self->_videoClient->play(3, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_5:
                    _self->_videoClient->stop(4);
                    break;
                case Qt::Key_T:
                    _self->_videoClient->play(4, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_G:
                    _self->_videoClient->play(4, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_B:
                    _self->_videoClient->play(4, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_6:
                    _self->_videoClient->stop(5);
                    break;
                case Qt::Key_Y:
                    _self->_videoClient->play(5, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_H:
                    _self->_videoClient->play(5, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_N:
                    _self->_videoClient->play(5, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_7:
                    _self->_videoClient->stop(6);
                    break;
                case Qt::Key_U:
                    _self->_videoClient->play(6, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_J:
                    _self->_videoClient->play(6, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_M:
                    _self->_videoClient->play(6, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_8:
                    _self->_videoClient->stop(7);
                    break;
                case Qt::Key_I:
                    _self->_videoClient->play(7, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_K:
                    _self->_videoClient->play(7, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_Comma:
                    _self->_videoClient->play(7, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_9:
                    _self->_videoClient->stop(8);
                    break;
                case Qt::Key_O:
                    _self->_videoClient->play(8, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_L:
                    _self->_videoClient->play(8, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_Period:
                    _self->_videoClient->play(8, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_0:
                    _self->_videoClient->stop(9);
                    break;
                case Qt::Key_P:
                    _self->_videoClient->play(9, _self->_mediaProfileSettingsModel->getVideoProfile(0));
                    break;
                case Qt::Key_Semicolon:
                    _self->_videoClient->play(9, _self->_mediaProfileSettingsModel->getVideoProfile(1));
                    break;
                case Qt::Key_Slash:
                    _self->_videoClient->play(9, _self->_mediaProfileSettingsModel->getVideoProfile(2));
                    break;
                case Qt::Key_Escape:
                    _self->_videoClient->stopAll();
                    _self->_audioClient->stop();
                    break;
                case Qt::Key_Minus:
                    _self->_audioClient->stop();
                    break;
                case Qt::Key_BracketLeft:
                    _self->_audioClient->play(_self->_mediaProfileSettingsModel->getAudioProfile(0));
                    break;
                case Qt::Key_Apostrophe:
                    _self->_audioClient->play(_self->_mediaProfileSettingsModel->getAudioProfile(1));
                    break;
                case Qt::Key_Up:
                    _self->_mainWindowController->selectViewAbove();
                    break;
                case Qt::Key_Down:
                    _self->_mainWindowController->selectViewBelow();
                    break;
                case Qt::Key_Return:
                    _self->_mainWindowController->dismissNotification();
                    break;
                case Qt::Key_Left:
                    _self->_mainWindowController->toggleSidebar();
                    break;
                default: qDebug() << key; break;
                }
            });

            //
            // Connect to gamepad button events. This is where all the gamepad button binding is done
            //
            connect(_self->_gamepadController, &GamepadController::buttonPressed, _self, [](SDL_GameControllerButton btn, bool isPressed)
            {
                switch (btn)
                {
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    if (_self->_driveControlSystem)
                    {
                        _self->_driveControlSystem->setLimit(isPressed ? 1.0 : _self->_settingsModel->getDrivePowerLimit());
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    if (_self->_driveControlSystem)
                    {
                        if (isPressed)
                        {
                            _self->_driveControlSystem->unfold();
                        }
                        else
                        {
                            _self->_driveControlSystem->stop();
                        }
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    if (isPressed) _self->_mainWindowController->selectViewBelow();
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    if (isPressed) _self->_mainWindowController->selectViewAbove();
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    if (isPressed) _self->_mainWindowController->toggleSidebar();
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    if (isPressed) _self->_mainWindowController->dismissNotification();
                    break;
                default: break;
                }
            });

            LOG_I(LogTag, "Initialization complete");
        });
    }
}

//
// Getters
//

QString MainController::getId()
{
    return _self->_mcId;
}

//
// Misc private functions
//

QString MainController::genId()
{
    const QString chars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    qsrand(QTime::currentTime().msec());
    QString id;
    switch (_settingsModel->getConfiguration())
    {
    case SettingsModel::ArmOperatorConfiguration:
        id = "mc_arm";
        break;
    case SettingsModel::DriverConfiguration:
        id = "mc_drive";
        break;
    case SettingsModel::CameraOperatorConfiguration:
        id = "mc_camera";
        break;
    case SettingsModel::ScienceArmOperatorConfiguration:
        id = "science_arm_operator";
        break;
    case SettingsModel::ScienceCameraOperatorConfiguration:
        id = "science_camera_operator";
        break;
    case SettingsModel::ObserverConfiguration:
        id = "mc_observer_";
        for(int i = 0; i < 12; ++i) {
            id.append(chars.at(qrand() % chars.length()));
        }
        return id;
    }

    return id;
}

} // namespace Soro
