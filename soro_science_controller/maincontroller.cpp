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

#include "soro_core/constants.h"
#include "soro_core/logger.h"

#define LogTag "MainController"

namespace Soro {

MainController *MainController::_self = nullptr;

MainController::MainController(QObject *parent) : QObject(parent) { }

void MainController::panic(QString tag, QString message)
{
    LOG_E(LogTag, QString("panic(): %1: %2").arg(tag, message));
    LOG_I(LogTag, "Committing suicide...");
    delete _self;
    LOG_I(LogTag, "Exiting with code 1");
    exit(1);
}

void MainController::init(QCoreApplication *app)
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

            LOG_I(LogTag, "Initialization complete");
        });
    }
}

//
// Getters
//

QString MainController::getId()
{
    return "science_controller";
}

} // namespace Soro
