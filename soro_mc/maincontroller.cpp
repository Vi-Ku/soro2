#include "maincontroller.h"

#include <QTimer>
#include <QMessageBox>
#include <QtWebEngine>

#include <SDL2/SDL.h>

#include <Qt5GStreamer/QGst/Init>
#include <Qt5GStreamer/QGlib/Error>
#include <Qt5GStreamer/QGst/Pipeline>
#include <Qt5GStreamer/QGst/Element>
#include <Qt5GStreamer/QGst/ElementFactory>
#include <Qt5GStreamer/QGst/Bin>

#include "qquickgstreamersurface.h"

#include <ros/ros.h>

#define LogTag "MainController"

namespace Soro {

MainController *MainController::_self = nullptr;

MainController::MainController(QObject *parent) : QObject(parent)
{
}

void MainController::panic(QString message)
{
    QMessageBox::critical(0, "Mission Control", message);
    logFatal(LogTag, QString("panic(): %1").arg(message));
    QCoreApplication::exit(1);
}

void MainController::init(QApplication *app)
{
    if (_self)
    {
        logError(LogTag, "init() called when already initialized");
    }
    else
    {
        logInfo(LogTag, "Initializing");
        _self = new MainController(app);
        QTimer::singleShot(0, _self, &MainController::initInternal);
    }
}

void MainController::initInternal()
{
    //
    // Create a unique identifier for this mission control, it is mainly used as a unique node name for ROS
    //
    _mcId = genId();
    logInfo(LogTag, QString("Mission Control ID is: %1").arg(_mcId).toLocal8Bit().constData());

    //
    // Create the settings model and load the main settings file
    //
    logInfo(LogTag, "Loading settings...");
    _settingsModel = new SettingsModel;
    if (!_settingsModel->load())
    {
        panic(QString("Failed to load settings file: %1").arg(_settingsModel->errorString()));
        return;
    }

    //
    // Create camera settings model to load camera configuration
    //
    logInfo(LogTag, "Loading camera settings...");
    _cameraSettingsModel = new CameraSettingsModel;
    if (!_cameraSettingsModel->load())
    {
        panic(QString("Failed to load camera settings file: %1").arg(_cameraSettingsModel->errorString()));
        return;
    }

    //
    // Initialize Qt5GStreamer, must be called before anything else is done with it
    //
    logInfo(LogTag, "Initializing QtGstreamer...");
    try
    {
        QGst::init();
    }
    catch (QGlib::Error e)
    {
        panic(QString("Failed to initialize QtGStreamer:  %1").arg(e.message()));
        return;
    }

    //
    // Initiaize the Qt webengine (i.e. blink web engine) for use in QML
    //
    logInfo(LogTag, "Initializing QtWebEngine...");
    QtWebEngine::initialize();

    //
    // Initialize SDL (for gamepad reading)
    //
    logInfo(LogTag, "Initializing SDL...");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) != 0)
    {
        panic(QString("Failed to initialize SDL:  %1").arg(SDL_GetError()));
        return;
    }

    //
    // Load the SDL gamepad map file
    // This map file allows SDL to know which button/axis (i.e. "Left X Axis") corresponds
    // to the raw reading from the controller (i.e. "Axis 0")
    //
    logInfo(LogTag, "Initializing SDL gamepad map...");
    QFile gamepadMap(":/config/gamecontrollerdb.txt"); // Loads from assets.qrc
    gamepadMap.open(QIODevice::ReadOnly);
    while (gamepadMap.bytesAvailable())
    {
        if (SDL_GameControllerAddMapping(gamepadMap.readLine().data()) == -1)
        {
            panic(QString("Failed to apply SDL gamepad map: %1").arg(SDL_GetError()));
            gamepadMap.close();
            return;
        }
    }
    gamepadMap.close();

    //
    // Create the QML application engine
    //
    logInfo(LogTag, "Initializing QML engine...");
    qmlRegisterType<QQuickGStreamerSurface>("Soro", 1, 0, "GStreamerSurface");
    _qmlEngine = new QQmlEngine(this);

    //
    // Create the GamepadController instance
    //
    _gamepadController = new GamepadController(this);

    //
    // Create the main UI
    //
    QQmlComponent qmlComponent(_qmlEngine, QUrl("qrc:/main.qml"));
    _window = qobject_cast<QQuickWindow*>(qmlComponent.create());
    if (!qmlComponent.errorString().isEmpty() || !_window)
    {
        // There was an error creating the QML window. This is most likely due to a QML syntax error
        panic(QString("Failed to create QML window:  %1").arg(qmlComponent.errorString()));
    }

    _gamepadController = new GamepadController(this);
    connect(_gamepadController, &GamepadController::buttonPressed, this, &MainController::gamepadButtonPressed);

    _rosConnectionController = new RosConnectionController(this);

    _driveSystem = new DriveControlSystem(this);

    _vidSurface1 = qvariant_cast<QQuickGStreamerSurface*>(_window->property("mainVideoSurface"));
    _vidSurface2 = qvariant_cast<QQuickGStreamerSurface*>(_window->property("sideVideoSurface1"));
    _vidSurface3 = qvariant_cast<QQuickGStreamerSurface*>(_window->property("sideVideoSurface2"));

    ///////////////////////////
    //////  TESTING  //////////

    QGst::PipelinePtr pipeline1 = QGst::Pipeline::create("pipeline1");
    QGst::BinPtr source1 = QGst::Bin::fromDescription("videotestsrc pattern=snow ! videoconvert");
    _sink1 = QGst::ElementFactory::make("qt5videosink");
    pipeline1->add(source1, _sink1);
    source1->link(_sink1);
    _vidSurface1->setSink(_sink1);
    _vidSurface3->setSink(_sink1);
    pipeline1->setState(QGst::StatePlaying);

    QGst::PipelinePtr pipeline2 = QGst::Pipeline::create("pipeline2");
    QGst::BinPtr source2 = QGst::Bin::fromDescription("videotestsrc ! videoconvert");
    //QGst::BinPtr source2 = QGst::Bin::fromDescription("udpsrc port=5502 ! application/x-rtp,media=video,encoding-name=JPEG,payload=26 ! rtpjpegdepay ! jpegdec ! videoconvert");
    _sink2 = QGst::ElementFactory::make("qt5videosink");
    pipeline2->add(source2, _sink2);
    source2->link(_sink2);
    _vidSurface2->setSink(_sink2);
    pipeline2->setState(QGst::StatePlaying);


    ///////////////////////////
    ///////////////////////////
    logInfo(LogTag, "Initialization complete");
}

