/* Copyright 2015 Yurii Litvinov and CyberTech Labs Ltd.
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

#include "trikOutputDeviceFile.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

#include <QsLog.h>

using namespace trikHal::trik;

TrikOutputDeviceFile::TrikOutputDeviceFile(const QString &fileName)
	: mFile(fileName)
{
}

bool TrikOutputDeviceFile::open(OpenMode mode)
{
	if (mode == OpenMode::NonBlockingBinary) {
		if (mFileDescriptor != -1)
			return true;

		// Create the FIFO node on demand; an existing node is fine.
		if (::mkfifo(mFile.fileName().toStdString().c_str(), 0666) != 0 && errno != EEXIST) {
			QLOG_ERROR() << "mkfifo(" << mFile.fileName() << ") failed:" << strerror(errno);
			return false;
		}

		// O_RDWR keeps us as our own reader, so open() never blocks and write()
		// never EPIPEs; O_NONBLOCK makes write() return EAGAIN (drop) when full.
		mFileDescriptor = ::open(mFile.fileName().toStdString().c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (mFileDescriptor == -1) {
			QLOG_ERROR() << "open(" << mFile.fileName() << ") failed:" << strerror(errno);
			return false;
		}

		QLOG_INFO() << "Opened output file (non-blocking binary)" << mFile.fileName();
		return true;
	}

	QLOG_INFO() << "Opening output device file" << mFile.fileName();

	if (!mFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Unbuffered | QIODevice::Text)) {
		QLOG_ERROR() << "File" << mFile.fileName() << " failed to open for writing";
		return false;
	}

	return true;
}

void TrikOutputDeviceFile::close()
{
	if (mFileDescriptor != -1) {
		QLOG_INFO() << "Closing output file (non-blocking binary)" << mFile.fileName();
		::close(mFileDescriptor);
		mFileDescriptor = -1;
	}

	if (mFile.isOpen()) {
		QLOG_INFO() << "Closing output device file" << mFile.fileName();
		mFile.close();
	}
}

void TrikOutputDeviceFile::write(const QString &data)
{
	mFile.write(data.toUtf8());
	mFile.flush();
}

bool TrikOutputDeviceFile::write(const QByteArray &data)
{
	if (mFileDescriptor == -1)
		return false;

	const ssize_t written = ::write(mFileDescriptor, data.constData(), static_cast<size_t>(data.size()));
	if (written < 0)
		return false; // EAGAIN: pipe full, drop the frame

	return written == data.size();
}

QString TrikOutputDeviceFile::fileName() const
{
	return mFile.fileName();
}
