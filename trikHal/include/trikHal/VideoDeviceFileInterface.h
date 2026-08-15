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

	virtual bool open() = 0;
	/// Start capturing. @p forDsp marks the stream as feeding the DSP video
	/// sensors (push consumer); a plain camera (pull/getPhoto) stream passes
	/// false so DSP-only tuning (e.g. the webcam exposure lock) is skipped.
	virtual bool startStreaming(bool forDsp = false) = 0;
	virtual void stopStreaming() = 0;
	virtual void close() = 0;
	virtual bool capture(const uint8_t *&data, size_t &size) = 0;
	virtual void release() = 0;
	virtual bool isOpen() const = 0;
	virtual QString id() const = 0;
	virtual uint32_t actualWidth() const = 0;
	virtual uint32_t actualHeight() const = 0;
	virtual uint32_t actualFourcc() const = 0;
	virtual uint32_t bytesPerLine() const = 0;

Q_SIGNALS:
	void frameReady(const uint8_t *data, size_t size);
};

} // namespace trikHal
