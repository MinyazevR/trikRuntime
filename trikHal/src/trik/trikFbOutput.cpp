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

#include "trikFbOutput.h"

#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <QsLog.h>

using namespace trikHal::trik;

TrikFbOutput::TrikFbOutput(QObject *parent)
	: FbOutputInterface(parent)
{
}

TrikFbOutput::~TrikFbOutput()
{
	close();
}

bool TrikFbOutput::open()
{
	if (mOpen) return true;

	mFd = ::open("/dev/fb0", O_RDWR);
	if (mFd < 0) {
		QLOG_WARN() << "TrikFbOutput: cannot open /dev/fb0";
		return false;
	}

	fb_fix_screeninfo finfo = {};
	if (::ioctl(mFd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		QLOG_WARN() << "TrikFbOutput: FBIOGET_FSCREENINFO failed";
		::close(mFd);
		mFd = -1;
		return false;
	}

	mMapLen = finfo.smem_len;
	mMap = static_cast<uint8_t *>(
		::mmap(nullptr, mMapLen, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0));
	if (mMap == MAP_FAILED) {
		QLOG_WARN() << "TrikFbOutput: mmap failed";
		::close(mFd);
		mFd = -1;
		mMap = nullptr;
		return false;
	}

	mOpen = true;
	emit started();
	return true;
}

void TrikFbOutput::close()
{
	if (!mOpen) return;

	mOpen = false;

	if (mMap) {
		::munmap(mMap, mMapLen);
		mMap = nullptr;
	}
	if (mFd >= 0) {
		::close(mFd);
		mFd = -1;
	}

	emit finished();
}

bool TrikFbOutput::isOpen() const
{
	return mOpen;
}

void TrikFbOutput::writeFrame(const uint8_t *rgb565)
{
	if (!mOpen || !mMap || !rgb565) return;

	// Hardcoded 240x240 RGB565 = BUFFER_SIZE_FOR_FB
	// Single memcpy – no compositing, no conversion, no malloc.
	::memcpy(mMap, rgb565, 240 * 240 * 2);
}

uint32_t TrikFbOutput::frameWidth() const  { return 240; }
uint32_t TrikFbOutput::frameHeight() const { return 240; }
