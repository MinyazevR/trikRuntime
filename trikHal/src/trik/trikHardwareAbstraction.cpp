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
#include "trikVideoDeviceFile.h"
#include "QsLog.h"
#include "commonI2c.h"
#include "trikV4l2VideoDevice.h"
#include <linux/videodev2.h>
#include <trikKernel/videoUtils.h>

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>

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

QVector<uint8_t> TrikHardwareAbstraction::captureV4l2StillImage(const QString &port, const QDir &pathToPic) const
{
	Q_UNUSED(pathToPic);
	TrikV4l2VideoDevice device(port);

	QLOG_INFO() << "Start open v4l2 device" << port;

	if (!device.open() || !device.startStreaming()) {
		QLOG_ERROR() << "Failed to open v4l2 device" << port;
		return {};
	}

	QEventLoop loop;
	QTimer::singleShot(1000, &loop, [&loop]() { loop.exit(-1); });
	QObject::connect(&device, &VideoDeviceFileInterface::frameReady, &loop,
	                 [&loop](const uint8_t *, size_t) { loop.quit(); });
	if (loop.exec() < 0) {
		QLOG_WARN() << "V4l2 still image capture timeout";
		return {};
	}

	const uint8_t *data = nullptr;
	size_t size = 0;
	if (!device.capture(data, size)) {
		QLOG_ERROR() << "V4l2 still image capture failed";
		return {};
	}

	const auto h = device.actualHeight();
	const auto w = device.actualWidth();
	QVector<uint8_t> raw(data, data + size);
	device.release();
	device.stopStreaming();

	QVector<uint8_t> rgb;
	switch (device.actualFourcc()) {
	case V4L2_PIX_FMT_NV16:
		rgb = trikKernel::VideoUtils::yuv422pToRgb(raw, h, w);
		break;
	case V4L2_PIX_FMT_YUYV:
		rgb = trikKernel::VideoUtils::yuyvToRgb(raw, h, w);
		break;
	default:
		QLOG_ERROR() << "V4l2: unsupported fourcc" << Qt::hex << device.actualFourcc();
		return {};
	}

	QLOG_INFO() << "Captrured RGB888 " << rgb.size() << "bytes image";
	return rgb;
}

VideoDeviceFileInterface *TrikHardwareAbstraction::createVideoDeviceFile(
		const QString &devicePath, uint32_t width, uint32_t height, uint32_t fourcc) const
{
	return new TrikVideoDeviceFile(devicePath, width, height, fourcc);
}

OutputDeviceFileInterface *TrikHardwareAbstraction::createDspCommunicator() const
{
	return new TrikOutputDeviceFile(QStringLiteral("/dev/rpmsg0"));
}


