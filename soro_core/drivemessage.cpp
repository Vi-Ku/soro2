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

#include "drivemessage.h"

#include <QDataStream>

namespace Soro {

DriveMessage::DriveMessage() { }

DriveMessage::DriveMessage(const QByteArray &payload)
{
    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> wheelFL;
    stream >> wheelML;
    stream >> wheelBL;
    stream >> wheelFR;
    stream >> wheelMR;
    stream >> wheelBR;
}

DriveMessage::operator QByteArray() const
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << wheelFL
           << wheelML
           << wheelBL
           << wheelFR
           << wheelMR
           << wheelBR;

    return payload;
}

} // namespace Soro
