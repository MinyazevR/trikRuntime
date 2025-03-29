/* Copyright 2013 - 2015 Yurii Litvinov and CyberTech Labs Ltd.
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

#include <QtCore/QString>
#include <QtCore/QMutex>

#include "mspCommunicatorInterface.h"

namespace trikKernel {
class Configurer;
}

namespace trikHal {
class MspI2cInterface;
}

namespace trikControl {

/// Provides direct interaction with I2C device.
class MspI2cCommunicator : public MspCommunicatorInterface
{
public:
	/// Constructor.
	/// @param configurer - contains preparsed XML configuration.
	MspI2cCommunicator(const trikKernel::Configurer &configurer, trikHal::MspI2cInterface &i2c);

	/// The constructor with two parameters is used to access a device on bus 2
	/// with address 0x48 (see system-config.xml) and effectively uses it to get
	/// information about the battery and so on.
	/// However, there are i2c ports for external peripherals that require a device address.
	MspI2cCommunicator(const trikKernel::Configurer &configurer, trikHal::MspI2cInterface &i2c
				, uint8_t bus, uint8_t deviceId);

	~MspI2cCommunicator() override;

	/// Send data to current device, if it is connected.
	void send(uint16_t deviceAddress, uint16_t value, bool isWord) override;

	/// Reads data by given I2C command number and returns the result.
	QVariant read(uint16_t deviceAddress, uint16_t numberOfBytes) override;

	Status status() const override;

private:
	void disconnect();

	QMutex mLock;
	trikHal::MspI2cInterface &mI2c;
	DeviceState mState;
};

}
