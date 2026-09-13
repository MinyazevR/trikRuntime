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

#include "usbDeviceInterface.h"

namespace trikHal {
namespace stub {

/// Stub USB device for desktop builds and tests. Logs operations.
class StubUsbDevice : public UsbDeviceInterface
{
public:
	StubUsbDevice(std::uint16_t vendorId, std::uint16_t productId
			, std::uint8_t outEndpoint, std::uint8_t inEndpoint);

	bool connect() override;
	void disconnect() override;
	bool isConnected() const override;
	bool send(const QByteArray &data) override;
	void setDataCallback(DataCallback callback, void *userData) override;
	QByteArray read(int timeoutMs) override;

private:
	std::uint16_t mVendorId;
	std::uint16_t mProductId;
	std::uint8_t mOutEndpoint;
	std::uint8_t mInEndpoint;
	bool mConnected = false;
};

}
}