void MainController::logDebug(QString tag, QString message)
{
    if (_self)
    {
        _self->log(LogLevelDebug, tag, message);
    }
}

void MainController::logInfo(QString tag, QString message)
{
    if (_self)
    {
        _self->log(LogLevelInfo, tag, message);
    }
}

void MainController::logWarning(QString tag, QString message)
{
    if (_self)
    {
        _self->log(LogLevelWarning, tag, message);
    }
}

void MainController::logError(QString tag, QString message)
{
    if (_self)
    {
        _self->log(LogLevelError, tag, message);
    }
}

void MainController::logFatal(QString tag, QString message)
{
    if (_self)
    {
        _self->log(LogLevelFatal, tag, message);
    }
}

void MainController::log(LogLevel level, QString tag, QString message) {
    if (ros::isInitialized()) {
        const char* formatted = QString("[%1] %2").arg(tag, message).toLocal8Bit().constData();
        switch (level) {
        case LogLevelDebug:
            ROS_DEBUG(formatted);
            break;
        case LogLevelInfo:
            ROS_INFO(formatted);
            break;
        case LogLevelWarning:
            ROS_WARN(formatted);
            break;
        case LogLevelError:
            ROS_ERROR(formatted);
            break;
        case LogLevelFatal:
            ROS_FATAL(formatted);
            break;
        }
    }
    else {
        const char* formatted = QString("[No ROS] [%1] %2").arg(tag, message).toLocal8Bit().constData();
        switch (level) {
        case LogLevelDebug:
            qDebug(formatted);
            break;
        case LogLevelInfo:
            qInfo(formatted);
            break;
        case LogLevelWarning:
            qWarning(formatted);
            break;
        case LogLevelError:
            qCritical(formatted);
            break;
        case LogLevelFatal:
            qFatal(formatted);
            break;
        }
    }
}

void MainController::gamepadButtonPressed(SDL_GameControllerButton button, bool pressed) {
    if ((button == SDL_CONTROLLER_BUTTON_A) && pressed) {
        if (_window->property("sidebarState").toString() == "visible") {
            _window->setProperty("sidebarState", QVariant("hidden"));
        }
        else {
            _window->setProperty("sidebarState", QVariant("visible"));
        }
    }
    else if ((button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) && pressed) {
        //_vidSurface1->clearSink();
        if (_sinkConfig == 1) {
            _vidSurface1->setProperty("visible", QVariant(true));
            _sinkConfig = 2;
            _vidSurface1->setSink(_sink2);
            _window->setProperty("view3Selected", QVariant(false));
            _window->setProperty("view1Selected", QVariant(true));
            _window->setProperty("view2Selected", QVariant(false));
        }
        else if (_sinkConfig == 2) {
            _sinkConfig = 3;
            _vidSurface1->setSink(_sink1);
            _window->setProperty("view3Selected", QVariant(false));
            _window->setProperty("view2Selected", QVariant(true));
            _window->setProperty("view1Selected", QVariant(false));
        }
        else if (_sinkConfig == 3) {
            _sinkConfig = 1;
            _vidSurface1->setProperty("visible", QVariant(false));
            _window->setProperty("view3Selected", QVariant(true));
            _window->setProperty("view2Selected", QVariant(false));
            _window->setProperty("view1Selected", QVariant(false));
        }
    }
    else if ((button == SDL_CONTROLLER_BUTTON_Y) && pressed) {
        _window->setProperty("fullscreen", !_window->property("fullscreen").toBool());
    }
}

QString MainController::getMissionControlId()
{
    return _self->_mcId;
}

GamepadController* MainController::getGamepadController()
{
    return _self->_gamepadController;
}

SettingsModel* MainController::getSettingsModel()
{
    return _self->_settingsModel;
}

QString MainController::genId()
{
    const QString chars("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    qsrand(QTime::currentTime().msec());
    QString randomString = "mc_";
    for(int i = 0; i < 12; ++i) {
        randomString.append(chars.at(qrand() % chars.length()));
    }
    return randomString;
}

} // namespace Soro
