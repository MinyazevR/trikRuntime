/* Copyright 2025 CyberTech Labs Ltd.
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

#include <QtCore/QMutex>
#include <QtCore/QString>

#include "deviceState.h"
#include "peripheryCommunicatorInterface.h"

namespace trikKernel {
class Configurer;
}

namespace trikHal {
class NrfI2cInterface;
}

namespace trikControl {

/// Communicator with the nRF52833 periphery module over I2C.
/// Implements PeripheryCommunicatorInterface for environments without USB.
class NrfI2cCommunicator : public PeripheryCommunicatorInterface
{
public:
	NrfI2cCommunicator(const trikKernel::Configurer &configurer, trikHal::NrfI2cInterface &i2c);

	NrfI2cCommunicator(const trikKernel::Configurer &configurer, trikHal::NrfI2cInterface &i2c
			, uint8_t bus, uint8_t deviceId);

	~NrfI2cCommunicator() override;

	int send(const QByteArray &data) override;
	int read(const QByteArray &data) override;

	/// Reads raw bytes via I2C (used by I2cDevice).
	QVector<uint8_t> readX(const QByteArray &data);

	Status status() const override;

private:
	void disconnect();

	QMutex mLock;
	trikHal::NrfI2cInterface &mI2c;
	DeviceState mState;
};

}