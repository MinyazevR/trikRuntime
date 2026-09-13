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

#include "trikUsbDevice.h"

#include <cstring>

#include <QsLog.h>

using namespace trikHal;
using namespace trikHal::trik;

TrikUsbDevice::TrikUsbDevice(std::uint16_t vendorId, std::uint16_t productId
		, std::uint8_t outEndpoint, std::uint8_t inEndpoint
		, std::uint8_t isoEndpoint, int packetsPerUrb)
	: mVendorId(vendorId)
	, mProductId(productId)
	, mOutEndpoint(outEndpoint)
	, mInEndpoint(inEndpoint)
	, mIsoEndpoint(isoEndpoint)
	, mPacketsPerUrb(packetsPerUrb)
{
}

TrikUsbDevice::~TrikUsbDevice()
{
	disconnect();
}

void LIBUSB_CALL TrikUsbDevice::onTransfer(libusb_transfer *transfer)
{
	auto *self = static_cast<TrikUsbDevice *>(transfer->user_data);
	self->mPending.fetch_sub(1, std::memory_order_acq_rel);
	self->handleTransfer(transfer);
	if (self->mRunning.load(std::memory_order_acquire)) {
		self->submitReadTransfer(transfer);
	}
}

void TrikUsbDevice::handleTransfer(libusb_transfer *transfer)
{
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		return;
	}
	if (mDataCallback == nullptr) {
		return;
	}
	QByteArray data(QByteArray::fromRawData(
			reinterpret_cast<const char *>(transfer->buffer)
			, static_cast<int>(transfer->actual_length)));
	mDataCallback(data, mDataCallbackUserData);
}

void TrikUsbDevice::submitReadTransfer(libusb_transfer *transfer)
{
	if (transfer == nullptr) {
		return;
	}
	if (libusb_submit_transfer(transfer) == 0) {
		mPending.fetch_add(1, std::memory_order_acq_rel);
	}
}

bool TrikUsbDevice::connect()
{
	if (libusb_init(&mContext) != 0) {
		QLOG_ERROR() << "TrikUsbDevice: libusb_init failed";
		return false;
	}

	mHandle = libusb_open_device_with_vid_pid(mContext, mVendorId, mProductId);
	if (mHandle == nullptr) {
		QLOG_ERROR() << "TrikUsbDevice: device" << QString::number(mVendorId, 16)
				<< ":" << QString::number(mProductId, 16) << "not found";
		libusb_exit(mContext);
		mContext = nullptr;
		return false;
	}

	if (libusb_claim_interface(mHandle, 0) != 0) {
		QLOG_ERROR() << "TrikUsbDevice: claim interface 0 failed";
		libusb_close(mHandle);
		mHandle = nullptr;
		libusb_exit(mContext);
		mContext = nullptr;
		return false;
	}

	QLOG_INFO() << "TrikUsbDevice: connected";

	mBuffers.assign(static_cast<std::size_t>(kPoolSize)
			, std::vector<uint8_t>(static_cast<std::size_t>(kBufferSize)));
	mTransfers.assign(static_cast<std::size_t>(kPoolSize), nullptr);

	mRunning.store(true, std::memory_order_release);
	mEventThread = std::thread([this]() {
		while (mRunning.load(std::memory_order_acquire)
				|| mPending.load(std::memory_order_acquire) > 0) {
			timeval tv{0, 10000};
			libusb_handle_events_timeout_completed(mContext, &tv, nullptr);
		}
	});

	// try ISO first
	bool isoOk = false;
	if (mIsoEndpoint != 0) {
		for (int i = 0; i < kPoolSize; ++i) {
			libusb_transfer *t = libusb_alloc_transfer(mPacketsPerUrb);
			if (t == nullptr) {
				continue;
			}
			libusb_fill_iso_transfer(t, mHandle, mIsoEndpoint
					, mBuffers[i].data()
					, static_cast<int>(mBuffers[i].size())
					, mPacketsPerUrb, &TrikUsbDevice::onTransfer, this, 0);
			libusb_set_iso_packet_lengths(t, static_cast<int>(mBuffers[i].size() / mPacketsPerUrb));
			if (libusb_submit_transfer(t) == 0) {
				mTransfers[i] = t;
				mPending.fetch_add(1, std::memory_order_acq_rel);
				isoOk = true;
			} else {
				libusb_free_transfer(t);
			}
		}
	}

	// fallback to BULK IN
	if (!isoOk) {
		QLOG_INFO() << "TrikUsbDevice: ISO not available, falling back to BULK IN 0x"
				<< QString::number(mInEndpoint, 16);
		for (int i = 0; i < kPoolSize; ++i) {
			libusb_transfer *t = libusb_alloc_transfer(0);
			if (t == nullptr) {
				continue;
			}
			libusb_fill_bulk_transfer(t, mHandle, mInEndpoint
					, mBuffers[i].data(), static_cast<int>(mBuffers[i].size())
					, &TrikUsbDevice::onTransfer, this, 0);
			if (libusb_submit_transfer(t) == 0) {
				mTransfers[i] = t;
				mPending.fetch_add(1, std::memory_order_acq_rel);
			} else {
				libusb_free_transfer(t);
			}
		}
	}

	return true;
}

void TrikUsbDevice::disconnect()
{
	if (!mRunning.exchange(false, std::memory_order_acq_rel)) {
		return;
	}

	for (libusb_transfer *t : mTransfers) {
		if (t != nullptr) {
			libusb_cancel_transfer(t);
		}
	}

	if (mEventThread.joinable()) {
		mEventThread.join();
	}

	freeTransfers();

	if (mHandle != nullptr) {
		libusb_release_interface(mHandle, 0);
		libusb_close(mHandle);
		mHandle = nullptr;
	}

	if (mContext != nullptr) {
		libusb_exit(mContext);
		mContext = nullptr;
	}

	QLOG_INFO() << "TrikUsbDevice: disconnected";
}

bool TrikUsbDevice::isConnected() const
{
	return mRunning.load(std::memory_order_acquire);
}

bool TrikUsbDevice::send(const QByteArray &data)
{
	if (mHandle == nullptr) {
		return false;
	}

	int transferred = 0;
	const int r = libusb_bulk_transfer(mHandle, mOutEndpoint
			, reinterpret_cast<unsigned char *>(const_cast<char *>(data.constData()))
			, static_cast<int>(data.size()), &transferred, 1000);
	return r == 0 && transferred == data.size();
}

void TrikUsbDevice::setDataCallback(DataCallback callback, void *userData)
{
	mDataCallback = callback;
	mDataCallbackUserData = userData;
}

QByteArray TrikUsbDevice::read(int timeoutMs)
{
	if (mHandle == nullptr) {
		return {};
	}

	std::vector<uint8_t> buf(static_cast<std::size_t>(kBufferSize));
	int transferred = 0;
	const int r = libusb_bulk_transfer(mHandle, mInEndpoint
			, buf.data(), static_cast<int>(buf.size()), &transferred, timeoutMs);
	if (r != 0 || transferred <= 0) {
		return {};
	}
	return QByteArray(reinterpret_cast<const char *>(buf.data()), transferred);
}

void TrikUsbDevice::freeTransfers()
{
	for (libusb_transfer *&t : mTransfers) {
		if (t != nullptr) {
			libusb_free_transfer(t);
			t = nullptr;
		}
	}
	mTransfers.clear();
	mBuffers.clear();
}