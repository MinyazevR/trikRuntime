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

#pragma once

#include "QtCore/QVector"
#include "inputDeviceFileInterface.h"
#include "outputDeviceFileInterface.h"
#include "eventFileInterface.h"
#include "fifoInterface.h"
#include "IIOFileInterface.h"
#include "nrfI2cInterface.h"
#include "usbDeviceInterface.h"
#include "systemConsoleInterface.h"

#include <trikHal/trikHalDeclSpec.h>
#include <QtCore/QDir>

namespace trikHal {

/// Hardware abstraction, provides devices that are used to communicate with robot hardware or emulate it.
class TRIKHAL_EXPORT HardwareAbstractionInterface
{
	Q_DISABLE_COPY(HardwareAbstractionInterface)
public:
	HardwareAbstractionInterface() = default;

	virtual ~HardwareAbstractionInterface() = default;

	/// Returns nRF I2C bus implementation.
	virtual NrfI2cInterface &nrfI2c() = 0;

	/// Create nRF I2C bus implementation.
	virtual NrfI2cInterface *createNrfI2c() = 0;

	/// Create Common I2C bus implementation.
	virtual NrfI2cInterface *createCommonI2c(uint8_t regSize) = 0;

	/// Creates a USB bulk transport for a vendor-specific device. Passes
	/// ownership to the caller.
	virtual UsbDeviceInterface *createUsbDevice(std::uint16_t vendorId, std::uint16_t productId
			, std::uint8_t outEndpoint, std::uint8_t inEndpoint) = 0;

	/// Returns system console wrapper, able to execute system command and launch processes.
	virtual SystemConsoleInterface &systemConsole() = 0;

	/// Creates new event file, passes ownership to a caller.
	/// @param fileName - file name (with path, relative or absolute) of an event file.
	/// @param thread - background thread where all socket events will be processed.
	virtual EventFileInterface *createEventFile(const QString &fileName) const = 0;

	/// Creates new FIFO, passes ownership to a caller.
	/// @param fileName - file name (with path, relative or absolute) of a FIFO file.
	virtual FifoInterface *createFifo(const QString &fileName) const = 0;

	/// Creates new IIO file, passes ownership to a caller.
	/// @param fileName - file name (with path, relative or absolute) of a FIFO file.
	virtual IIOFileInterface *createIIOFile(const QString &fileName, const QString &scanType) const = 0;

	/// Creates new input device file, passes ownership to a caller.
	/// @param fileName - file name (with path, relative or absolute) of a device file.
	virtual InputDeviceFileInterface *createInputDeviceFile(const QString &fileName) const = 0;

	/// Creates new output device file, passes ownership to a caller.
	/// @param fileName - file name (with path, relative or absolute) of a device file.
	virtual OutputDeviceFileInterface *createOutputDeviceFile(const QString &fileName) const = 0;

	/// Returns QVector with info about picture pixels
	/// @param port - port name for device
	/// @param pathToPic - path to picture
	virtual QVector<uint8_t> captureV4l2StillImage(const QString &port, const QDir &pathToPic) const = 0;
};

}
