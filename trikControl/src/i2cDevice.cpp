/* Copyright 2018 Aleksey Fefelov and CyberTech Labs Ltd.
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

#include <trikHal/mspI2cInterface.h>
#include "i2cDevice.h"
#include "i2cCommunicatorInterface.h"
#include "QsLog.h"

using namespace trikControl;

I2cDevice::I2cDevice(const trikKernel::Configurer &configurer,
		     trikHal::MspI2cInterface *i2c, int bus, int address)
	: mState("I2cDevice")
	, mInterface(i2c)
	, mCommunicator(new MspI2cCommunicator(configurer, *i2c, bus, address))

{
	mState.ready();
}

I2cDevice::~I2cDevice() {

}

I2cDevice::Status I2cDevice::status() const
{
	return combine(*mCommunicator, mState.status());
}

int I2cDevice::send(int reg, int value, const QString &mode)
{
	if (status() != DeviceInterface::Status::ready) {
		return -1;
	}

	QByteArray command;

	command.append(static_cast<char>(reg & 0xFF));
	command.append(static_cast<char>((reg >> 8) & 0xFF));
	command.append(static_cast<char>(value & 0xFF));

	if (mode == "b") {
		return mCommunicator->send(command);
	}

	if (mode == "w") {
		command.append(static_cast<char>((value >> 8) & 0xFF));
		return mCommunicator->send(command);
	}

	return -1;
}

int I2cDevice::read(int reg, const QString &mode) {
	QByteArray command;

	command.append(static_cast<char>(reg & 0xFF));
	command.append(static_cast<char>((reg >> 8) & 0xFF));

	if (mode == "w") {
		command.append(static_cast<char>(0x02));
		command.append(static_cast<char>(0x00));
		return mCommunicator->read(command);
	}

	command.append(static_cast<char>(0x01));
	command.append(static_cast<char>(0x00));
	return mCommunicator->read(command);
}

QVector<uint8_t> I2cDevice::readX(int reg, int size) {

	QByteArray command;

	command.append(static_cast<char>(reg & 0xFF));
	command.append(static_cast<char>((reg >> 8) & 0xFF));

	command.append(static_cast<char>(size & 0xFF));
	command.append(static_cast<char>((size >> 8) & 0xFF));
	return mCommunicator->readX(command);
}

int I2cDevice::transfer(
		const QVector<trikControl::I2cDeviceInterface::Message> &vector) {
	return mCommunicator->transfer(vector);
}
