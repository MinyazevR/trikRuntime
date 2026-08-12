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

#include "videoDeviceFileBase.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <QtCore/QSocketNotifier>
#include <QsLog.h>

using namespace trikHal;

VideoDeviceFileBase::VideoDeviceFileBase(const QString &devicePath,
                                         uint32_t width, uint32_t height,
                                         uint32_t preferredFourcc,
                                         uint32_t bufferCount,
                                         bool needPalStandard,
                                         QObject *parent)
	: VideoDeviceFileInterface(parent)
	, mPath(devicePath)
	, mWidth(width)
	, mHeight(height)
	, mRequestedFourcc(preferredFourcc)
	, mBufferCount(bufferCount)
	, mNeedPalStandard(needPalStandard)
{
}

VideoDeviceFileBase::~VideoDeviceFileBase()
{
	close();
}

bool VideoDeviceFileBase::isOpen() const
{
	return mFd >= 0;
}

bool VideoDeviceFileBase::open()
{
	mFd = ::open(qPrintable(mPath), O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (mFd < 0) {
		QLOG_ERROR() << "VideoDeviceFileBase: open" << mPath << "failed:" << strerror(errno);
		return false;
	}

	v4l2_capability cap = {};
	if (ioctl(mFd, VIDIOC_QUERYCAP, &cap) == 0) {
		const auto caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
		                      ? cap.device_caps
		                      : cap.capabilities;
		constexpr unsigned needed = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;
		if ((caps & needed) != needed) {
			QLOG_ERROR() << "VideoDeviceFileBase:" << mPath << "missing streaming capture caps";
			close();
			return false;
		}
	} else {
		QLOG_WARN() << "VideoDeviceFileBase: QUERYCAP failed:" << strerror(errno);
	}

	if (!negotiateFormat() || !setFormat() || !allocateBuffers()) {
		close();
		return false;
	}

	QLOG_INFO() << "VideoDeviceFileBase:" << mPath << "opened, format" << Qt::hex << mActualFourcc;
	return true;
}

void VideoDeviceFileBase::close()
{
	mNotifier.reset();

	if (mStreaming)
		stopStreaming();

	freeBuffers();

	if (mFd >= 0) {
		::close(mFd);
		mFd = -1;
	}

	mCurrentData = nullptr;
	mCurrentSize = 0;
	mCurrentBufIdx = -1;

	QLOG_INFO() << "VideoDeviceFileBase:" << mPath << "closed";
}

// ---------------------------------------------------------------------------
// negotiateFormat / setFormat / onFrameReady
// ---------------------------------------------------------------------------

bool VideoDeviceFileBase::negotiateFormat()
{
	mActualFourcc = mRequestedFourcc;

	if (mNeedPalStandard) {
		v4l2_std_id stdid = V4L2_STD_625_50;
		if (ioctl(mFd, VIDIOC_S_STD, &stdid) < 0) {
			QLOG_INFO() << "VideoDeviceFileBase: VIDIOC_S_STD(PAL) failed for" << mPath
			            << strerror(errno);
		}
	}
	return true;
}

bool VideoDeviceFileBase::setFormat()
{
	v4l2_format fmt = {};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = mWidth;
	fmt.fmt.pix.height = mHeight;
	fmt.fmt.pix.pixelformat = mActualFourcc ? mActualFourcc : mRequestedFourcc;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (ioctl(mFd, VIDIOC_TRY_FMT, &fmt) < 0) {
		QLOG_WARN() << "VideoDeviceFileBase: TRY_FMT failed:" << strerror(errno);
	}

	if (ioctl(mFd, VIDIOC_S_FMT, &fmt) < 0) {
		QLOG_ERROR() << "VideoDeviceFileBase: S_FMT failed:" << strerror(errno);
		return false;
	}

	mActualFourcc = fmt.fmt.pix.pixelformat;
	mWidth = fmt.fmt.pix.width;
	mHeight = fmt.fmt.pix.height;
	mLineLen = fmt.fmt.pix.bytesperline;

	QLOG_INFO() << "VideoDeviceFileBase: format" << Qt::hex << mActualFourcc
	            << Qt::dec << mWidth << 'x' << mHeight;
	return true;
}

// ---------------------------------------------------------------------------
// onActivated  (QSocketNotifier slot)
// ---------------------------------------------------------------------------

void VideoDeviceFileBase::onFrameReady(const uint8_t *data, size_t size)
{
	emit frameReady(data, size);
}

// ---------------------------------------------------------------------------
// capture / release
// ---------------------------------------------------------------------------

bool VideoDeviceFileBase::capture(const uint8_t *&data, size_t &size)
{
	if (!mCurrentData)
		return false;
	data = mCurrentData;
	size = mCurrentSize;
	return true;
}

void VideoDeviceFileBase::release()
{
	if (mCurrentBufIdx < 0 || mCurrentBufIdx >= static_cast<int>(mMmapBufs.size()))
		return;

	v4l2_buffer buf = {};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = mCurrentBufIdx;

	if (ioctl(mFd, VIDIOC_QBUF, &buf) < 0) {
		QLOG_ERROR() << "VideoDeviceFileBase: QBUF failed:" << strerror(errno);
	}

	mCurrentData = nullptr;
	mCurrentSize = 0;
	mCurrentBufIdx = -1;

	if (mNotifier)
		mNotifier->setEnabled(true);
}

// ---------------------------------------------------------------------------
// onActivated  (QSocketNotifier slot)
// ---------------------------------------------------------------------------

void VideoDeviceFileBase::onActivated(int fd)
{
	Q_UNUSED(fd);

	v4l2_buffer buf = {};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (ioctl(mFd, VIDIOC_DQBUF, &buf) < 0) {
		if (errno != EAGAIN) {
			QLOG_ERROR() << "VideoDeviceFileBase: DQBUF failed:" << strerror(errno);
		}
		return;
	}

	if (buf.index >= static_cast<decltype(buf.index)>(mMmapBufs.size())) {
		QLOG_ERROR() << "VideoDeviceFileBase: invalid buffer index" << buf.index;
		return;
	}

	mCurrentData = mMmapBufs[buf.index].data;
	mCurrentSize = buf.bytesused;
	mCurrentBufIdx = buf.index;

	if (mNotifier)
		mNotifier->setEnabled(false);

	onFrameReady(mCurrentData, mCurrentSize);
}

// ---------------------------------------------------------------------------
// allocateBuffers / freeBuffers / startStreaming / stopStreaming
// ---------------------------------------------------------------------------

bool VideoDeviceFileBase::allocateBuffers()
{
	v4l2_requestbuffers req = {};
	req.count = mBufferCount;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(mFd, VIDIOC_REQBUFS, &req) < 0) {
		QLOG_ERROR() << "VideoDeviceFileBase: REQBUFS failed:" << strerror(errno);
		return false;
	}

	if (req.count == 0) {
		QLOG_ERROR() << "VideoDeviceFileBase: REQBUFS returned 0 buffers";
		return false;
	}

	const uint32_t count = std::min<uint32_t>(req.count, mBufferCount);
	mMmapBufs.reserve(count);

	for (uint32_t i = 0; i < count; ++i) {
		v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (ioctl(mFd, VIDIOC_QUERYBUF, &buf) < 0) {
			QLOG_ERROR() << "VideoDeviceFileBase: QUERYBUF" << i << "failed:" << strerror(errno);
			return false;
		}

		auto *map = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
		                 MAP_SHARED, mFd, buf.m.offset);
		if (map == MAP_FAILED) {
			QLOG_ERROR() << "VideoDeviceFileBase: mmap" << i << "failed:" << strerror(errno);
			return false;
		}

		mMmapBufs.push_back({static_cast<uint8_t *>(map), buf.length});
	}

	QLOG_INFO() << "VideoDeviceFileBase:" << mMmapBufs.size() << "buffers allocated";
	return true;
}

