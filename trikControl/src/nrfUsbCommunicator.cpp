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

#include "nrfUsbCommunicator.h"

#include <cstring>

#include <trikHal/usbDeviceInterface.h>
#include <trikKernel/timeVal.h>
#include <QsLog.h>

using namespace trikControl;

namespace {

uint64_t readLe64(const uint8_t *p)
{
	return static_cast<uint64_t>(p[0])
			| (static_cast<uint64_t>(p[1]) << 8)
			| (static_cast<uint64_t>(p[2]) << 16)
			| (static_cast<uint64_t>(p[3]) << 24)
			| (static_cast<uint64_t>(p[4]) << 32)
			| (static_cast<uint64_t>(p[5]) << 40)
			| (static_cast<uint64_t>(p[6]) << 48)
			| (static_cast<uint64_t>(p[7]) << 56);
}

int32_t readLe32(const uint8_t *p)
{
	return static_cast<int32_t>(static_cast<uint32_t>(p[0])
			| (static_cast<uint32_t>(p[1]) << 8)
			| (static_cast<uint32_t>(p[2]) << 16)
			| (static_cast<uint32_t>(p[3]) << 24));
}

}

NrfUsbCommunicator::NrfUsbCommunicator(
		std::unique_ptr<trikHal::UsbDeviceInterface> usb, uint32_t telemetryPeriodUs)
	: mUsb(std::move(usb))
	, mTelemetryPeriodUs(telemetryPeriodUs)
	, mState("NrfUsbCommunicator")
{
}

NrfUsbCommunicator::~NrfUsbCommunicator()
{
	disconnect();
}

bool NrfUsbCommunicator::connect()
{
	mUsb->setDataCallback(&NrfUsbCommunicator::onUsbData, this);
	if (!mUsb->connect()) {
		QLOG_ERROR() << "NrfUsbCommunicator: USB connect failed";
		mState.fail();
		return false;
	}
	mState.ready();
	QLOG_INFO() << "NrfUsbCommunicator: connected, telemetry period" << mTelemetryPeriodUs << "us";
	return true;
}

void NrfUsbCommunicator::disconnect()
{
	mUsb->setDataCallback(nullptr, nullptr);
	mUsb->disconnect();
	mState.off();
}

int NrfUsbCommunicator::send(const QByteArray &data)
{
	if (!mState.isReady()) {
		return 0;
	}
	QByteArray frame(static_cast<int>(nrfUsbProtocol::kHeaderSize) + data.size(), '\0');
	auto *hdr = reinterpret_cast<nrfUsbProtocol::CommHeader *>(frame.data());
	hdr->type = nrfUsbProtocol::kTypePeriphery;
	hdr->reqId = nrfUsbProtocol::kReqIdFireAndForget;
	hdr->len = static_cast<uint16_t>(data.size());
	frame.replace(static_cast<int>(nrfUsbProtocol::kHeaderSize), data.size(), data);
	return mUsb->send(frame) ? data.size() : 0;
}

int NrfUsbCommunicator::read(const QByteArray &data)
{
	if (!mState.isReady()) {
		return 0;
	}

	QByteArray frame(static_cast<int>(nrfUsbProtocol::kHeaderSize) + data.size(), '\0');
	auto *hdr = reinterpret_cast<nrfUsbProtocol::CommHeader *>(frame.data());
	hdr->type = nrfUsbProtocol::kTypePeriphery;
	hdr->flags = 0;
	hdr->reqId = 0xAA;
	hdr->seqId = 0;
	hdr->len = static_cast<uint16_t>(data.size());
	frame.replace(static_cast<int>(nrfUsbProtocol::kHeaderSize), data.size(), data);

	if (!mUsb->send(frame)) {
		return 0;
	}

	const auto response = mUsb->read(100);
	const int minSize = static_cast<int>(nrfUsbProtocol::kHeaderSize + 4);
	if (response.size() < minSize) {
		return 0;
	}

	const auto *respData = reinterpret_cast<const uint8_t *>(response.constData());
	return static_cast<int>(readLe32(respData + response.size() - 4));
}

