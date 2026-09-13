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

#include "stubUsbDevice.h"

#include <QtCore/QDebug>

using namespace trikHal;
using namespace trikHal::stub;

StubUsbDevice::StubUsbDevice(std::uint16_t vendorId, std::uint16_t productId
		, std::uint8_t outEndpoint, std::uint8_t inEndpoint)
	: mVendorId(vendorId)
	, mProductId(productId)
	, mOutEndpoint(outEndpoint)
	, mInEndpoint(inEndpoint)
{
}

bool StubUsbDevice::connect()
{
	qDebug() << "StubUsbDevice: connect" << QString::number(mVendorId, 16)
			<< ":" << QString::number(mProductId, 16)
			<< "out=0x" << QString::number(mOutEndpoint, 16)
			<< "in=0x" << QString::number(mInEndpoint, 16);
	mConnected = true;
	return true;
}

void StubUsbDevice::disconnect()
{
	qDebug() << "StubUsbDevice: disconnect";
	mConnected = false;
}

bool StubUsbDevice::isConnected() const
{
	return mConnected;
}

bool StubUsbDevice::send(const QByteArray &data)
{
	qDebug() << "StubUsbDevice: send" << data.toHex();
	return true;
}

void StubUsbDevice::setDataCallback(DataCallback callback, void *userData)
{
	Q_UNUSED(callback)
	Q_UNUSED(userData)
	qDebug() << "StubUsbDevice: setDataCallback";
}

QByteArray StubUsbDevice::read(int timeoutMs)
{
	Q_UNUSED(timeoutMs)
	qDebug() << "StubUsbDevice: read";
	return {};
}