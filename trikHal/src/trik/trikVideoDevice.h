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

#include "videoDeviceFileBase.h"

namespace trikHal {
namespace trik {

class TrikVideoDevice : public trikHal::VideoDeviceFileBase
{
	Q_OBJECT
	Q_DISABLE_COPY(TrikVideoDevice)

public:
	TrikVideoDevice(const QString &devicePath, uint32_t width, uint32_t height,
	                uint32_t fourcc, uint32_t bufferCount = 3,
	                bool needPalStandard = false);

protected:
	bool setFormat() override;
};

} // namespace trik
} // namespace trikHal
