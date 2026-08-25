/* Copyright 2026 CyberTech Labs Ltd.
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

#include <QtCore/QScopedPointer>
#include <QtCore/QVector>
#include <cstdint>
#include <cstddef>

class QSocketNotifier;

namespace trikHal {

/// Shared V4L2 capture implementation (mmap buffers, notifier-driven dequeue)
/// for the target hardware. Streams start/stop and USB-webcam exposure handling
/// live here; concrete devices only plug in their format negotiation.
class VideoDeviceFileBase : public VideoDeviceFileInterface
{
	Q_OBJECT

public:
	/// Records the requested capture parameters. The device is not touched until
	/// open() is called.
	VideoDeviceFileBase(const QString &devicePath,
	                    uint32_t width, uint32_t height,
	                    uint32_t preferredFourcc,
	                    uint32_t bufferCount = 3,
	                    bool isWebcam = false,
	                    QObject *parent = nullptr);

	/// Closes the device (idempotent).
	~VideoDeviceFileBase() override;

	/// Opens the device, negotiates the format and maps the capture buffers.
	bool open() override;

	/// Queues the mapped buffers and switches the driver to streaming.
	bool startStreaming() override;

	/// See VideoDeviceFileInterface::fixExposure().
	void fixExposure() override;

	/// Switches the driver off streaming and drops the notifier.
	void stopStreaming() override;

	/// Releases every OS resource held by the device. Idempotent.
	void close() override;

	/// Returns the last dequeued frame, or false when none is pending.
	bool capture(const uint8_t *&data, size_t &size) override;

	/// Returns the current buffer to the driver (QBUF).
	void release() override;

	/// Whether the device file is currently open.
	bool isOpen() const override;

	uint32_t actualWidth() const override { return mWidth; }
	uint32_t actualHeight() const override { return mHeight; }
	uint32_t actualFourcc() const override { return mActualFourcc; }
	uint32_t bytesPerLine() const override { return mLineLen; }

	QString id() const override { return mPath; }

protected:
	/// Sets the capture format via VIDIOC_S_FMT. May be overridden to add extra
	/// device-specific negotiation on top of the base implementation.
	virtual bool setFormat();

	/// Forwards a dequeued frame to the frameReady() signal.
	virtual void onFrameReady(const uint8_t *data, size_t size);

	/// Apply the default V4L2 control values for a USB (UVC) webcam. Mirrors the
	/// init_webcam() step previously performed by the media-sensor init script:
	/// anti-flicker, fixed white balance and gain. Exposure is left on auto.
	void applyWebcamDefaults();

	/// Set a single V4L2 control on the device.
	bool setControl(uint32_t id, int32_t value);

	/// Requests and maps the mmap capture buffers. Cleans up partial state on
	/// failure, so it is safe to call without relying on the caller to close().
	virtual bool allocateBuffers();

	/// Unmaps every capture buffer.
	virtual void freeBuffers();

	/// Configure USERPTR mode.  Must be called before open().
	/// When @p data is non-null the device uses V4L2_MEMORY_USERPTR and
	/// instructs the driver to DMA directly into the caller's buffer,
	/// eliminating the per-frame memcpy.  bufferCount buffers are registered
	/// but only one is queued at a time (single-frame-in-flight pipeline).
	void setUserPtrBuffer(void *data, size_t size) override;

	QString mPath;
	uint32_t mWidth;
	uint32_t mHeight;
	uint32_t mRequestedFourcc;
	uint32_t mActualFourcc = 0;
	uint32_t mLineLen = 0;
	uint32_t mBufferCount;
	bool mIsWebcam = false;
	bool mExposureFixed = false;
	int mFd = -1;
	bool mStreaming = false;

	struct MmapBuf {
		uint8_t *data = nullptr;
		size_t size = 0;
	};
	QVector<MmapBuf> mMmapBufs;

	bool mUseUserPtr = false;
	void *mUserPtrData = nullptr;
	size_t mUserPtrSize = 0;

	const uint8_t *mCurrentData = nullptr;
	size_t mCurrentSize = 0;
	int mCurrentBufIdx = -1;

private Q_SLOTS:
	void onActivated(int fd);

private:
	QScopedPointer<QSocketNotifier> mNotifier;
};

} // namespace trikHal
