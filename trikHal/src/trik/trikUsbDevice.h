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

#include <atomic>
#include <thread>

#include <libusb-1.0/libusb.h>

namespace trikHal {
namespace trik {

/// Real libusb implementation of UsbDeviceInterface for nRF52833.
///
/// Uses ISO IN endpoint with batched packets for telemetry, BULK OUT for
/// commands. Falls back to BULK IN when ISO is unavailable.
class TrikUsbDevice : public UsbDeviceInterface
{
public:
	TrikUsbDevice(std::uint16_t vendorId, std::uint16_t productId
			, std::uint8_t outEndpoint, std::uint8_t inEndpoint
			, std::uint8_t isoEndpoint = 0x88
			, int packetsPerUrb = 4);

	~TrikUsbDevice() override;

	bool connect() override;
	void disconnect() override;
	bool isConnected() const override;
	bool send(const QByteArray &data) override;
	void setDataCallback(DataCallback callback, void *userData) override;
	QByteArray read(int timeoutMs) override;

private:
	/// libusv completion trampoline.
	static void LIBUSB_CALL onTransfer(libusb_transfer *transfer);

	void handleTransfer(libusb_transfer *transfer);
	void submitReadTransfer(libusb_transfer *transfer);
	void eventLoop();
	void freeTransfers();

	std::uint16_t mVendorId;
	std::uint16_t mProductId;
	std::uint8_t mOutEndpoint;
	std::uint8_t mInEndpoint;
	std::uint8_t mIsoEndpoint;
	int mPacketsPerUrb;

	libusb_context *mContext = nullptr;
	libusb_device_handle *mHandle = nullptr;

	std::vector<libusb_transfer *> mTransfers;
	std::vector<std::vector<uint8_t>> mBuffers;
	static constexpr int kPoolSize = 8;
	static constexpr int kBufferSize = 512;

	DataCallback mDataCallback = nullptr;
	void *mDataCallbackUserData = nullptr;

	std::atomic<bool> mRunning{false};
	std::atomic<int> mPending{0};
	std::thread mEventThread;
};

}
}