NrfUsbCommunicator::Status NrfUsbCommunicator::status() const
{
	return mState.status();
}

SnapshotPtr NrfUsbCommunicator::latest() const
{
	QReadLocker lock(&mSnapshotLock);
	return mLatestSnapshot;
}

int32_t NrfUsbCommunicator::readPort(uint8_t port)
{
	QByteArray frame(static_cast<int>(nrfUsbProtocol::kHeaderSize + 3), '\0');
	auto *hdr = reinterpret_cast<nrfUsbProtocol::CommHeader *>(frame.data());
	hdr->type = nrfUsbProtocol::kTypePeriphery;
	hdr->flags = 0;
	hdr->reqId = port + 1;
	hdr->seqId = 0;
	hdr->len = 3;
	frame[static_cast<int>(nrfUsbProtocol::kHeaderSize)] = static_cast<char>(nrfUsbProtocol::kDevicePort);
	frame[static_cast<int>(nrfUsbProtocol::kHeaderSize + 1)] = static_cast<char>(nrfUsbProtocol::kPortGetValue);
	frame[static_cast<int>(nrfUsbProtocol::kHeaderSize + 2)] = static_cast<char>(port);

	if (!mUsb->send(frame)) {
		return 0;
	}

	const auto response = mUsb->read(100);
	const int minSize = static_cast<int>(nrfUsbProtocol::kHeaderSize + 6);
	if (response.size() < minSize) {
		return 0;
	}

	const auto *respData = reinterpret_cast<const uint8_t *>(response.constData());
	const auto *respPayload = respData + nrfUsbProtocol::kHeaderSize;
	return readLe32(respPayload + 2);
}

void NrfUsbCommunicator::onUsbData(const QByteArray &data, void *userData)
{
	auto *self = static_cast<NrfUsbCommunicator *>(userData);
	self->handleData(data);
}

void NrfUsbCommunicator::handleData(const QByteArray &data)
{
	const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
	std::size_t offset = 0;
	const std::size_t totalLen = static_cast<std::size_t>(data.size());

	while (offset + nrfUsbProtocol::kHeaderSize <= totalLen) {
		nrfUsbProtocol::CommHeader hdr;
		std::memcpy(&hdr, bytes + offset, nrfUsbProtocol::kHeaderSize);
		const std::size_t frameLen = static_cast<std::size_t>(hdr.len)
				+ nrfUsbProtocol::kHeaderSize;
		if (offset + frameLen > totalLen) {
			break;
		}

		const bool isTelemetry = hdr.type == nrfUsbProtocol::kTypePeriphery
				&& hdr.reqId == nrfUsbProtocol::kReqIdTelemetry
				&& hdr.len >= nrfUsbProtocol::kTelemetryFixed
				&& bytes[offset + nrfUsbProtocol::kHeaderSize] == nrfUsbProtocol::kDeviceControl;

		if (isTelemetry) {
			const auto *payload = bytes + offset + nrfUsbProtocol::kHeaderSize;
			const uint64_t tsUs = readLe64(payload + 1);
			std::array<int32_t, nrfUsbProtocol::kTelemetryPortCount> values{};
			for (std::size_t i = 0; i < nrfUsbProtocol::kTelemetryPortCount; ++i) {
				values[i] = readLe32(payload + nrfUsbProtocol::kTelemetryFixed + i * 4);
			}
			publishSnapshot(tsUs, values);
		}

		offset += frameLen;
	}
}

void NrfUsbCommunicator::publishSnapshot(
		uint64_t timestampUs
		, const std::array<int32_t, nrfUsbProtocol::kTelemetryPortCount> &values)
{
	auto snapshot = std::make_shared<TelemetrySnapshot>();
	snapshot->timestampUs = timestampUs;
	snapshot->values = values;
	snapshot->generation = 0;

	QWriteLocker lock(&mSnapshotLock);
	snapshot->generation = mLatestSnapshot->generation + 1;
	mLatestSnapshot = snapshot;
}