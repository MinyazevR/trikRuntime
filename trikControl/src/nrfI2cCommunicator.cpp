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

#include "nrfI2cCommunicator.h"

#include <trikHal/nrfI2cInterface.h>
#include <trikKernel/configurer.h>
#include <QsLog.h>

using namespace trikControl;

NrfI2cCommunicator::NrfI2cCommunicator(const trikKernel::Configurer &configurer, trikHal::NrfI2cInterface &i2c)
	: mI2c(i2c)
	, mState("Nrf I2C Communicator")
{
	const QString devicePath = configurer.attributeByDevice("i2c", "path");

	bool ok = false;
	const int deviceId = configurer.attributeByDevice("i2c", "deviceId").toInt(&ok, 0);
	if (!ok) {
		QLOG_ERROR() << "Incorrect I2C device id" << configurer.attributeByDevice("i2c", "deviceId");
		mState.fail();
		return;
	}

	if (mI2c.connect(devicePath, deviceId)) {
		mState.ready();
	} else {
		mState.fail();
	}
}

NrfI2cCommunicator::NrfI2cCommunicator(const trikKernel::Configurer &configurer,
		trikHal::NrfI2cInterface &i2c, uint8_t bus, uint8_t deviceId)
	: mI2c(i2c)
	, mState("Nrf I2C Communicator")
{
	QString devicePath;
	if (bus == 1) {
		devicePath = configurer.attributeByDevice("i2cBus1", "path");
	} else if (bus == 2) {
		devicePath = configurer.attributeByDevice("i2cBus2", "path");
	} else {
		QLOG_ERROR() << "Incorrect I2C bus" << bus;
		mState.fail();
		return;
	}

	if (mI2c.connect(devicePath, deviceId)) {
		mState.ready();
	} else {
		mState.fail();
	}
}

NrfI2cCommunicator::~NrfI2cCommunicator()
{
	if (mState.isReady()) {
		disconnect();
	}
}

int NrfI2cCommunicator::send(const QByteArray &data)
{
	if (!mState.isReady()) {
		QLOG_ERROR() << "Trying to send data through I2C communicator which is not ready, ignoring";
		return -1;
	}

	QMutexLocker lock(&mLock);
	return mI2c.send(data);
}

int NrfI2cCommunicator::read(const QByteArray &data)
{
	if (!mState.isReady()) {
		QLOG_ERROR() << "Trying to read data from I2C communicator which is not ready, ignoring";
		return 0;
	}

	QMutexLocker lock(&mLock);
	return mI2c.read(data);
}

QVector<uint8_t> NrfI2cCommunicator::readX(const QByteArray &data)
{
	if (!mState.isReady()) {
		QLOG_ERROR() << "Trying to read data from I2C communicator which is not ready, ignoring";
		return {};
	}

	QMutexLocker lock(&mLock);
	return mI2c.readX(data);
}

DeviceInterface::Status NrfI2cCommunicator::status() const
{
	return mState.status();
}

void NrfI2cCommunicator::disconnect()
{
	QMutexLocker lock(&mLock);
	mI2c.disconnect();
	mState.off();
}