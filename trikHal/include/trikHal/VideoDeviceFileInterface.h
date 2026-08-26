/* Copyright 2026 CyberTech Labs Ltd.
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

#include <QtCore/QObject>
#include <QtCore/QString>
#include "trikHal/trikHalDeclSpec.h"

namespace trikHal {

/// A V4L2 capture device: open/start/stop a streaming capture and hand raw
/// frames to consumers via the frameReady() signal. Implementations own the
/// underlying mmap buffers and are created by HardwareAbstractionInterface.
class TRIKHAL_EXPORT VideoDeviceFileInterface : public QObject
{
	Q_OBJECT

public:
	explicit VideoDeviceFileInterface(QObject *parent = nullptr) : QObject(parent) {}
	~VideoDeviceFileInterface() override = default;

	virtual bool open() = 0;

	/// Start capturing. The stream runs until stopStreaming() is called.
	virtual bool startStreaming() = 0;

	/// Lock the exposure to manual, if the device supports it and it has not
	/// been fixed yet. Intended for continuous capture consumers that need a
	/// stable brightness; a still-shot consumer should leave it on auto.
	/// Idempotent and non-blocking.
	virtual void fixExposure() = 0;

	virtual void stopStreaming() = 0;
	virtual void close() = 0;
	virtual void release() = 0;
	virtual uint32_t actualFourcc() const = 0;
	virtual uint32_t bytesPerLine() const = 0;

	/// Configure the device to use a caller-managed buffer (USERPTR mode)
	/// instead of driver-allocated MMAP buffers.  Must be called before
	/// open().  The buffer must be physically contiguous and stay valid for
	/// the entire lifetime of the device.  Pass nullptr to use the default
	/// MMAP mode.  The default implementation is a no-op (MMAP only).
	virtual void setUserPtrBuffer(void *data, size_t size)
	{
		Q_UNUSED(data);
		Q_UNUSED(size);
	}

Q_SIGNALS:
	void frameReady(const uint8_t *data, size_t size);
};

} // namespace trikHal
