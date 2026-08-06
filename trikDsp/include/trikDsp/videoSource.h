#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

#include "dspSource.h"

namespace trikDsp {

/// V4L2 video capture source.  Opens a /dev/video* device, negotiates
/// format (NV16 or YUYV), mmap's DMA ring buffers and starts streaming.
/// Delivers raw frame data with no colour conversion.
class VideoSource : public DspSource
{
public:
	/// @param path        device node, e.g. "/dev/video0".
	/// @param width       desired frame width (driver may adjust).
	/// @param height      desired frame height (driver may adjust).
	/// @param pixelFormat PixelFormat::Nv16 or PixelFormat::Yuyv.
	VideoSource(const QString &path, uint32_t width, uint32_t height, PixelFormat pixelFormat);

	~VideoSource() override;             // STREAMOFF + munmap + close

	QString id() const override;         // device path
	int fd() const override;             // non-blocking fd

	bool open() override;                // open + S_FMT + mmap + STREAMON
	void close() override;               // STREAMOFF + munmap + close fd
	bool isOpen() const override { return mFd >= 0; }

	/// Dequeue the next DMA buffer.  data/size reference mmap'd
	/// kernel memory — valid until release() is called.
	bool capture(const uint8_t *&data, size_t &size) override;
	void release() override;             // return buffer to V4L2 ring (QBUF)

	AlgoDescriptor algoDescriptor() const override;

private:
	bool setFormat();                    // TRY_FMT → S_FMT, read actual fmt
	bool initMmap();                     // REQBUFS → QUERYBUF → mmap
	bool startStreaming();               // QBUF all → STREAMON

	QString mPath;
	PixelFormat mRequestedFmt;
	uint32_t mWidth;
	uint32_t mHeight;
	int mFd = -1;
	uint32_t mActualFmt = 0;             // negotiated V4L2 fourcc
	uint32_t mLineLen = 0;               // bytesperline
	bool mStreaming = false;

	struct MmapBuf { uint8_t *data; size_t size; };
	QVector<MmapBuf> mMmapBufs;
	int mLastBufIdx = 0;                 // index returned by last DQBUF
};

}