void VideoDeviceFileBase::freeBuffers()
{
	for (auto &buf : mMmapBufs) {
		if (buf.data != nullptr && buf.data != MAP_FAILED)
			munmap(buf.data, buf.size);
	}
	mMmapBufs.clear();
}

bool VideoDeviceFileBase::startStreaming()
{
	for (uint32_t i = 0; i < static_cast<uint32_t>(mMmapBufs.size()); ++i) {
		v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (ioctl(mFd, VIDIOC_QBUF, &buf) < 0) {
			QLOG_ERROR() << "VideoDeviceFileBase: QBUF" << i << "failed:" << strerror(errno);
			return false;
		}
	}

	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(mFd, VIDIOC_STREAMON, &type) < 0) {
		QLOG_ERROR() << "VideoDeviceFileBase: STREAMON failed:" << strerror(errno);
		return false;
	}

	mStreaming = true;

	mNotifier.reset(new QSocketNotifier(mFd, QSocketNotifier::Read, this));
	connect(mNotifier.data(), &QSocketNotifier::activated,
	        this, &VideoDeviceFileBase::onActivated);

	return true;
}

void VideoDeviceFileBase::stopStreaming()
{
	if (!mStreaming || mFd < 0)
		return;

	mNotifier.reset();

	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(mFd, VIDIOC_STREAMOFF, &type) < 0) {
		QLOG_ERROR() << "VideoDeviceFileBase: STREAMOFF failed:" << strerror(errno);
	}
	mStreaming = false;
}
