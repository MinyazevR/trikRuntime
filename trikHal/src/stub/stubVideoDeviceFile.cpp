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

#include "stubVideoDeviceFile.h"

#include <QsLog.h>

using namespace trikHal::stub;

StubVideoDeviceFile::StubVideoDeviceFile(const QString &fileName)
	: VideoDeviceFileInterface(nullptr)
	, mFileName(fileName)
{
}

bool StubVideoDeviceFile::open()
{
	QLOG_INFO() << "StubVideoDeviceFile: open" << mFileName;
	mOpened = true;
	return true;
}

bool StubVideoDeviceFile::startStreaming(bool forDsp)
{
	Q_UNUSED(forDsp);
	QLOG_INFO() << "StubVideoDeviceFile: startStreaming" << mFileName;
	return true;
}

void StubVideoDeviceFile::stopStreaming()
{
	QLOG_INFO() << "StubVideoDeviceFile: stopStreaming" << mFileName;
}

void StubVideoDeviceFile::close()
{
	QLOG_INFO() << "StubVideoDeviceFile: close" << mFileName;
	mOpened = false;
}

bool StubVideoDeviceFile::capture(const uint8_t *&data, size_t &size)
{
	Q_UNUSED(data)
	Q_UNUSED(size)
	return false;
}

void StubVideoDeviceFile::release()
{
}

bool StubVideoDeviceFile::isOpen() const
{
	return mOpened;
}
