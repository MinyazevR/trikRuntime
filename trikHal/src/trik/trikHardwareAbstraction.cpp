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
#include "trikMspUsb.h"
#include "trikSystemConsole.h"
#include "trikEventFile.h"
#include "trikInputDeviceFile.h"
#include "trikOutputDeviceFile.h"
#include "trikFifo.h"
#include "trikIIOFile.h"
#include "trikVideoDevice.h"
#include "QsLog.h"
#include "commonI2c.h"
#include "trikFbOutput.h"

using namespace trikHal;
using namespace trikHal::trik;

TrikHardwareAbstraction::TrikHardwareAbstraction()
	: mI2c(new TrikI2c())
	, mUsb(new TrikMspUsb())
	, mSystemConsole(new TrikSystemConsole())
{
}

TrikHardwareAbstraction::~TrikHardwareAbstraction()
{
}

MspI2cInterface &TrikHardwareAbstraction::mspI2c()
{
	return *mI2c.data();
}

MspI2cInterface *TrikHardwareAbstraction::createMspI2c() {
	return new TrikI2c();
}

MspI2cInterface *TrikHardwareAbstraction::createCommonI2c(uint8_t regSize) {
	return new CommonI2c(regSize);
}


MspUsbInterface &TrikHardwareAbstraction::mspUsb()
{
	return *mUsb.data();
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

VideoDeviceFileInterface *TrikHardwareAbstraction::createVideoDeviceFile(
		const QString &devicePath, uint32_t width, uint32_t height,
		uint32_t fourcc, bool needPalStandard) const
{
	return new TrikVideoDevice(devicePath, width, height, fourcc, 3, needPalStandard);
}

OutputDeviceFileInterface *TrikHardwareAbstraction::createDspCommunicator() const
{
	return new TrikOutputDeviceFile(QStringLiteral("/dev/rpmsg0"));
}

FbOutputInterface *TrikHardwareAbstraction::createFbOutput() const
{
	return new trik::TrikFbOutput();
}


