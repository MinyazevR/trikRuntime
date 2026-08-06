#include "videoSource.h"
#include "dspConverters.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <QsLog.h>

namespace trikDsp {

namespace {

constexpr size_t V4L2_BUF_COUNT = 3;

}

VideoSource::VideoSource(const QString &path, uint32_t width, uint32_t height, PixelFormat pixelFormat)
	: mPath(path)
	, mRequestedFmt(pixelFormat)
	, mWidth(width)
	, mHeight(height)
{
}

VideoSource::~VideoSource()
{
	close();
}

QString VideoSource::id() const
{
	return mPath;
}

int VideoSource::fd() const
{
	return mFd;
}

AlgoDescriptor VideoSource::algoDescriptor() const
{
	return {fromV4l2Fourcc(mActualFmt), mLineLen};
}

bool VideoSource::open()
{
	mFd = ::open(mPath.toStdString().c_str(), O_RDWR | O_NONBLOCK, 0);
	if (mFd < 0) {
		QLOG_ERROR() << "VideoSource:" << mPath << "open failed:" << strerror(errno);
		return false;
	}

	if (!setFormat() || !initMmap() || !startStreaming()) {
		close();
		return false;
	}

	QLOG_INFO() << "VideoSource:" << mPath << mActualFmt << mWidth << 'x' << mHeight;
	return true;
}

void VideoSource::close()
{
	if (mStreaming) {
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		::ioctl(mFd, VIDIOC_STREAMOFF, &type);
		mStreaming = false;
	}

	for (auto &buf : mMmapBufs) {
		if (buf.data != MAP_FAILED) {
			::munmap(buf.data, buf.size);
		}
	}
	mMmapBufs.clear();

	if (mFd >= 0) {
		::close(mFd);
		mFd = -1;
	}
}

bool VideoSource::capture(const uint8_t *&data, size_t &size)
{
	v4l2_buffer buf = {};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (::ioctl(mFd, VIDIOC_DQBUF, &buf) != 0) {
		if (errno != EAGAIN) {
			QLOG_ERROR() << "VideoSource: DQBUF failed:" << strerror(errno);
		}
		return false;
	}

	if (static_cast<int>(buf.index) >= mMmapBufs.size()) {
		QLOG_ERROR() << "VideoSource: bad buffer index" << buf.index;
		return false;
	}

	mLastBufIdx = static_cast<int>(buf.index);
	data = mMmapBufs[mLastBufIdx].data;
	size = buf.bytesused;
	return true;
}

void VideoSource::release()
{
	v4l2_buffer buf = {};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = static_cast<__u32>(mLastBufIdx);

	if (::ioctl(mFd, VIDIOC_QBUF, &buf) != 0) {
		QLOG_ERROR() << "VideoSource: QBUF failed:" << strerror(errno);
	}
}

bool VideoSource::setFormat()
{
	v4l2_format fmt = {};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = mWidth;
	fmt.fmt.pix.height = mHeight;
	fmt.fmt.pix.pixelformat = toV4l2Fourcc(mRequestedFmt);
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (::ioctl(mFd, VIDIOC_TRY_FMT, &fmt) != 0) {
		QLOG_WARN() << "VideoSource: TRY_FMT failed:" << strerror(errno);
	}

	if (::ioctl(mFd, VIDIOC_S_FMT, &fmt) != 0) {
		QLOG_ERROR() << "VideoSource: S_FMT failed:" << strerror(errno);
		return false;
	}

	mWidth = fmt.fmt.pix.width;
	mHeight = fmt.fmt.pix.height;
	mActualFmt = fmt.fmt.pix.pixelformat;
	mLineLen = fmt.fmt.pix.bytesperline;

	/// Enum all supported formats to check if ours is emulated by the driver
	/// (done in userspace, slower).  Flag is in <linux/videodev2.h> since 3.17.
	for (uint32_t i = 0;; ++i) {
		v4l2_fmtdesc fdesc = {};
		fdesc.index = i;
		fdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (::ioctl(mFd, VIDIOC_ENUM_FMT, &fdesc) != 0)
			break;
		if (fdesc.pixelformat != mActualFmt)
			continue;
		if (fdesc.flags & V4L2_FMT_FLAG_EMULATED) {
			QLOG_WARN() << "VideoSource:" << mPath
			            << "format is emulated, performance will be degraded";
		}
		break;
	}

	return true;
}

bool VideoSource::initMmap()
{
	v4l2_requestbuffers req = {};
	req.count = V4L2_BUF_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (::ioctl(mFd, VIDIOC_REQBUFS, &req) != 0) {
		QLOG_ERROR() << "VideoSource: REQBUFS failed:" << strerror(errno);
		return false;
	}

	if (req.count == 0) {
		QLOG_ERROR() << "VideoSource: REQBUFS returned 0 buffers";
		return false;
	}
	if (req.count < V4L2_BUF_COUNT) {
		QLOG_WARN() << "VideoSource: got" << req.count
		            << "buffers, requested" << V4L2_BUF_COUNT;
	}
	if (req.count > V4L2_BUF_COUNT) {
		req.count = V4L2_BUF_COUNT;
	}

	for (__u32 i = 0; i < req.count; ++i) {
		v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (::ioctl(mFd, VIDIOC_QUERYBUF, &buf) != 0) {
			QLOG_ERROR() << "VideoSource: QUERYBUF failed:" << strerror(errno);
			return false;
		}

		void *start = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
		                     MAP_SHARED, mFd, buf.m.offset);
		if (start == MAP_FAILED) {
			QLOG_ERROR() << "VideoSource: mmap failed:" << strerror(errno);
			return false;
		}

		mMmapBufs.push_back({static_cast<uint8_t *>(start), buf.length});
	}
	return true;
}

bool VideoSource::startStreaming()
{
	for (int i = 0; i < mMmapBufs.size(); ++i) {
		v4l2_buffer buf = {};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = static_cast<__u32>(i);

		if (::ioctl(mFd, VIDIOC_QBUF, &buf) != 0) {
			QLOG_ERROR() << "VideoSource: QBUF failed:" << strerror(errno);
			return false;
		}
	}

	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (::ioctl(mFd, VIDIOC_STREAMON, &type) != 0) {
		QLOG_ERROR() << "VideoSource: STREAMON failed:" << strerror(errno);
		return false;
	}

	mStreaming = true;
	return true;
}

}
