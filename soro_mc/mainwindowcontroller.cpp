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

#include "mainwindowcontroller.h"
#include "maincontroller.h"
#include "qmlgstreamerglitem.h"
#include "qmlgstreamerpainteditem.h"
#include "soro_core/constants.h"
#include "soro_core/logger.h"
#include "soro_core/namegen.h"
#include "soro_core/compassmessage.h"
#include "soro_core/atmospheresensormessage.h"
#include "soro_core/gpsmessage.h"
#include "soro_core/switchmessage.h"
#include "soro_core/gstreamerutil.h"

#include <QPixmap>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickItemGrabResult>

#include <Qt5GStreamer/QGst/ElementFactory>

#define LogTag "MainWindowController"

namespace Soro {

MainWindowController::MainWindowController(QQmlEngine *engine, const SettingsModel *settings, const MediaProfileSettingsModel *mediaProfileSettings,
                                           const CameraSettingsModel *cameraSettings, QObject *parent) : QObject(parent)
{
    _cameraSettings = cameraSettings;
    _mediaProfileSettings = mediaProfileSettings;
    _settings = settings;
    _lastSatellites = 0;
    _lastLat = _lastLng = _lastCompass = _lastElevation = 0.0;
    _lastTemperature = _lastHumidity = _lastWindSpeed = _lastWindDirection = 0.0;
    _logAtmosphere = false;

    QQmlComponent qmlComponent(engine, QUrl("qrc:/qml/main.qml"));
    _window = qobject_cast<QQuickWindow*>(qmlComponent.create());
    if (!qmlComponent.errorString().isEmpty() || !_window)
    {
        // There was an error creating the QML window. This is most likely due to a QML syntax error
        MainController::panic(LogTag, QString("Failed to create QML window:  %1").arg(qmlComponent.errorString()));
    }
    //
    // Setup the camera views in the UI
    //
    int videoCount = cameraSettings->getCameraCount();
    if (videoCount > 10)
    {
        MainController::panic(LogTag, "UI does not support more than 10 different camera views");
    }
    _window->setProperty("videoCount", videoCount);

    // Set map image
    _mapView = qvariant_cast<MapViewImpl*>(_window->property("mapViewImpl"));
    _mapView->setImage(QCoreApplication::applicationDirPath() + "/../maps/" + _settings->getMapImage());
    _mapView->setStartCoordinate(_settings->getMapStartCoordinates());
    _mapView->setEndCoordinate(_settings->getMapEndCoordinates());

    for (int i = 0; i < videoCount; i++)
    {
        // Set camera name
        //_window->setProperty(QString("video%1Name").arg(i).toLatin1().constData(), cameraSettings->getCamera(i).name);
        QMetaObject::invokeMethod(_window, "setVideoName", Q_ARG(QVariant, i), Q_ARG(QVariant, cameraSettings->getCamera(i).name));
    }

    _window->setProperty("selectedView", "video0");

    switch (settings->getConfiguration())
    {
    case SettingsModel::ArmOperatorConfiguration:
        _window->setProperty("configuration", "Arm Operator");
        break;
    case SettingsModel::DriverConfiguration:
        _window->setProperty("configuration", "Driver");
        break;
    case SettingsModel::ScienceArmOperatorConfiguration:
        _window->setProperty("configuration", "Science Arm Operator");
        break;
    case SettingsModel::ScienceCameraOperatorConfiguration:
        _window->setProperty("configuration", "Science Camera Operator");
        break;
    case SettingsModel::CameraOperatorConfiguration:
        _window->setProperty("configuration", "Camera Operator");
        break;
    case SettingsModel::ObserverConfiguration:
        _window->setProperty("configuration", "Observer");
        break;
    }

    connect(_window, SIGNAL(keyPressed(int)), this, SIGNAL(keyPressed(int)));

    //
    // Setup MQTT
    //
    LOG_I(LogTag, "Creating MQTT client...");
    _mqtt = new QMQTT::Client(settings->getMqttBrokerAddress(), SORO_NET_MQTT_BROKER_PORT, this);
    connect(_mqtt, &QMQTT::Client::received, this, &MainWindowController::onMqttMessage);
    connect(_mqtt, &QMQTT::Client::connected, this, &MainWindowController::onMqttConnected);
    connect(_mqtt, &QMQTT::Client::disconnected, this, &MainWindowController::onMqttDisconnected);
    _mqtt->setClientId(MainController::getId() + "_mainwindowcontroller");
    _mqtt->setAutoReconnect(true);
    _mqtt->setAutoReconnectInterval(1000);
    _mqtt->connectToHost();
}

void MainWindowController::setSpectrometer404Reading(QList<quint16> readings)
{
   _window->setProperty("spectrometer404Readings", readings);
}

void MainWindowController::setSpectrometerWhiteReading(QList<quint16> readings)
{
   _window->setProperty("spectrometerWhiteReadings", readings);
}

void MainWindowController::setO2GasReading(quint32 ppm)
{
    _window->setProperty("o2GasPpm", raw);
}

void MainWindowController::setCO2GasReading(quint32 ppm)
{
    _window->setProperty("co2GasPpm", raw);
}

void MainWindowController::setMQ2GasReading(quint16 raw)
{
    _window->setProperty("mq2GasReading", raw);
}

void MainWindowController::setMQ4GasReading(quint16 raw)
{
    _window->setProperty("mq4GasReading", raw);
}

void MainWindowController::setMQ5GasReading(quint16 raw)
{
    _window->setProperty("mq5GasReading", raw);
}

void MainWindowController::setMQ6GasReading(quint16 raw)
{
    _window->setProperty("mq6GasReading", raw);
}

void MainWindowController::setMQ7GasReading(quint16 raw)
{
    _window->setProperty("mq7GasReading", raw);
}

void MainWindowController::setMQ9GasReading(quint16 raw)
{
    _window->setProperty("mq9GasReading", raw);
}

void MainWindowController::setMQ135GasReading(quint16 raw)
{
    _window->setProperty("mq135GasReading", raw);
}

void MainWindowController::onMqttConnected()
{
    LOG_I(LogTag, "Connected to MQTT broker");
    _mqtt->subscribe("notification", 2);
    _mqtt->subscribe("compass", 0);
    _mqtt->subscribe("gps", 0);
    _mqtt->subscribe("atmosphere", 0);
    _mqtt->subscribe("atmosphere_switch", 2);
    Q_EMIT mqttConnected();
}

void MainWindowController::onMqttDisconnected()
{
    LOG_W(LogTag, "Disconnected from MQTT broker");
    Q_EMIT mqttDisconnected();
}

void MainWindowController::takeMainContentViewScreenshot()
{
    QSharedPointer<QQuickItemGrabResult> result = qvariant_cast<QQuickItem*>(_window->property("mainContentView"))->grabToImage();

    connect(result.data(), &QQuickItemGrabResult::ready, this, [this, result]()
    {
        QString name = NameGen::generate(1);
        if (result.data()->image().save(QCoreApplication::applicationDirPath() + "/../screenshots/" + name + ".png"))
        {
            // Save our current position and atmosphere data to go along with the screenshot
            QFile file(QCoreApplication::applicationDirPath() + "/../screenshots/" + name + ".txt");
            if (file.open(QIODevice::WriteOnly))
            {
                QTextStream stream(&file);
                stream << "Time:\t\t" << QDateTime::currentDateTime().toString(Qt::SystemLocaleLongDate) << "\n\n"
                       << "Heading:\t" << QString::number(_lastCompass, 'f', 2) << "\n"
                       << "Latitude:\t" << QString::number(_lastLat, 'f', 7) << "\n"
                       << "Longitude:\t" << QString::number(_lastLng, 'f', 7) << "\n"
                       << "Elevation:\t" << QString::number(_lastElevation, 'f', 3) << "\n"
                       << "Satellites:\t" << QString::number(_lastSatellites) << "\n\n";
                if (_logAtmosphere)
                {
                    stream << "Temperature:\t" << QString::number(_lastTemperature, 'f', 2) << "\n"
                           << "Humidity:\t" << QString::number(_lastHumidity, 'f', 2) << "\n"
                           << "Wind Direction:\t" << QString::number(_lastWindDirection, 'f', 2) << "\n"
                           << "Wind Speed:\t" << QString::number(_lastWindSpeed, 'f', 2);
                }
                else
                {
                    stream << "Atmospheric data was not available when this screenshot was taken.";
                }
                file.close();
            }
            else
            {
                LOG_E(LogTag, "Cannot open file to log position data of screenshot");
            }

            LOG_I(LogTag, "Screenshot saved");
            notify(NotificationMessage::Level_Info, "Screenshot Saved", "A screenshot has been saved to \"" + name + ".png\" in the screenshots folder.");
        }
        else
        {
            LOG_E(LogTag, "Error saving screenshot");
            notify(NotificationMessage::Level_Error, "Screenshot Error", "Could not take a screenshot of the active view. Something is very wrong.");
        }
    });
}

void MainWindowController::onMqttMessage(const QMQTT::Message &msg)
{
    if (msg.topic() == "notification")
    {
        NotificationMessage notificationMsg(msg.payload());

        switch (notificationMsg.level)
        {
        case NotificationMessage::Level_Error:
            LOG_E(LogTag, QString("Received error notification: %1 - %2 ").arg(notificationMsg.title, notificationMsg.message));
            break;
        case NotificationMessage::Level_Warning:
            LOG_W(LogTag, QString("Received warning notification: %1 - %2 ").arg(notificationMsg.title, notificationMsg.message));
            break;
        case NotificationMessage::Level_Info:
            LOG_I(LogTag, QString("Received info notification: %1 - %2 ").arg(notificationMsg.title, notificationMsg.message));
            break;
        }
    }
    else if (msg.topic() == "gps")
    {
        GpsMessage gpsMsg(msg.payload());
        _window->setProperty("latitude", gpsMsg.location.latitude);
        _window->setProperty("longitude", gpsMsg.location.longitude);
        _window->setProperty("gpsSatellites", gpsMsg.satellites);
        _mapView->updateLocation(gpsMsg.location);
        _lastLat = gpsMsg.location.latitude;
        _lastLng = gpsMsg.location.longitude;
        _lastElevation = gpsMsg.elevation;
        _lastSatellites = gpsMsg.satellites;
    }
    else if (msg.topic() == "compass")
    {
        CompassMessage compassMsg(msg.payload());
        _window->setProperty("compassHeading", compassMsg.heading);
        _lastCompass = compassMsg.heading;
        qDebug() << "Compass " << compassMsg.heading;
    }
    else if (msg.topic() == "atmosphere")
    {
        AtmosphereSensorMessage atmosphereMsg(msg.payload());
        _lastTemperature = atmosphereMsg.temperature;
        _lastHumidity = atmosphereMsg.humidity;
        _lastWindDirection = atmosphereMsg.windDirection;
        _lastWindSpeed = atmosphereMsg.windSpeed;
    }
    else if (msg.topic() == "atmosphere_switch")
    {
        SwitchMessage switchMsg(msg.payload());
        if (!switchMsg.on)
        {
            // This is needed so we don't log stale data for screenshots if atmosphere
            // sensors are off
            _logAtmosphere = switchMsg.on;
        }
    }
}

QVector<QGst::ElementPtr> MainWindowController::getVideoSinks()
{
    QVector<QGst::ElementPtr> sinks;
    for (int i = 0; i < _cameraSettings->getCameraCount(); ++i)
    {
        if (_settings->getEnableHwRendering())
        {
            // Item should be of the class QmlGStreamerGlItem
            QVariant returnValue;
            QMetaObject::invokeMethod(_window, "getVideoSurface", Q_RETURN_ARG(QVariant, returnValue), Q_ARG(QVariant, i));
            sinks.append(qvariant_cast<QmlGStreamerGlItem*>(returnValue)->videoSink());
        }
        else
        {
            // Item should be of the class QmlGStreamerPaintedItem
            QVariant returnValue;
            QMetaObject::invokeMethod(_window, "getVideoSurface", Q_RETURN_ARG(QVariant, returnValue), Q_ARG(QVariant, i));
            sinks.append(qvariant_cast<QmlGStreamerPaintedItem*>(returnValue)->videoSink());
        }
    }
    return sinks;
}

void MainWindowController::notify(NotificationMessage::Level level, QString title, QString message)
{
    QString typeString;
    switch (level)
    {
    case NotificationMessage::Level_Error:
        typeString = "error";
        break;
    case NotificationMessage::Level_Warning:
        typeString = "warning";
        break;
    case NotificationMessage::Level_Info:
        typeString = "info";
        break;
    }

    QMetaObject::invokeMethod(_window, "notify", Q_ARG(QVariant, typeString), Q_ARG(QVariant, title), Q_ARG(QVariant, message));
}

void MainWindowController::notifyAll(NotificationMessage::Level level, QString title, QString message)
{
    NotificationMessage msg;
    msg.level = level;
    msg.title = title;
    msg.message = message;

    // Publish this notification on the notification topic. We will get this message back,
    // since we are also subscribed to it, and that's when we'll show it from the onNewNotification() function
    _mqtt->publish(QMQTT::Message(_notificationMsgId++, "notification", msg, 2));
}

void MainWindowController::onConnectedChanged(bool connected)
{
    _window->setProperty("connected", connected);
}

void MainWindowController::onLatencyUpdated(quint32 latency)
{
    _window->setProperty("latency", latency);
}

void MainWindowController::onDataRateUpdated(quint64 rateFromRover)
{
    _window->setProperty("dataRateFromRover", rateFromRover);
}

void MainWindowController::toggleSidebar()
{
    QMetaObject::invokeMethod(_window, "toggleSidebar");
}

void MainWindowController::selectViewAbove()
{
    QMetaObject::invokeMethod(_window, "selectViewAbove");
}

void MainWindowController::selectViewBelow()
{
    QMetaObject::invokeMethod(_window, "selectViewBelow");
}

void MainWindowController::dismissNotification()
{
    QMetaObject::invokeMethod(_window, "dismissNotification");
}

void MainWindowController::onAudioProfileChanged(GStreamerUtil::AudioProfile profile)
{
    _window->setProperty("audioStreaming", profile.codec != GStreamerUtil::CODEC_NULL);
    _window->setProperty("audioProfile", _mediaProfileSettings->getAudioProfileName(profile));
}

void MainWindowController::onVideoProfileChanged(uint cameraIndex, GStreamerUtil::VideoProfile profile)
{
    QMetaObject::invokeMethod(_window, "setVideoIsStreaming", Q_ARG(QVariant, cameraIndex), Q_ARG(QVariant, profile.codec != GStreamerUtil::CODEC_NULL));
    QMetaObject::invokeMethod(_window, "setVideoProfileName", Q_ARG(QVariant, cameraIndex), Q_ARG(QVariant, _mediaProfileSettings->getVideoProfileName(profile)));
}

} // namespace Soro
