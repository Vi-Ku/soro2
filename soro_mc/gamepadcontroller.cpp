/*
 * Copyright 2017 Michael Hillebrand <michaelhillebrand@ou.edu>
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

#include "gamepadcontroller.h"
#include "maincontroller.h"
#include "soro_core/logger.h"
#include <climits>

#define LOG_TAG "GamepadController"

namespace Soro {

GamepadController::GamepadController(QObject *parent) : QObject(parent) {
    // Start poll timer immediately
    _deadzone = 0.05;
    _gameController = nullptr;

    _pollTimer.setInterval(50); // Default poll
    _searchTimer.setInterval(1000);

    connect(&_pollTimer, &QTimer::timeout, this, [this]()
    {
        SDL_GameControllerUpdate();
        if (_gameController && SDL_GameControllerGetAttached(_gameController))
        {
            // Update gamepad data
            updateIfChangedAxis(SDL_CONTROLLER_AXIS_LEFTX, &_axisLeftX);
            updateIfChangedAxis(SDL_CONTROLLER_AXIS_LEFTY, &_axisLeftY);
            updateIfChangedAxis(SDL_CONTROLLER_AXIS_RIGHTX, &_axisRightX);
            updateIfChangedAxis(SDL_CONTROLLER_AXIS_RIGHTY, &_axisRightY);
            updateIfChangedAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, &_axisLeftTrigger);
            updateIfChangedAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, &_axisRightTrigger);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_A, &_buttonA);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_B, &_buttonB);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_X, &_buttonX);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_Y, &_buttonY);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, &_buttonLeftShoulder);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, &_buttonRightShoulder);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_START, &_buttonStart);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_BACK, &_buttonBack);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_LEFTSTICK, &_buttonLeftStick);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_RIGHTSTICK, &_buttonRightStick);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_DPAD_UP, &_dpadUp);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT, &_dpadLeft);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN, &_dpadDown);
            updateIfChangedButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, &_dpadRight);
            Q_EMIT poll();
        }
        else
        {
            setGamepad(nullptr);
            _pollTimer.stop();
            _searchTimer.start();
        }
    });

    connect(&_searchTimer, &QTimer::timeout, this, [this]()
    {
        SDL_GameControllerUpdate();
        LOG_I(LOG_TAG, QString("Searching for useable controllers (%1 candidates)...").arg(SDL_NumJoysticks()));
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
        {
            if (SDL_IsGameController(i))
            {
                SDL_GameController *controller = SDL_GameControllerOpen(i);
                if (controller) {
                    //this gamepad will do
                    setGamepad(controller);
                    _searchTimer.stop();
                    _pollTimer.start();
                    break;
                }
                SDL_GameControllerClose(controller);
                LOG_W(LOG_TAG, "The gamepad has been disconnected");
            }
        }
    });

    _searchTimer.start();
}

float GamepadController::convertToFloatWithDeadzone(qint16 value, float deadzone)
{
    float val = (float)(value)/(float)(INT16_MAX);
    return qAbs(val) > deadzone ? val : 0.0;
}

SDL_GameController* GamepadController::getGamepad()
{
    return _gameController;
}

QString GamepadController::getGamepadName() const
{
    return _gamepadName;
}

bool GamepadController::isGamepadConnected() const {
    return _gameController != nullptr;
}

bool GamepadController::getButtonPressed(SDL_GameControllerButton button) const
{
    return SDL_GameControllerGetButton(_gameController, button);
}

float GamepadController::getAxisValue(SDL_GameControllerAxis axis) const
{
    return convertToFloatWithDeadzone(SDL_GameControllerGetAxis(_gameController, axis), _deadzone);
}

float GamepadController::getDeadzone() const
{
    return _deadzone;
}

void GamepadController::setDeadzone(float deadzone)
{
    _deadzone = qMax(qMin(deadzone, 0.5f), 0.0f);
}

void GamepadController::setPollInterval(int rate)
{
    _pollTimer.setInterval(rate);
}

int GamepadController::getPollInterval() const
{
    return _pollTimer.interval();
}

void GamepadController::updateIfChangedAxis(SDL_GameControllerAxis axis, qint16 *currentValue)
{
    qint16 temp = SDL_GameControllerGetAxis(_gameController, axis);
    if (*currentValue != temp)
    {
        *currentValue = temp;
        Q_EMIT axisChanged(axis, convertToFloatWithDeadzone(temp, _deadzone));
    }
}

void GamepadController::updateIfChangedButton(SDL_GameControllerButton button, bool *currentValue)
{
    bool temp = SDL_GameControllerGetButton(_gameController, button) > 0;
    if (*currentValue != temp)
    {
        *currentValue = temp;
        Q_EMIT buttonPressed(button, temp);
    }
}

void GamepadController::setGamepad(SDL_GameController *controller)
{
    if (_gameController != controller)
    {
        _gameController = controller;
        _gamepadName = _gameController ? QString(SDL_GameControllerName(_gameController)) : "";
        LOG_I(LOG_TAG, "Active controller is \'" + _gamepadName + "\'");
        Q_EMIT gamepadChanged(isGamepadConnected(), _gamepadName);
    }
}

} // namespace Soro
