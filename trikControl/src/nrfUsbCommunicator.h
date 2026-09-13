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

#include <QtCore/QAtomicInt>
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>

#include <array>
#include <memory>

#include "deviceState.h"
#include "peripheryCommunicatorInterface.h"
#include "nrfUsbProtocol.h"

namespace trikHal {
class UsbDeviceInterface;
}

namespace trikControl {

/// Lock-free telemetry snapshot from the nRF52833 periphery module.
struct TelemetrySnapshot {
	uint64_t timestampUs = 0;
	std::array<int32_t, nrfUsbProtocol::kTelemetryPortCount> values{};
	uint32_t generation = 0;
};

using SnapshotPtr = std::shared_ptr<const TelemetrySnapshot>;

/// Communicator with the nRF52833 periphery module over USB.
///
/// Receives telemetry, maintains a lock-free snapshot cache for pull by clients.
/// Commands are sent via BULK OUT.
class NrfUsbCommunicator : public QObject, public PeripheryCommunicatorInterface
{
	Q_OBJECT

public:
	/// Constructor.
	/// @param usb - USB transport (takes ownership).
	/// @param telemetryPeriodUs - device telemetry period in microseconds.
	NrfUsbCommunicator(std::unique_ptr<trikHal::UsbDeviceInterface> usb, uint32_t telemetryPeriodUs);

	~NrfUsbCommunicator() override;

	/// Connect to the device and start the telemetry stream.
	bool connect();

	/// Disconnect and stop.
	void disconnect();

	/// PeripheryCommunicatorInterface:
	int send(const QByteArray &data) override;
	int read(const QByteArray &data) override;
	Status status() const override;

	/// Returns the latest snapshot atomically. O(1), lock-free.
	/// Returns nullptr if no snapshot has been received yet.
	SnapshotPtr latest() const;

	/// Synchronous port read (GET_VALUE). Returns the value or 0 on failure.
	int32_t readPort(uint8_t port);

private:
	/// Data callback from the USB transport.
	static void onUsbData(const QByteArray &data, void *userData);

	/// Parse incoming data, extract snapshots.
	void handleData(const QByteArray &data);

	/// Store a new snapshot lock-free.
	void publishSnapshot(uint64_t timestampUs
			, const std::array<int32_t, nrfUsbProtocol::kTelemetryPortCount> &values);

	std::unique_ptr<trikHal::UsbDeviceInterface> mUsb;
	uint32_t mTelemetryPeriodUs;

	/// Lock-free snapshot store: writer atomically replaces shared_ptr.
	mutable QReadWriteLock mSnapshotLock;
	SnapshotPtr mLatestSnapshot{std::make_shared<TelemetrySnapshot>()};

	DeviceState mState;
};

}