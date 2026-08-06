/* Copyright 2024 CyberTech Labs Ltd.
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

class TRIKHAL_EXPORT VideoDeviceFileInterface : public QObject
{
	Q_OBJECT

public:
	explicit VideoDeviceFileInterface(QObject *parent = nullptr) : QObject(parent) {}
	~VideoDeviceFileInterface() override = default;

	/// Open the V4L2 device, negotiate format, allocate DMA ring buffers.
	/// Does NOT start streaming — call startStreaming() separately.
	virtual bool open() = 0;

	/// QBUF all buffers + STREAMON + start internal QSocketNotifier.
	virtual bool startStreaming() = 0;

	/// STREAMOFF.
	virtual void stopStreaming() = 0;

	/// Stop streaming, munmap buffers, close fd.
	virtual void close() = 0;

	/// Return the last dequeued frame pointer (zero-copy).
	/// Valid until release().  Returns false if no frame available.
	virtual bool capture(const uint8_t *&data, size_t &size) = 0;

	/// Return the buffer dequeued by capture() back to the V4L2 ring (QBUF).
	virtual void release() = 0;

	/// True after successful open(), before close().
	virtual bool isOpen() const = 0;

	/// Unique identifier for this device (typically the device path).
	virtual QString id() const = 0;

	/// Negotiated frame dimensions and pixel format (available after open()).
	virtual uint32_t actualWidth() const = 0;
	virtual uint32_t actualHeight() const = 0;
	virtual uint32_t actualFourcc() const = 0;

	/// Bytes per scanline (may be > width * bpp due to alignment).
	virtual uint32_t bytesPerLine() const = 0;

Q_SIGNALS:
	/// Emitted when a new frame is dequeued and ready via capture().
	/// The data pointer is valid until release().
	void frameReady(const uint8_t *data, size_t size);
};

} // namespace trikHal
