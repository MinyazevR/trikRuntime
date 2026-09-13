/* Copyright 2015 Yurii Litvinov and CyberTech Labs Ltd.
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

#include "trikHardwareAbstraction.h"

#include "trikI2c.h"
#include "trikUsbDevice.h"
#include "trikSystemConsole.h"
#include "trikEventFile.h"
#include "trikInputDeviceFile.h"
#include "trikOutputDeviceFile.h"
#include "trikFifo.h"
#include "trikIIOFile.h"
#include "QsLog.h"
#include "commonI2c.h"
#include "trikV4l2VideoDevice.h"

using namespace trikHal;
using namespace trikHal::trik;

TrikHardwareAbstraction::TrikHardwareAbstraction()
	: mI2c(new TrikI2c())
	, mSystemConsole(new TrikSystemConsole())
{
}

TrikHardwareAbstraction::~TrikHardwareAbstraction()
{
}

NrfI2cInterface &TrikHardwareAbstraction::nrfI2c()
{
	return *mI2c.data();
}

NrfI2cInterface *TrikHardwareAbstraction::createNrfI2c() {
	return new TrikI2c();
}

NrfI2cInterface *TrikHardwareAbstraction::createCommonI2c(uint8_t regSize) {
	return new CommonI2c(regSize);
}

UsbDeviceInterface *TrikHardwareAbstraction::createUsbDevice(std::uint16_t vendorId, std::uint16_t productId
		, std::uint8_t outEndpoint, std::uint8_t inEndpoint)
{
	return new TrikUsbDevice(vendorId, productId, outEndpoint, inEndpoint);
}

SystemConsoleInterface &TrikHardwareAbstraction::systemConsole()
{
	return *mSystemConsole.data();
}

EventFileInterface *TrikHardwareAbstraction::createEventFile(const QString &fileName) const
{
	return new TrikEventFile(fileName);
}

FifoInterface *TrikHardwareAbstraction::createFifo(const QString &fileName) const
{
	return new TrikFifo(fileName);
}

IIOFileInterface *TrikHardwareAbstraction::createIIOFile(const QString &fileName, const QString &scanType) const
{
	return new TrikIIOFile(fileName, scanType);
}

InputDeviceFileInterface *TrikHardwareAbstraction::createInputDeviceFile(const QString &fileName) const
{
	return new TrikInputDeviceFile(fileName);
}

OutputDeviceFileInterface *TrikHardwareAbstraction::createOutputDeviceFile(const QString &fileName) const
{
	return new TrikOutputDeviceFile(fileName);
}

QVector<uint8_t> TrikHardwareAbstraction::captureV4l2StillImage(const QString &port, const QDir &pathToPic) const
{
	Q_UNUSED(pathToPic);
	TrikV4l2VideoDevice device(port); // need only one camera device

	QLOG_INFO() << "Start open v4l2 device" << port;

	const QVector<uint8_t> & shot =  device.makeShot();

	QLOG_INFO() << "Captrured RGB888 " << shot.size() << "bytes image";
	return shot;
}


