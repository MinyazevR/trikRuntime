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

#include <VideoDeviceFileInterface.h>

#include <QtCore/QString>

namespace trikHal {
namespace stub {

class StubVideoDeviceFile : public VideoDeviceFileInterface
{
	Q_OBJECT
	Q_DISABLE_COPY(StubVideoDeviceFile)
public:
	explicit StubVideoDeviceFile(const QString &fileName);

	bool open() override;
	bool startStreaming() override;
	void stopStreaming() override;
	void close() override;
	bool capture(const uint8_t *&data, size_t &size) override;
	void release() override;
	bool isOpen() const override;

	uint32_t actualWidth() const override { return 0; }
	uint32_t actualHeight() const override { return 0; }
	uint32_t actualFourcc() const override { return 0; }
	uint32_t bytesPerLine() const override { return 0; }

	QString id() const override { return mFileName; }

private:
	QString mFileName;
	bool mOpened = false;
};

} // namespace stub
} // namespace trikHal
