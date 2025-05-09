/* Copyright 2016 CyberTech Labs Ltd.
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
 * limitations under the License. */

#pragma once

#include <QtCore/QObject>

#include "deviceInterface.h"
#include "eventInterface.h"

#include <trikControl/trikControlDeclSpec.h>

namespace trikControl {

/// Generic event device.
class TRIKCONTROL_EXPORT EventDeviceInterface : public QObject, public DeviceInterface
{
	Q_OBJECT

public Q_SLOTS:
	/// Returns object that allows to selectively subscribe only to event with given code.
	virtual EventInterface *onEvent(int eventType) = 0;

Q_SIGNALS:
	/// Emitted when there is new event in an event file.
	/// @param event - type of the event.
	/// @param code - code of the event inside a type.
	/// @param value - value sent with the event.
	/// @param eventTime - time stamp of the event, in milliseconds from start of the Unix time (modulo 2^32).
	void on(int event, int code, int value, int eventTime);
};

}

Q_DECLARE_METATYPE(trikControl::EventDeviceInterface *)
