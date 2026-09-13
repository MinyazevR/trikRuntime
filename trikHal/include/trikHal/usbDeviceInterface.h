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

#include <QtCore/QByteArray>
#include <QtCore/QObject>

#include <trikHal/trikHalDeclSpec.h>

namespace trikHal {

/// Abstract USB bulk transport for vendor-specific peripherals.
class TRIKHAL_EXPORT UsbDeviceInterface
{
	Q_DISABLE_COPY(UsbDeviceInterface)
public:
	/// Callback for incoming data from the device.
	using DataCallback = void(*)(const QByteArray &data, void *userData);

	UsbDeviceInterface() = default;

	virtual ~UsbDeviceInterface() = default;

	/// Open device, claim interface, start event loop. Returns true on success.
	virtual bool connect() = 0;

	/// Stop event loop, release interface, close device.
	virtual void disconnect() = 0;

	/// Returns true if device is connected and event loop is running.
	virtual bool isConnected() const = 0;

	/// Fire-and-forget send on BULK OUT endpoint. Returns true if submitted.
	virtual bool send(const QByteArray &data) = 0;

	/// Register callback for incoming data (ISO IN or BULK IN).
	/// @param callback - function called on each received URB completion.
	/// @param userData - opaque pointer passed back to callback.
	virtual void setDataCallback(DataCallback callback, void *userData) = 0;

	/// Synchronous read on BULK IN endpoint (fallback path).
	/// @param timeoutMs - timeout in milliseconds.
	virtual QByteArray read(int timeoutMs) = 0;
};

}