/* Copyright 2014 - 2015 CyberTech Labs Ltd.
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

#include <QtCore/QThread>
#include <QtCore/QScopedPointer>

#include "soundSensorInterface.h"
#include "deviceState.h"

namespace trikKernel {
class Configurer;
}

namespace trikHal {
class HardwareAbstractionInterface;
}

namespace trikControl {

class SoundSensorWorker;

/// Implementation of sound sensor for real robot.
class SoundSensor : public SoundSensorInterface
{
	Q_OBJECT

public:
	/// Constructor.
	/// @param port - port on which this sensor is configured.
	/// @param configurer - configurer object containing preparsed XML files with sensor parameters.
	SoundSensor(const QString &port, const trikKernel::Configurer &configurer
	            , trikHal::HardwareAbstractionInterface &hardwareAbstraction);

	~SoundSensor() override;

	Status status() const override;

public slots:
	void init(bool showOnDisplay) override;

	void detect() override;

	void volume(int volCoeff) override;

	QVector<int> read() override;

	void stop() override;

private:
	/// Sensor state.
	DeviceState mState;

	/// Worker object that handles sensor in separate thread.
	QScopedPointer<SoundSensorWorker> mSoundSensorWorker;

	/// Worker thread.
	QThread mWorkerThread;
};

}
